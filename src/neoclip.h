/*
 * neoclip - Neovim clipboard provider
 * Last Change:  2024 Aug 19
 * License:      https://unlicense.org
 * URL:          https://github.com/matveyt/neoclip
 */


#ifndef NEOCLIP_H
#define NEOCLIP_H

#include <lua.h>
#include <lauxlib.h>


enum {
    uv_module = lua_upvalueindex(1),    // module name
    uv_share = lua_upvalueindex(2),     // shared value (table or userdata)
};

// incomplete type
typedef struct neo_UD neo_UD;

// neo_common.c
void neo_join(lua_State* L, int ix, const char* sep);
void neo_split(lua_State* L, int ix, const void* data, size_t cb, int type);
int neo_id(lua_State* L);                               // lua_CFunction() => string
void neo_inspect(lua_State* L, int ix);                 // debug only
void neo_printf(lua_State* L, const char* fmt, ...);    // debug only

// inline helpers
static inline int neo_absindex(lua_State *L, int ix)
{
    return (0 > ix && ix > LUA_REGISTRYINDEX) ? (ix + 1 + lua_gettop(L)) : ix;
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
        return 0;   // MCHAR
    case 'l':
    case 'V':
        return 1;   // MLINE
    case 'b':
    case '\026':
        return 2;   // MBLOCK
    default:
        return 255; // MAUTO
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
