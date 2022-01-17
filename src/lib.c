/*
 * dfu-util
 *
 * Copyright 2007-2008 by OpenMoko, Inc.
 * Copyright 2010-2012 Stefan Schmidt
 * Copyright 2013-2014 Hans Petter Selasky <hps@bitfrost.no>
 * Copyright 2010-2021 Tormod Volden
 * Copyright 2021      Marek Kraus <gamelaster@outlook.com>
 *
 * Originally written by Harald Welte <laforge@openmoko.org>
 *
 * Based on existing code of dfu-programmer-0.4
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifdef WIN32
#define LIBDFU_EXPORT __declspec(dllexport)
#else
#define LIBDFU_EXPORT
#endif

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <libusb.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>

#include "portable.h"
#include "dfu.h"
#include "dfu_file.h"
#include "dfu_load.h"
#include "dfu_util.h"
#include "dfuse.h"

int verbose = 0;

struct dfu_if *dfu_root = NULL;

char *match_path = NULL;
int match_vendor = -1;
int match_product = -1;
int match_vendor_dfu = -1;
int match_product_dfu = -1;
int match_config_index = -1;
int match_iface_index = -1;
int match_iface_alt_index = -1;
int match_devnum = -1;
const char *match_iface_alt_name = NULL;
const char *match_serial = NULL;
const char *match_serial_dfu = NULL;

static int parse_match_value(const char *str, int default_value)
{
  char *remainder;
  int value;

  if (str == NULL) {
    value = default_value;
  } else if (*str == '*') {
    value = -1; /* Match anything */
  } else if (*str == '-') {
    value = 0x10000; /* Impossible vendor/product ID */
  } else {
    value = strtoul(str, &remainder, 16);
    if (remainder == str) {
      value = default_value;
    }
  }
  return value;
}

static void parse_vendprod(const char *str)
{
  const char *comma;
  const char *colon;

  /* Default to match any DFU device in runtime or DFU mode */
  match_vendor = -1;
  match_product = -1;
  match_vendor_dfu = -1;
  match_product_dfu = -1;

  comma = strchr(str, ',');
  if (comma == str) {
    /* DFU mode vendor/product being specified without any runtime
     * vendor/product specification, so don't match any runtime device */
    match_vendor = match_product = 0x10000;
  } else {
    colon = strchr(str, ':');
    if (colon != NULL) {
      ++colon;
      if ((comma != NULL) && (colon > comma)) {
        colon = NULL;
      }
    }
    match_vendor = parse_match_value(str, match_vendor);
    match_product = parse_match_value(colon, match_product);
    if (comma != NULL) {
      /* Both runtime and DFU mode vendor/product specifications are
       * available, so default DFU mode match components to the given
       * runtime match components */
      match_vendor_dfu = match_vendor;
      match_product_dfu = match_product;
    }
  }
  if (comma != NULL) {
    ++comma;
    colon = strchr(comma, ':');
    if (colon != NULL) {
      ++colon;
    }
    match_vendor_dfu = parse_match_value(comma, match_vendor_dfu);
    match_product_dfu = parse_match_value(colon, match_product_dfu);
  }
}

static void parse_serial(char *str)
{
  char *comma;

  match_serial = str;
  comma = strchr(str, ',');
  if (comma == NULL) {
    match_serial_dfu = match_serial;
  } else {
    *comma++ = 0;
    match_serial_dfu = comma;
  }
  if (*match_serial == 0) match_serial = NULL;
  if (*match_serial_dfu == 0) match_serial_dfu = NULL;
}

static int parse_number(char *str, char *nmb)
{
  char *endptr;
  long val;

  errno = 0;
  val = strtol(nmb, &endptr, 0);

  if ((errno == ERANGE && (val == LONG_MAX || val == LONG_MIN))
      || (errno != 0 && val == 0) || (*endptr != '\0')) {
    errx(EX_USAGE, "Something went wrong with the argument of --%s\n", str);
  }

  if (endptr == nmb) {
    errx(EX_USAGE, "No digits were found from the argument of --%s\n", str);
  }

  return (int)val;
}


static void print_version(void)
{
  printf(PACKAGE_STRING "\n\n");
  printf("Copyright 2005-2009 Weston Schmidt, Harald Welte and OpenMoko Inc.\n"
         "Copyright 2010-2021 Tormod Volden and Stefan Schmidt\n"
         "This program is Free Software and has ABSOLUTELY NO WARRANTY\n"
         "Please report bugs to " PACKAGE_BUGREPORT "\n\n");
}

static enum mode mode = MODE_NONE;
static struct dfu_file file;
static const char *dfuse_options = NULL;

LIBDFU_EXPORT int libdfu_execute()
{
  int expected_size = 0;
  unsigned int transfer_size = 0;
  struct dfu_status status;
  libusb_context *ctx;
  char *end;
  int final_reset = 0;
  int wait_device = 0;
  int ret;
  int dfuse_device = 0;
  int fd;
  int detach_delay = 5;
  uint16_t runtime_vendor;
  uint16_t runtime_product;


  /* make sure all prints are flushed */
  setvbuf(stdout, NULL, _IONBF, 0);

  print_version();
  if (mode == MODE_VERSION) {
    exit(EX_OK);
  }

#if defined(LIBUSB_API_VERSION) || defined(LIBUSBX_API_VERSION)
  if (verbose) {
    const struct libusb_version *ver;
    ver = libusb_get_version();
    printf("libusb version %i.%i.%i%s (%i)\n", ver->major,
           ver->minor, ver->micro, ver->rc, ver->nano);
  }
#else
  warnx("libusb version is ancient");
#endif
  if (mode == MODE_NONE && !dfuse_options) {
    fprintf(stderr, "You need to specify one of -D or -U\n");
    exit(EX_USAGE);
  }

  if (match_config_index == 0) {
    /* Handle "-c 0" (unconfigured device) as don't care */
    match_config_index = -1;
  }

  if (mode == MODE_DOWNLOAD) {
    dfu_load_file(&file, MAYBE_SUFFIX, MAYBE_PREFIX);
    /* If the user didn't specify product and/or vendor IDs to match,
     * use any IDs from the file suffix for device matching */
    if (match_vendor < 0 && file.idVendor != 0xffff) {
      match_vendor = file.idVendor;
      printf("Match vendor ID from file: %04x\n", match_vendor);
    }
    if (match_product < 0 && file.idProduct != 0xffff) {
      match_product = file.idProduct;
      printf("Match product ID from file: %04x\n", match_product);
    }
  } else if (mode == MODE_NONE && dfuse_options) {
    /* for DfuSe special commands, match any device */
    mode = MODE_DOWNLOAD;
    file.idVendor = 0xffff;
    file.idProduct = 0xffff;
  }

  if (wait_device) {
    printf("Waiting for device, exit with ctrl-C\n");
  }

  ret = libusb_init(&ctx);
  if (ret)
    errx(EX_IOERR, "unable to initialize libusb: %s", libusb_error_name(ret));

  if (verbose > 2) {
#if defined(LIBUSB_API_VERSION) && LIBUSB_API_VERSION >= 0x01000106
    libusb_set_option(ctx, LIBUSB_OPTION_LOG_LEVEL, LIBUSB_LOG_LEVEL_DEBUG);
#else
    libusb_set_debug(ctx, 255);
#endif
  }
probe:
  probe_devices(ctx);

  if (mode == MODE_LIST) {
    list_dfu_interfaces();
    disconnect_devices();
    libusb_exit(ctx);
    return EX_OK;
  }

  if (dfu_root == NULL) {
    if (wait_device) {
      milli_sleep(20);
      goto probe;
    } else {
      warnx("No DFU capable USB device available");
      libusb_exit(ctx);
      return EX_IOERR;
    }
  } else if (file.bcdDFU == 0x11a && dfuse_multiple_alt(dfu_root)) {
    printf("Multiple alternate interfaces for DfuSe file\n");
  } else if (dfu_root->next != NULL) {
    /* We cannot safely support more than one DFU capable device
     * with same vendor/product ID, since during DFU we need to do
     * a USB bus reset, after which the target device will get a
     * new address */
    errx(EX_IOERR, "More than one DFU capable USB device found! "
                   "Try `--list' and specify the serial number "
                   "or disconnect all but one device\n");
  }

  /* We have exactly one device. Its libusb_device is now in dfu_root->dev */

  printf("Opening DFU capable USB device...\n");
  ret = libusb_open(dfu_root->dev, &dfu_root->dev_handle);
  if (ret || !dfu_root->dev_handle)
    errx(EX_IOERR, "Cannot open device: %s", libusb_error_name(ret));

  printf("Device ID %04x:%04x\n", dfu_root->vendor, dfu_root->product);

  /* If first interface is DFU it is likely not proper run-time */
  if (dfu_root->interface > 0)
    printf("Run-Time device");
  else
    printf("Device");
  printf(" DFU version %04x\n",
         libusb_le16_to_cpu(dfu_root->func_dfu.bcdDFUVersion));

  if (verbose) {
    printf("DFU attributes: (0x%02x)", dfu_root->func_dfu.bmAttributes);
    if (dfu_root->func_dfu.bmAttributes & USB_DFU_CAN_DOWNLOAD)
      printf(" bitCanDnload");
    if (dfu_root->func_dfu.bmAttributes & USB_DFU_CAN_UPLOAD)
      printf(" bitCanUpload");
    if (dfu_root->func_dfu.bmAttributes & USB_DFU_MANIFEST_TOL)
      printf(" bitManifestationTolerant");
    if (dfu_root->func_dfu.bmAttributes & USB_DFU_WILL_DETACH)
      printf(" bitWillDetach");
    printf("\n");
    printf("Detach timeout %d ms\n", libusb_le16_to_cpu(dfu_root->func_dfu.wDetachTimeOut));
  }

  /* Transition from run-Time mode to DFU mode */
  if (!(dfu_root->flags & DFU_IFF_DFU)) {
    int err;
    /* In the 'first round' during runtime mode, there can only be one
    * DFU Interface descriptor according to the DFU Spec. */

    /* FIXME: check if the selected device really has only one */

    runtime_vendor = dfu_root->vendor;
    runtime_product = dfu_root->product;

    printf("Claiming USB DFU (Run-Time) Interface...\n");
    ret = libusb_claim_interface(dfu_root->dev_handle, dfu_root->interface);
    if (ret < 0) {
      errx(EX_IOERR, "Cannot claim interface %d: %s",
           dfu_root->interface, libusb_error_name(ret));
    }

    /* Needed for some devices where the DFU interface is not the first,
     * and should also be safe if there are multiple alt settings.
     * Otherwise skip the request since it might not be supported
     * by the device and the USB stack may or may not recover */
    if (dfu_root->interface > 0 || dfu_root->flags & DFU_IFF_ALT) {
      printf("Setting Alternate Interface zero...\n");
      ret = libusb_set_interface_alt_setting(dfu_root->dev_handle, dfu_root->interface, 0);
      if (ret < 0) {
        errx(EX_IOERR, "Cannot set alternate interface zero: %s", libusb_error_name(ret));
      }
    }

    printf("Determining device status...\n");
    err = dfu_get_status(dfu_root, &status);
    if (err == LIBUSB_ERROR_PIPE) {
      printf("Device does not implement get_status, assuming appIDLE\n");
      status.bStatus = DFU_STATUS_OK;
      status.bwPollTimeout = 0;
      status.bState  = DFU_STATE_appIDLE;
      status.iString = 0;
    } else if (err < 0) {
      errx(EX_IOERR, "error get_status: %s", libusb_error_name(err));
    } else {
      printf("DFU state(%u) = %s, status(%u) = %s\n", status.bState,
             dfu_state_to_string(status.bState), status.bStatus,
             dfu_status_to_string(status.bStatus));
    }
    milli_sleep(status.bwPollTimeout);

    switch (status.bState) {
      case DFU_STATE_appIDLE:
      case DFU_STATE_appDETACH:
        printf("Device really in Run-Time Mode, send DFU "
               "detach request...\n");
        if (dfu_detach(dfu_root->dev_handle,
                       dfu_root->interface, 1000) < 0) {
          warnx("error detaching");
        }
        if (dfu_root->func_dfu.bmAttributes & USB_DFU_WILL_DETACH) {
          printf("Device will detach and reattach...\n");
        } else {
          printf("Resetting USB...\n");
          ret = libusb_reset_device(dfu_root->dev_handle);
          if (ret < 0 && ret != LIBUSB_ERROR_NOT_FOUND)
            errx(EX_IOERR, "error resetting "
                           "after detach: %s", libusb_error_name(ret));
        }
        break;
      case DFU_STATE_dfuERROR:
        printf("dfuERROR, clearing status\n");
        if (dfu_clear_status(dfu_root->dev_handle,
                             dfu_root->interface) < 0) {
          errx(EX_IOERR, "error clear_status");
        }
        /* fall through */
      default:
        warnx("WARNING: Device already in DFU mode? (bState=%d %s)",
              status.bState, dfu_state_to_string(status.bState));
        libusb_release_interface(dfu_root->dev_handle,
                                 dfu_root->interface);
        goto dfustate;
    }
    libusb_release_interface(dfu_root->dev_handle,
                             dfu_root->interface);
    libusb_close(dfu_root->dev_handle);
    dfu_root->dev_handle = NULL;

    /* keeping handles open might prevent re-enumeration */
    disconnect_devices();

    if (mode == MODE_DETACH) {
      libusb_exit(ctx);
      return EX_OK;
    }

    milli_sleep(detach_delay * 1000);

    /* Change match vendor and product to impossible values to force
     * only DFU mode matches in the following probe */
    match_vendor = match_product = 0x10000;

    probe_devices(ctx);

    if (dfu_root == NULL) {
      errx(EX_IOERR, "Lost device after RESET?");
    } else if (dfu_root->next != NULL) {
      errx(EX_IOERR, "More than one DFU capable USB device found! "
                     "Try `--list' and specify the serial number "
                     "or disconnect all but one device");
    }

    /* Check for DFU mode device */
    if (!(dfu_root->flags | DFU_IFF_DFU))
      errx(EX_PROTOCOL, "Device is not in DFU mode");

    printf("Opening DFU USB Device...\n");
    ret = libusb_open(dfu_root->dev, &dfu_root->dev_handle);
    if (ret || !dfu_root->dev_handle) {
      errx(EX_IOERR, "Cannot open device");
    }
  } else {
    /* we're already in DFU mode, so we can skip the detach/reset
     * procedure */
    /* If a match vendor/product was specified, use that as the runtime
     * vendor/product, otherwise use the DFU mode vendor/product */
    runtime_vendor = match_vendor < 0 ? dfu_root->vendor : match_vendor;
    runtime_product = match_product < 0 ? dfu_root->product : match_product;
  }

dfustate:
#if 0
  printf("Setting Configuration %u...\n", dfu_root->configuration);
	ret = libusb_set_configuration(dfu_root->dev_handle, dfu_root->configuration);
	if (ret < 0) {
		errx(EX_IOERR, "Cannot set configuration: %s", libusb_error_name(ret));
	}
#endif
  printf("Claiming USB DFU Interface...\n");
  ret = libusb_claim_interface(dfu_root->dev_handle, dfu_root->interface);
  if (ret < 0) {
    errx(EX_IOERR, "Cannot claim interface - %s", libusb_error_name(ret));
  }

  if (dfu_root->flags & DFU_IFF_ALT) {
    printf("Setting Alternate Interface #%d ...\n", dfu_root->altsetting);
    ret = libusb_set_interface_alt_setting(dfu_root->dev_handle, dfu_root->interface, dfu_root->altsetting);
    if (ret < 0) {
      errx(EX_IOERR, "Cannot set alternate interface: %s", libusb_error_name(ret));
    }
  }

status_again:
  printf("Determining device status...\n");
  ret = dfu_get_status(dfu_root, &status );
  if (ret < 0) {
    errx(EX_IOERR, "error get_status: %s", libusb_error_name(ret));
  }
  printf("DFU state(%u) = %s, status(%u) = %s\n", status.bState,
         dfu_state_to_string(status.bState), status.bStatus,
         dfu_status_to_string(status.bStatus));

  milli_sleep(status.bwPollTimeout);

  switch (status.bState) {
    case DFU_STATE_appIDLE:
    case DFU_STATE_appDETACH:
      errx(EX_PROTOCOL, "Device still in Run-Time Mode!");
      break;
    case DFU_STATE_dfuERROR:
      printf("Clearing status\n");
      if (dfu_clear_status(dfu_root->dev_handle, dfu_root->interface) < 0) {
        errx(EX_IOERR, "error clear_status");
      }
      goto status_again;
      break;
    case DFU_STATE_dfuDNLOAD_IDLE:
    case DFU_STATE_dfuUPLOAD_IDLE:
      printf("Aborting previous incomplete transfer\n");
      if (dfu_abort(dfu_root->dev_handle, dfu_root->interface) < 0) {
        errx(EX_IOERR, "can't send DFU_ABORT");
      }
      goto status_again;
      break;
    case DFU_STATE_dfuIDLE:
    default:
      break;
  }

  if (DFU_STATUS_OK != status.bStatus ) {
    printf("WARNING: DFU Status: '%s'\n",
           dfu_status_to_string(status.bStatus));
    /* Clear our status & try again. */
    if (dfu_clear_status(dfu_root->dev_handle, dfu_root->interface) < 0)
      errx(EX_IOERR, "USB communication error");
    if (dfu_get_status(dfu_root, &status) < 0)
      errx(EX_IOERR, "USB communication error");
    if (DFU_STATUS_OK != status.bStatus)
      errx(EX_PROTOCOL, "Status is not OK: %d", status.bStatus);

    milli_sleep(status.bwPollTimeout);
  }

  printf("DFU mode device DFU version %04x\n",
         libusb_le16_to_cpu(dfu_root->func_dfu.bcdDFUVersion));

  if (dfu_root->func_dfu.bcdDFUVersion == libusb_cpu_to_le16(0x11a))
    dfuse_device = 1;
  else if (dfuse_options)
    printf("Warning: DfuSe option used on non-DfuSe device\n");

  /* Get from device or user, warn if overridden */
  int func_dfu_transfer_size = libusb_le16_to_cpu(dfu_root->func_dfu.wTransferSize);
  if (func_dfu_transfer_size) {
    printf("Device returned transfer size %i\n", func_dfu_transfer_size);
    if (!transfer_size)
      transfer_size = func_dfu_transfer_size;
    else
      printf("Warning: Overriding device-reported transfer size\n");
  } else {
    if (!transfer_size)
      errx(EX_USAGE, "Transfer size must be specified");
  }

#ifdef __linux__
  /* limited to 4k in libusb Linux backend */
	if ((int)transfer_size > 4096) {
		transfer_size = 4096;
		printf("Limited transfer size to %i\n", transfer_size);
	}
#endif /* __linux__ */

  if (transfer_size < dfu_root->bMaxPacketSize0) {
    transfer_size = dfu_root->bMaxPacketSize0;
    printf("Adjusted transfer size to %i\n", transfer_size);
  }

  switch (mode) {
    case MODE_UPLOAD:
      /* open for "exclusive" writing */
      fd = open(file.name, O_WRONLY | O_BINARY | O_CREAT | O_EXCL | O_TRUNC, 0666);
      if (fd < 0) {
        warn("Cannot open file %s for writing", file.name);
        ret = EX_CANTCREAT;
        break;
      }

      if (dfuse_device || dfuse_options) {
        ret = dfuse_do_upload(dfu_root, transfer_size, fd, dfuse_options);
      } else {
        ret = dfuload_do_upload(dfu_root, transfer_size, expected_size, fd);
      }
      close(fd);
      if (ret < 0)
        ret = EX_IOERR;
      else
        ret = EX_OK;
      break;

    case MODE_DOWNLOAD:
      if (((file.idVendor  != 0xffff && file.idVendor  != runtime_vendor) ||
          (file.idProduct != 0xffff && file.idProduct != runtime_product)) &&
          ((file.idVendor  != 0xffff && file.idVendor  != dfu_root->vendor) ||
              (file.idProduct != 0xffff && file.idProduct != dfu_root->product))) {
        errx(EX_USAGE, "Error: File ID %04x:%04x does "
                       "not match device (%04x:%04x or %04x:%04x)",
             file.idVendor, file.idProduct,
             runtime_vendor, runtime_product,
             dfu_root->vendor, dfu_root->product);
      }
      if (dfuse_device || dfuse_options || file.bcdDFU == 0x11a) {
        ret = dfuse_do_dnload(dfu_root, transfer_size, &file, dfuse_options);
      } else {
        ret = dfuload_do_dnload(dfu_root, transfer_size, &file);
      }
      if (ret < 0)
        ret = EX_IOERR;
      else
        ret = EX_OK;
      break;
    case MODE_DETACH:
      ret = dfu_detach(dfu_root->dev_handle, dfu_root->interface, 1000);
      if (ret < 0) {
        warnx("can't detach");
        /* allow combination with final_reset */
        ret = 0;
      }
      break;
    default:
      warnx("Unsupported mode: %u", mode);
      ret = EX_SOFTWARE;
      break;
  }

  if (!ret && final_reset) {
    ret = dfu_detach(dfu_root->dev_handle, dfu_root->interface, 1000);
    if (ret < 0) {
      /* Even if detach failed, just carry on to leave the
                           device in a known state */
      warnx("can't detach");
    }
    printf("Resetting USB to switch back to Run-Time mode\n");
    ret = libusb_reset_device(dfu_root->dev_handle);
    if (ret < 0 && ret != LIBUSB_ERROR_NOT_FOUND) {
      warnx("error resetting after download: %s", libusb_error_name(ret));
      ret = EX_IOERR;
    }
  }

  libusb_close(dfu_root->dev_handle);
  dfu_root->dev_handle = NULL;

  disconnect_devices();
  libusb_exit(ctx);
  return ret;
}

LIBDFU_EXPORT void libdfu_set_download(const char *filename)
{
  mode = MODE_DOWNLOAD;
  memset(&file, 0, sizeof(file));
  file.name = filename;
}

LIBDFU_EXPORT void libdfu_set_altsetting(int alt)
{
  match_iface_alt_index = alt;
}

LIBDFU_EXPORT void libdfu_set_vendprod(int vendor, int product)
{
  match_vendor = vendor;
  match_product = product;
}

LIBDFU_EXPORT void libdfu_set_dfuse_options(const char *dfuse_opts)
{
  dfuse_options = strdup(dfuse_opts);
}

static void (*libdfu_stderr_callback)(const char *) = NULL;

LIBDFU_EXPORT void libdfu_set_stderr_callback(void (*callback)(const char *))
{
  libdfu_stderr_callback = callback;
}

static void (*libdfu_stdout_callback)(const char *) = NULL;

LIBDFU_EXPORT void libdfu_set_stdout_callback(void (*callback)(const char *))
{
  libdfu_stdout_callback = callback;
}

static void (*libdfu_progress_callback)(const char *, int) = NULL;

LIBDFU_EXPORT void libdfu_set_progress_callback(void (*callback)(const char *, int))
{
  libdfu_progress_callback = callback;
}

void lib_printf(const char* format, ...)
{
  if (libdfu_stdout_callback != NULL) {
    static char* data = NULL;
    if (data == NULL) {
      data = malloc(4096);
    }
    va_list args;
    va_start(args, format);
    vsnprintf(data, 4096, format, args);
    va_end(args);
    libdfu_stdout_callback(data);
  }
}

void lib_fprintf(FILE* stream, const char* format, ...)
{
  if (libdfu_stderr_callback != NULL) {
    static char* data = NULL;
    if (data == NULL) {
      data = malloc(4096);
    }
    va_list args;
        va_start(args, format);
    vsnprintf(data, 4096, format, args);
        va_end(args);
    libdfu_stderr_callback(data);
  }
}
