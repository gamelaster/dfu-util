#ifndef LIB_H
#define LIB_H

#include <stdio.h>

void lib_report_state(const char* state, int progress);
void lib_printf(const char* format, ...);
void lib_fprintf(FILE* stream, const char* format, ...);
#define _PRINTF(format, ...) lib_printf(format, __VA_ARGS__)
#define _FPRINTF(stream, format, ...) lib_fprintf(stream, format, __VA_ARGS__)

#endif