#ifndef _CONFIG_H_
#define _CONFIG_H_

#include <stdint.h>

typedef uint8_t   u8;
typedef uint16_t  u16;
typedef uint32_t  u32;
typedef uint64_t  u64;
typedef int8_t    i8;
typedef int16_t   i16;
typedef int32_t   i32;
typedef int64_t   i64;

#define FALSE 0
#define TRUE 1

#define PACKED  __attribute__((packed))

#define EARLY(n)   __attribute__((constructor(n)))
#define INIT       __attribute__((constructor(600)))
#define DESTROY    __attribute__((destructor(600)))
#define LATE(n)    __attribute__((destructor(n)))

#endif /* _CONFIG_H_ */
