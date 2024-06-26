/*
 * neoclip - Neovim clipboard provider
 * Last Change:  2024 Jun 25
 * License:      https://unlicense.org
 * URL:          https://github.com/matveyt/neoclip
 */


#ifndef NEOCLIP_H
#define NEOCLIP_H


#include <lua.h>
#include <lauxlib.h>


#ifndef _CONCAT
#define _CONCAT(_Token1,_Token2)   _CONCAT2(_Token1,_Token2)
#define _CONCAT2(_Token1,_Token2)  _Token1##_Token2
#endif // _CONCAT

#ifndef _STRINGIZE
#define _STRINGIZE(_Token)          _STRINGIZE2(_Token)
#define _STRINGIZE2(_Token)         #_Token
#endif // _STRINGIZE


#ifndef PLATFORM
#error PLATFORM must be defined!
#endif // PLATFORM

// supported platforms
#define PLATFORM_WinAPI             1
#define PLATFORM_AppKit             2
#define PLATFORM_X11                3
#define PLATFORM_Wayland            4
#define PLATFORM_Type               _CONCAT(PLATFORM_, PLATFORM)


// exported to Lua
extern int neo_start(lua_State* L);
extern int neo_stop(lua_State* L);
extern int neo_status(lua_State* L);
extern int neo_get(lua_State* L);
extern int neo_set(lua_State* L);


// neo_common.c
extern int neo_type(int ch);
extern void neo_split(lua_State* L, int ix, const void* data, size_t cb, int type);
extern void neo_join(lua_State* L, int ix, const char* sep);
extern int neo_nil(lua_State* L);
extern int neo_true(lua_State* L);
extern int neo_id(lua_State* L);
extern int neo_vimg(lua_State* L, const char* var, int dflt);


#endif // NEOCLIP_H
