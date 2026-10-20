/* Lua (third_party/lua, unmodified) built as one file with 32-bit numbers (see lua32.h).
 * Only the core and the auxiliary library: no standard libraries, so data and scripts loaded
 * into a state can reach nothing the engine doesn't give them. Same order as Lua's onelua.c. */
#define LUA_CORE
#define LUA_LIB
#define ltable_c
#define lvm_c
#include "lprefix.h"
#include "lua32.h"

/* Don't export Lua's internal symbols. */
#undef LUAI_FUNC
#undef LUAI_DDEC
#undef LUAI_DDEF
#define LUAI_FUNC static
#define LUAI_DDEC(def)
#define LUAI_DDEF static

#include "lzio.c"
#include "lctype.c"
#include "lopcodes.c"
#include "lmem.c"
#include "lundump.c"
#include "ldump.c"
#include "lstate.c"
#include "lgc.c"
#include "llex.c"
#include "lcode.c"
#include "lparser.c"
#include "ldebug.c"
#include "lfunc.c"
#include "lobject.c"
#include "ltm.c"
#include "lstring.c"
#include "ltable.c"
#include "ldo.c"
#include "lvm.c"
#include "lapi.c"
#include "lauxlib.c"
