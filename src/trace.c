/**
 * @file trace.c
 * @brief Implementation of tracing information...
 */

#include <string.h>

#include "gc.h"
#include "luav.h"
#include "trace.h"

/**
 * @brief Initialize a trace information structure
 *
 * @param trace the trace to initialize
 * @param instrs the number of instructions to allocate for the trace
 */
void trace_init(trace_t *trace, size_t instrs) {
  size_t size = instrs * sizeof(traceinfo_t);
  trace->instrs = gc_alloc(size, LANY);
  memset(&trace->args, LANY, sizeof(traceinfo_t));
  memset(trace->instrs, LANY, size);

  size_t tsize = instrs * sizeof(tableinfo_t);
  trace->tables = gc_alloc(tsize, LANY);
  memset(trace->tables, 0, tsize);
}
