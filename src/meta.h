#ifndef _META_H_
#define _META_H_

#include "lhash.h"
#include "luav.h"

#define META_INVALID      100 // just some random number
#define META_UNUSED       0
#define META_ADD          1
#define META_SUB          2
#define META_MUL          3
#define META_DIV          4
#define META_MOD          5
#define META_POW          6
#define META_UNM          7
#define META_CONCAT       8
#define META_LEN          9
#define META_EQ           10
#define META_LT           11
#define META_LE           12
#define META_INDEX        13
#define META_NEWINDEX     14
#define META_CALL         15
#define META_METATABLE    16
#define META_TOSTRING     17
#define NUM_META_METHODS  18

extern luav meta_strings[NUM_META_METHODS];

#define TBL(x) lv_gettable(x,0)
#define getmetatable(v) (lv_istable(v)    ? TBL(v)->metatable :                \
                         lv_isuserdata(v) ? TBL(lhash_get(&userdata_meta, v)) :\
                         NULL)

extern lhash_t userdata_meta;

#endif /* _META_H_ */
