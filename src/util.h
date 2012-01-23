#ifndef _UTIL_H
#define _UTIL_H

#include <malloc.h>
#include <stdint.h>
#include <stdio.h>

typedef uint8_t   u8;
typedef uint16_t  u16;
typedef uint32_t  u32;
typedef uint64_t  u64;
typedef int8_t    i8;
typedef int16_t   i16;
typedef int32_t   i32;
typedef int64_t   i64;

uint8_t   read1(FILE *f);
uint32_t  read4(FILE *f);
uint64_t  read8(FILE *f);

uint8_t   pread1(uint8_t **p);
uint32_t  pread4(uint8_t **p);
uint64_t  pread8(uint8_t **p);

/* calls malloc and asserts that it succeeded */
void *amalloc(size_t s);

#endif /* _UTIL_H */
