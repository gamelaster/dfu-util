cmake_minimum_required(VERSION 3.15)
project(dfu-util)

add_executable(dfu-util src/main.c
    src/portable.h
    src/dfu_load.c
    src/dfu_load.h
    src/dfu_util.c
    src/dfu_util.h
    src/dfuse.c
    src/dfuse.h
    src/dfuse_mem.c
    src/dfuse_mem.h
    src/dfu.c
    src/dfu.h
    src/usb_dfu.h
    src/dfu_file.c
    src/dfu_file.h
    src/quirks.c
    src/quirks.h)

if (WIN32)
    target_compile_definitions(dfu-util PRIVATE HAVE_WINDOWS_H _CRT_SECURE_NO_WARNINGS HAVE_STRING_H)
    target_include_directories(dfu-util PRIVATE ../libusb-prebuilt/include/libusb-1.0 msvc/getopt)
    target_sources(dfu-util PRIVATE msvc/getopt/getopt.c msvc/getopt/getopt1.c)
    target_link_directories(dfu-util PRIVATE ../libusb-prebuilt/dll)
    target_link_libraries(dfu-util PRIVATE libusb-1.0)
endif ()