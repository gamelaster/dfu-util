#ifndef LIBDFU_UTIL_H
#define LIBDFU_UTIL_H

void libdfu_set_download(const char *filename);
void libdfu_set_altsetting(int alt);
void libdfu_set_vendprod(int vendor, int product);
void libdfu_set_dfuse_options(const char *dfuse_opts);
int libdfu_execute();
void libdfu_set_stderr_callback(void (*callback)(const char *));
void libdfu_set_stdout_callback(void (*callback)(const char *));
void libdfu_set_progress_callback(void (*callback)(const char *, int));

#endif