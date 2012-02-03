#ifndef _LIB_COROUTINE_H
#define _LIB_COROUTINE_H

struct lthread;

struct lthread* coroutine_current(void);
void            coroutine_changeenv(struct lthread *to);

#endif /* _LIB_COROUTINE_H */
