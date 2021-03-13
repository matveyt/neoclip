/*
 * neoclip - Neovim clipboard provider
 * Last Change:  2021 Mar 09
 * License:      https://unlicense.org
 * URL:          https://github.com/matveyt/neoclip
 */


#ifndef NEOCLIP_H
#define NEOCLIP_H


#include <lua.h>
#include <lauxlib.h>


// exported to Lua
extern int neo_id(lua_State* L);
extern int neo_start(lua_State* L);
extern int neo_stop(lua_State* L);
extern int neo_get(lua_State* L);
extern int neo_set(lua_State* L);


// splitjoin.c
extern int neo_split(lua_State* L, const void* data, size_t cb);
extern void neo_join(lua_State* L, const char* sep);


#endif // NEOCLIP_H
