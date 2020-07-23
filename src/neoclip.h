/*
 * neoclip - Neovim clipboard provider
 * Last Change:  2020 Jul 23
 * License:      https://unlicense.org
 * URL:          https://github.com/matveyt/neoclip
 */


#ifndef NEOCLIP_H
#define NEOCLIP_H


#include <lua.h>
#include <lauxlib.h>


// neoclip_w32.c
extern int neo_get(lua_State* L);
extern int neo_set(lua_State* L);


// splitjoin.c
extern int neo_split(lua_State* L, const void* data, size_t cb);
extern void neo_join(lua_State* L, const char* sep);


#endif // NEOCLIP_H
