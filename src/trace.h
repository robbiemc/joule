#ifndef _TRACE_H
#define _TRACE_H

#include "config.h"
#include "luav.h"

#define TRACELIMIT 8
#define TRACEMAX   255
#define TRACE_UPVAL (1 << 7)
#define TRACE_CONST (1 << 6)
#define TRACE_ISUPVAL(v) ((v) & TRACE_UPVAL)
#define TRACE_ISCONST(v) ((v) & TRACE_CONST)
#define TRACE_TYPEMASK 0xf

struct lclosure;

typedef u8 traceinfo_t[TRACELIMIT];

typedef struct tableinfo {
  struct lhash  *pointer;
  u64           version;
  luav          value;
} tableinfo_t;

typedef union misc {
  tableinfo_t table;
  struct lclosure *closure;
} misc_t;

/* Package for tracing information */
typedef struct trace {
  traceinfo_t args;
  traceinfo_t *instrs;
  misc_t      *misc;
} trace_t;

void trace_init(trace_t *trace, size_t instrs);

#endif /* _TRACE_H */
