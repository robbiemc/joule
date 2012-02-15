#ifndef _META_H_
#define _META_H_

#include "lhash.h"
#include "luav.h"

#define META_INVALID_IDX      100 // just some random number
#define META_UNUSED_IDX       0
#define META_ADD_IDX          1
#define META_SUB_IDX          2
#define META_MUL_IDX          3
#define META_DIV_IDX          4
#define META_MOD_IDX          5
#define META_POW_IDX          6
#define META_UNM_IDX          7
#define META_CONCAT_IDX       8
#define META_LEN_IDX          9
#define META_EQ_IDX           10
#define META_LT_IDX           11
#define META_LE_IDX           12
#define META_INDEX_IDX        13
#define META_NEWINDEX_IDX     14
#define META_CALL_IDX         15
#define META_METATABLE_IDX    16
#define META_TOSTRING_IDX     17
#define NUM_META_METHODS      18

extern luav meta_strings[NUM_META_METHODS];

#define META_ADD        meta_strings[META_ADD_IDX]
#define META_SUB        meta_strings[META_SUB_IDX]
#define META_MUL        meta_strings[META_MUL_IDX]
#define META_DIV        meta_strings[META_DIV_IDX]
#define META_MOD        meta_strings[META_MOD_IDX]
#define META_POW        meta_strings[META_POW_IDX]
#define META_UNM        meta_strings[META_UNM_IDX]
#define META_CONCAT     meta_strings[META_CONCAT_IDX]
#define META_LEN        meta_strings[META_LEN_IDX]
#define META_EQ         meta_strings[META_EQ_IDX]
#define META_LT         meta_strings[META_LT_IDX]
#define META_LE         meta_strings[META_LE_IDX]
#define META_INDEX      meta_strings[META_INDEX_IDX]
#define META_NEWINDEX   meta_strings[META_NEWINDEX_IDX]
#define META_CALL       meta_strings[META_CALL_IDX]
#define META_METATABLE  meta_strings[META_METATABLE_IDX]
#define META_TOSTRING   meta_strings[META_TOSTRING_IDX]

#define TBL(x) ((lhash_t*) lv_getptr(x))
#define getmetatable(v) (lv_istable(v)    ? TBL(v)->metatable :                \
                         lv_isuserdata(v) ? TBL(lhash_get(&userdata_meta, v)) :\
                         NULL)

extern lhash_t userdata_meta;

#endif /* _META_H_ */
