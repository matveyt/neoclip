/*
 * neoclip - Neovim clipboard provider
 * Last Change:  2024 Aug 14
 * License:      https://unlicense.org
 * URL:          https://github.com/matveyt/neoclip
 */


#ifndef NEOCLIP_H
#define NEOCLIP_H

#include <lua.h>
#include <lauxlib.h>


// userdata
typedef struct neo_UD neo_UD;

// module interface
extern int neo_start(lua_State* L);
extern int neo_stop(lua_State* L);
extern int neo__gc(lua_State* L);
extern int neo_status(lua_State* L);
extern int neo_get(lua_State* L);
extern int neo_set(lua_State* L);

// neo_common.c
extern int neo_id(lua_State* L);                            // lua_CFunction
extern void neo_join(lua_State* L, int ix, const char* sep);
extern int neo_nil(lua_State* L);                           // lua_CFunction
extern void neo_split(lua_State* L, int ix, const void* data, size_t cb, int type);
extern int neo_true(lua_State* L);                          // lua_CFunction
extern int neo_type(int ch);
extern int neo_vimg(lua_State* L, const char* var, int dflt);
extern void neo_inspect(lua_State* L, int ix);              // debug only
extern void neo_printf(lua_State* L, const char* fmt, ...); // debug only

// inline helpers
static inline int neo_absindex(lua_State *L, int ix)
{
    return (0 > ix && ix > LUA_REGISTRYINDEX) ? (ix + 1 + lua_gettop(L)) : ix;
}
static inline void neo_pushname(lua_State* L)
{
    lua_pushvalue(L, lua_upvalueindex(1));
}
static inline neo_UD* neo_ud(lua_State* L)
{
    return (neo_UD*)luaL_checkudata(L, lua_upvalueindex(2),
        lua_tostring(L, lua_upvalueindex(1)));
}


#endif // NEOCLIP_H
