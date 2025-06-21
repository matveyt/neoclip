/*
 * neoclip - Neovim clipboard provider
 * Last Change:  2025 Jun 21
 * License:      https://unlicense.org
 * URL:          https://github.com/matveyt/neoclip
 */


#if !defined(NEOCLIP_NIX_H)
#define NEOCLIP_NIX_H

#if !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200112L
#endif // _POSIX_C_SOURCE

#if !defined(WITH_THREADS) && !defined(WITH_LUV)
#define WITH_LUV
#elif defined(WITH_THREADS) && defined(WITH_LUV)
#undef WITH_THREADS
#endif // WITH_LUV

#include "neoclip.h"


// selection index
enum {
    sel_prim,
    sel_sec,
    sel_clip,
    sel_total
};

// driver state : incomplete type
typedef struct neo_X neo_X;

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
