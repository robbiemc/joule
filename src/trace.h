#ifndef _TRACE_H
#define _TRACE_H

#include "config.h"

typedef u32 traceinfo_t;

/* Package for tracing information */
typedef struct trace {
  traceinfo_t args;
  traceinfo_t *instrs;
} trace_t;

#define TYPEBITS 4
#define TYPEMASK ((1 << TYPEBITS) - 1)
#define TYPEMASKN(n) (TYPEMASK << ((n) * TYPEBITS))
#define TRACELIMIT 8
#define GET_TRACETYPE(bits, n) (((bits) >> ((n) * TYPEBITS)) & TYPEMASK)
#define TRACEINFO_NONE1 LANY
#define TRACEINFO_NONE2 ((TRACEINFO_NONE1 << TYPEBITS) | TRACEINFO_NONE1)
#define TRACEINFO_NONE4 ((TRACEINFO_NONE2 << (TYPEBITS * 2)) | TRACEINFO_NONE2)
#define TRACEINFO_NONE  ((TRACEINFO_NONE4 << (TYPEBITS * 4)) | TRACEINFO_NONE4)

#define BUILD_TRACEINFO1(type) (type)
#define BUILD_TRACEINFO2(type1, type2) \
  ((traceinfo_t) ((type1) | ((type2) << TYPEBITS)))
#define SET_TRACEINFO(info, type, n) \
  ((u32) (((info) & (u32) ~TYPEMASKN(n)) | (u32) ((type) << ((n) * TYPEBITS))))

void trace_init(trace_t *trace, size_t instrs);

#endif /* _TRACE_H */
