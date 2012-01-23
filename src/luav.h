#ifndef _LUA_H_
#define _LUA_H_

#include "lstring.h"

typedef uint64_t luav;

extern const luav LV_NIL;
extern const luav LV_TRUE;
extern const luav LV_FALSE;

luav lv_bool(u8 v);
luav lv_number(u64 v);
luav lv_string(lstring_t *v);

#endif /* _LUA_H_ */
