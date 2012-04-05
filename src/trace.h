#ifndef _TRACE_H
#define _TRACE_H

#include "config.h"

#define TRACELIMIT 8
#define TRACEMAX   255
#define TRACE_UPVAL (1 << 7)
#define TRACE_ISUPVAL(v) ((v) & TRACE_UPVAL)
#define TRACE_TYPEMASK 0xf

/* Bits for branches, everything */
#define TRACE_JUMP (1 << 3)
#define TRACE_FALLEN (1 << 2)
#define TRACE_SET_JUMP(t) ((t) & ~TRACE_JUMP)
#define TRACE_SET_FALLEN(t) ((t) & ~TRACE_FALLEN)
#define TRACE_HAS_JUMPED(t) (!((t) & TRACE_JUMP))
#define TRACE_HAS_FALLEN(t) (!((t) & TRACE_FALLEN))

typedef u8 traceinfo_t[TRACELIMIT];

/* Package for tracing information */
typedef struct trace {
  traceinfo_t args;
  traceinfo_t *instrs;
} trace_t;

void trace_init(trace_t *trace, size_t instrs);

#endif /* _TRACE_H */
