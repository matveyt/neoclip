/*
 * neoclip - Neovim clipboard provider
 * Last Change:  2024 Aug 20
 * License:      https://unlicense.org
 * URL:          https://github.com/matveyt/neoclip
 */


#include "neoclip_nix.h"


// forward prototypes
static int neo_stop(lua_State* L);
static int neo_status(lua_State* L);
static int neo_get(lua_State* L);
static int neo_set(lua_State* L);


// module registration
__attribute__((visibility("default")))
int luaopen_driver(lua_State* L)
{
    static struct luaL_Reg const iface[] = {
        { "id", neo_id },
        { "start", neo_start },
        { "stop", neo_stop },
        { "status", neo_status },
        { "get", neo_get },
        { "set", neo_set },
        { NULL, NULL }
    };

#if defined(luaL_newlibtable)
    luaL_newlibtable(L, iface);
#else
    lua_createtable(L, 0, sizeof(iface) / sizeof(iface[0]) - 1);
#endif

    lua_pushvalue(L, 1);    // upvalue 1 : module name
    lua_newtable(L);        // upvalue 2 : shared table

#if defined(luaL_newlibtable)
    luaL_setfuncs(L, iface, 2);
#else
    luaL_openlib(L, NULL, iface, 2);
#endif
    return 1;
}


// invalidate state
static int neo_stop(lua_State* L)
{
    // uv_share.x = nil
    lua_pushnil(L);
    lua_setfield(L, uv_share, "x");
    // collectgarbage()
    lua_gc(L, LUA_GCCOLLECT, 0);

    lua_pushnil(L);
    return 1;
}


// get status
static int neo_status(lua_State* L)
{
    lua_pushboolean(L, neo_x(L) != NULL);
    return 1;
}


// get(regname) => [lines, regtype]
static int neo_get(lua_State* L)
{
    luaL_checktype(L, 1, LUA_TSTRING);  // regname
    int sel = (*lua_tostring(L, 1) == '*') ? sel_prim : sel_clip;

    // a table to return
    lua_createtable(L, 2, 0);
    neo_fetch(L, -1, sel);

    // always return table (empty on error)
    return 1;
}


// set(regname, lines, regtype) => boolean
static int neo_set(lua_State* L)
{
    luaL_checktype(L, 1, LUA_TSTRING);  // regname
    luaL_checktype(L, 2, LUA_TTABLE);   // lines
    luaL_checktype(L, 3, LUA_TSTRING);  // regtype
    int sel = (*lua_tostring(L, 1) == '*') ? sel_prim : sel_clip;
    int type = neo_type(*lua_tostring(L, 3));

    neo_X* x = neo_x(L);
    if (x != NULL) {
        // change selection data
        neo_join(L, 2, "\n");

        size_t cb;
        const char* ptr = lua_tolstring(L, -1, &cb);
        neo_own(x, true, sel, ptr, cb, type);
    }

    lua_pushboolean(L, x != NULL);
    return 1;
}
