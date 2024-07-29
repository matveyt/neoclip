/*
 * neoclip - Neovim clipboard provider
 * Last Change:  2024 Jul 27
 * License:      https://unlicense.org
 * URL:          https://github.com/matveyt/neoclip
 */


#include "neoclip.h"
#include "neoclip_nix.h"


// global context
static void* X = NULL;


// module registration
__attribute__((visibility("default")))
int luaopen_driver(lua_State* L)
{
    static struct luaL_Reg const methods[] = {
        { "id", neo_id },
        { "start", neo_start },
        { "stop", neo_stop },
        { "status", neo_status },
        { "get", neo_get },
        { "set", neo_set },
        { NULL, NULL }
    };

    // setup ID from module name
    lua_pushcfunction(L, neo_id);
    lua_pushvalue(L, 1);
    lua_call(L, 1, 0);

#if defined(luaL_newlib)
    luaL_newlib(L, methods);
#else
    lua_createtable(L, 0, sizeof(methods) / sizeof(methods[0]) - 1);
    luaL_register(L, NULL, methods);
#endif
    return 1;
}


// library cleanup
__attribute__((destructor))
void luaclose_driver(void)
{
    neo_kill(X);
}


// run X thread
int neo_start(lua_State* L)
{
    if (X == NULL) {
        static int first_run = 1;
        int targets_atom = neo_vimg(L, "neoclip_targets_atom", 1);
        const char* err;

        X = neo_create(first_run, targets_atom, &err);
        first_run = 0;

        if (X == NULL)
            return luaL_error(L, err);
    }

    lua_pushnil(L);
    return 1;
}


// stop X thread
int neo_stop(lua_State* L)
{
    neo_kill(X);
    X = NULL;
    lua_pushnil(L);
    return 1;
}


// is X thread running?
int neo_status(lua_State* L)
{
    lua_pushboolean(L, X != NULL);
    return 1;
}


// get selection
// neoclip.get(regname) -> [lines, regtype]
int neo_get(lua_State* L)
{
    luaL_checktype(L, 1, LUA_TSTRING);  // regname
    int sel = *lua_tostring(L, 1) == '*' ? prim : clip;

    // table to return
    lua_newtable(L);

    // query selection data
    if (X != NULL) {
        size_t cb;
        int type;
        const void* buf = neo_fetch(X, sel, &cb, &type);

        if (buf != NULL) {
            neo_split(L, lua_gettop(L), buf, cb, type);
            neo_lock(X, 0);
        }
    }

    // always return table (empty on error)
    return 1;
}


// set selection
// neoclip.set(regname, lines, regtype) -> boolean
int neo_set(lua_State* L)
{
    luaL_checktype(L, 1, LUA_TSTRING);  // regname
    luaL_checktype(L, 2, LUA_TTABLE);   // lines
    luaL_checktype(L, 3, LUA_TSTRING);  // regtype
    int sel = *lua_tostring(L, 1) == '*' ? prim : clip;
    int type = neo_type(*lua_tostring(L, 3));

    // change selection data
    if (X != NULL) {
        neo_join(L, 2, "\n");

        // get user data
        size_t cb;
        const char* ptr = lua_tolstring(L, -1, &cb);

        // owned
        neo_own(X, 1, sel, ptr, cb, type);
        lua_pop(L, 1);
    }

    lua_pushboolean(L, X != NULL);
    return 1;
}
