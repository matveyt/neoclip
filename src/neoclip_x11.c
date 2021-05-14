/*
 * neoclip - Neovim clipboard provider
 * Last Change:  2021 May 14
 * License:      https://unlicense.org
 * URL:          https://github.com/matveyt/neoclip
 */


#include "neoclip.h"
#include "neo_x.h"


// global context
static void* X = NULL;


// module registration for Lua 5.1
__attribute__((visibility("default")))
int luaopen_neoclip_x11(lua_State* L)
{
    static struct luaL_Reg const methods[] = {
        { "id", neo_id },
        { "start", neo_start },
        { "stop", neo_stop },
        { "get", neo_get },
        { "set", neo_set },
        { NULL, NULL }
    };
    lua_newtable(L);
    luaL_register(L, NULL, methods);
    return 1;
}


// library cleanup
__attribute__((destructor))
void luaclose_neoclip_x11(void)
{
    neo_stop(NULL);
}


// module ID
int neo_id(lua_State* L)
{
    lua_pushliteral(L, "neoclip/X11");
    return 1;
}


// run X thread
int neo_start(lua_State* L)
{
    if (X == NULL)
        X = neo_X_start();

    lua_pushboolean(L, X != NULL);
    return 1;
}


// stop X thread
int neo_stop(lua_State* L)
{
    if (X != NULL) {
        neo_X_send(X, proto, dele);
        neo_X_cleanup(X);
        X = NULL;
    }

    (void)L;    // unused
    return 0;
}


// get selection
// neoclip.get(regname) -> [lines, regtype]
int neo_get(lua_State* L)
{
    luaL_checktype(L, 1, LUA_TSTRING);  // regname
    int sel = *lua_tostring(L, 1) != '*' ? clip : prim;

    // table to return
    lua_newtable(L);

    // query selection data
    if (X != NULL) {
        size_t cb;
        int type;
        const void* buf = neo_X_update(X, sel, &cb, &type);

        if (buf != NULL) {
            neo_split(L, lua_gettop(L), buf, cb, type);
            neo_X_lock(X, 0);
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
    int sel = *lua_tostring(L, 1) != '*' ? clip : prim;
    int type = neo_type(*lua_tostring(L, 3));

    // change selection data
    if (X != NULL) {
        neo_join(L, 2, "\n");

        // get user data
        size_t cb;
        const char* ptr = lua_tolstring(L, -1, &cb);

        // owned
        neo_X_ready(X, sel, ptr, cb, type);
        neo_X_send(X, neo_owned, sel);

        lua_pop(L, 1);
    }

    lua_pushboolean(L, X != NULL);
    return 1;
}
