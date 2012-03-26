#ifndef _TRACE_H
#define _TRACE_H

#include "config.h"

#define TRACELIMIT 8
#define TRACE_UPVAL (1 << 7)

typedef u8 traceinfo_t[TRACELIMIT];

/* Package for tracing information */
typedef struct trace {
  traceinfo_t args;
  traceinfo_t *instrs;
} trace_t;

void trace_init(trace_t *trace, size_t instrs);

#endif /* _TRACE_H */
