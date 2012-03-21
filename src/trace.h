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
#define TRACELIMIT 8
#define GET_TRACETYPE(bits, n) (((bits) >> ((n) * TYPEBITS)) & TYPEMASK)
#define BUILD_TRACEINFO1(type) (type)
#define BUILD_TRACEINFO2(type1, type2) \
  ((traceinfo_t) ((type1) | ((type2) << TYPEBITS)))

void trace_init(trace_t *trace, size_t instrs);

#endif /* _TRACE_H */
