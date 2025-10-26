#ifndef LOG_H
#define LOG_H

#include <stdio.h>

extern int VERBOSE;

#define LOG(output, fmt, ...)                                                  \
  do {                                                                         \
    if (VERBOSE)                                                               \
      fprintf(output, fmt "\n", ##__VA_ARGS__);                   \
  } while (0)

#endif
