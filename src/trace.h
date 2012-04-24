#ifndef _TRACE_H
#define _TRACE_H

#include "config.h"

#define TRACELIMIT 8
#define TRACEMAX   255
#define TRACE_UPVAL (1 << 7)
#define TRACE_ISUPVAL(v) ((v) & TRACE_UPVAL)
#define TRACE_TYPEMASK 0xf

typedef u8 traceinfo_t[TRACELIMIT];

typedef u64 tableinfo_t[2];

/* Package for tracing information */
typedef struct trace {
  traceinfo_t args;
  traceinfo_t *instrs;
  tableinfo_t *tables;
} trace_t;

void trace_init(trace_t *trace, size_t instrs);

#endif /* _TRACE_H */
