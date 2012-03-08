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

#define PACKED     __attribute__((packed))
#define NORETURN   __attribute__((noreturn))
#define MUST_CHECK __attribute__((warn_unused_result))
#if defined(__APPLE__) && !defined(__clang__)
# define EARLY(n)   __attribute__((constructor))
# define LATE(n)    __attribute__((destructor))
# define INIT       __attribute__((constructor))
# define DESTROY    __attribute__((destructor))
#else
# define EARLY(n)   __attribute__((constructor(200+n)))
# define LATE(n)    __attribute__((destructor(200+n)))
# define INIT       __attribute__((constructor(500)))
# define DESTROY    __attribute__((destructor(500)))
#endif

#define INIT_HEAP_SIZE    (128 * 1024)
#define LUAV_INIT_STRING  100
#define LUA_NUMBER_FMT    "%.14g"
#define LFIELDS_PER_FLUSH 50

#endif /* _CONFIG_H_ */
