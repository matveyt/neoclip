/*
 * neoclip - Neovim clipboard provider
 * Last Change:  2025 Jun 20
 * License:      https://unlicense.org
 * URL:          https://github.com/matveyt/neoclip
 */


#if !defined(NEOCLIP_H)
#define NEOCLIP_H

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <lua.h>
#include <lauxlib.h>

#if !defined(_countof)
#define _countof(o) (sizeof(o) / sizeof(o[0]))
#endif // _countof


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

// API implementation
int neo_start(lua_State* L);    // lua_CFunction() => nil or error
int neo_stop(lua_State* L);     // lua_CFunction() => nil
int neo_status(lua_State* L);   // lua_CFunction() => boolean
int neo_get(lua_State* L);      // lua_CFunction(reg) => {string_array, type}
int neo_set(lua_State* L);      // lua_CFunction(reg, string_array, type) => boolean
int neo__gc(lua_State* L);      // destroy state

// neo_common.c
int neo_id(lua_State* L);       // lua_CFunction(uv_module) => string
int neo_nil(lua_State* L);      // lua_CFunction() => nil
int neo_true(lua_State* L);     // lua_CFunction() => true
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
