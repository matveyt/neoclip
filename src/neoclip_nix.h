/*
 * neoclip - Neovim clipboard provider
 * Last Change:  2024 Aug 20
 * License:      https://unlicense.org
 * URL:          https://github.com/matveyt/neoclip
 */


#ifndef NEOCLIP_NIX_H
#define NEOCLIP_NIX_H

#if !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200112L
#endif // _POSIX_C_SOURCE
#include "neoclip.h"


// selection index
enum {
    sel_prim,
    sel_clip,
    sel_total
};

// incomplete type
typedef struct neo_X neo_X;

int neo_start(lua_State* L);
void neo_fetch(lua_State* L, int ix, int sel);
void neo_own(neo_X* x, bool offer, int sel, const void* ptr, size_t cb, int type);

// inline helper
static inline neo_X* neo_x(lua_State* L)
{
    luaL_checktype(L, uv_share, LUA_TTABLE);
    lua_getfield(L, uv_share, "x");
    neo_X* x = (neo_X*)neo_ud(L, -1);
    lua_pop(L, 1);
    return x;
}


#endif // NEOCLIP_NIX_H
