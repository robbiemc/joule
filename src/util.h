#ifndef _UTIL_H
#define _UTIL_H

#include <stdint.h>
#include <stdio.h>

uint8_t   read1(FILE *f);
uint32_t  read4(FILE *f);
uint64_t  read8(FILE *f);

#endif /* _UTIL_H */
