/*
 * neoclip - Neovim clipboard provider
 * Last Change:  2024 Aug 23
 * License:      https://unlicense.org
 * URL:          https://github.com/matveyt/neoclip
 */


#ifndef NEOCLIP_H
#define NEOCLIP_H

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <lua.h>
#include <lauxlib.h>


// Vim register type
enum {
    MCHAR = 0,
    MLINE = 1,
    MBLOCK = 2,
    MAUTO = 255,
};

enum {
    uv_module = lua_upvalueindex(1),    // module name
    uv_share = lua_upvalueindex(2),     // shared value (table or userdata)
};

// userdata : incomplete type
typedef struct neo_UD neo_UD;

// neo_common.c
int neo_id(lua_State* L);   // lua_CFunction(uv_module) => string
void neo_join(lua_State* L, int ix, const char* sep);
void neo_split(lua_State* L, int ix, const void* data, size_t cb, int type);
void neo_inspect(lua_State* L, int ix);                 // debug only
void neo_printf(lua_State* L, const char* fmt, ...);    // debug only

// inline helpers
static inline int neo_absindex(lua_State *L, int ix)
{
    return (0 > ix && ix > LUA_REGISTRYINDEX) ? (ix + 1 + lua_gettop(L)) : ix;
}
static inline bool neo_did(lua_State* L, const char* what)
{
    lua_getfield(L, LUA_REGISTRYINDEX, what);
    bool did = lua_toboolean(L, -1);
    if (!did) {
        lua_pushboolean(L, true);
        lua_setfield(L, LUA_REGISTRYINDEX, what);
    }
    lua_pop(L, 1);
    return did;
}
static inline void neo_pushcfunction(lua_State* L, lua_CFunction fn)
{
    lua_pushvalue(L, uv_module);    // upvalue 1 : module name
    lua_pushvalue(L, uv_share);     // upvalue 2 : shared table
    lua_pushcclosure(L, fn, 2);
}
static inline int neo_type(int ch)
{
    switch (ch) {
    case 'c':
    case 'v':
        return MCHAR;
    case 'l':
    case 'V':
        return MLINE;
    case 'b':
    case '\026':
        return MBLOCK;
    default:
        return MAUTO;
    }
}
static inline neo_UD* neo_checkud(lua_State* L, int ix)
{
    return (neo_UD*)luaL_checkudata(L, ix, lua_tostring(L, uv_module));
}
static inline neo_UD* neo_ud(lua_State* L, int ix)
{
    return (neo_UD*)luaL_testudata(L, ix, lua_tostring(L, uv_module));
}


#endif // NEOCLIP_H
