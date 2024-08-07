/*
 * neoclip - Neovim clipboard provider
 * Last Change:  2024 Aug 08
 * License:      https://unlicense.org
 * URL:          https://github.com/matveyt/neoclip
 */


#ifndef NEOCLIP_H
#define NEOCLIP_H


#include <lua.h>
#include <lauxlib.h>


// exports to Lua
extern int neo_start(lua_State* L);
extern int neo_stop(lua_State* L);
extern int neo__gc(lua_State* L);
extern int neo_status(lua_State* L);
extern int neo_get(lua_State* L);
extern int neo_set(lua_State* L);
extern int neo_nil(lua_State* L);   // neo_common.c: start, stop
extern int neo_true(lua_State* L);  // neo_common.c: status
extern int neo_id(lua_State* L);    // neo_common.c: id

// shared upvalues
#define neo_pushname(L) lua_pushvalue((L), lua_upvalueindex(1))
#define neo_ud(L) ((UD*)luaL_checkudata((L), lua_upvalueindex(2), \
    lua_tostring(L, lua_upvalueindex(1))))


// internal helpers from neo_common.c
extern int neo_type(int ch);
extern void neo_split(lua_State* L, int ix, const void* data, size_t cb, int type);
extern void neo_join(lua_State* L, int ix, const char* sep);
extern int neo_vimg(lua_State* L, const char* var, int dflt);


#endif // NEOCLIP_H
