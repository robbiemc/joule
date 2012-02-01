#ifndef _META_H_
#define _META_H_

#include "vm.h"

#define META_INVALID      100 // just some random number
#define META_ADD          0
#define META_SUB          1
#define META_MUL          2
#define META_DIV          3
#define META_MOD          4
#define META_POW          5
#define META_UNM          6
#define META_CONCAT       7
#define META_LEN          8
#define META_EQ           9
#define META_LT           10
#define META_LE           11
#define META_INDEX        12
#define META_NEWINDEX     13
#define META_CALL         14
#define META_METATABLE    15
#define NUM_META_METHODS  16

#endif /* _META_H_ */
