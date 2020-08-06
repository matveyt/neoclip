/*
 * neoclip - Neovim clipboard provider
 * Last Change:  2020 Aug 06
 * License:      https://unlicense.org
 * URL:          https://github.com/matveyt/neoclip
 */


#include "neoclip.h"
#include "neo_x.h"


static void* X = NULL;


// module registration for Lua 5.1
__attribute__((visibility("default")))
int luaopen_neoclip_x11(lua_State* L)
{
    // start X thread
    X = neo_X_start();
    if (X == NULL)
        return 0;

    // register module
    static struct luaL_Reg const methods[] = {
        { "get", neo_get },
        { "set", neo_set },
        { NULL, NULL }
    };

    luaL_register(L, "neoclip", methods);
    return 1;
}


// library cleanup
__attribute__((destructor))
void luaclose_neoclip_x11(void)
{
    if (X != NULL) {
        neo_X_send(X, proto, dele);
        neo_X_join(X);
        neo_X_cleanup(X);
        X = NULL;
    }
}


// get selection
// neoclip.get(regname) -> [lines, regtype]
int neo_get(lua_State* L)
{
    luaL_checktype(L, 1, LUA_TSTRING);  // regname
    int sel = *lua_tostring(L, 1) != '*' ? clip : prim;

    // a table to return
    lua_newtable(L);

    // get selection data
    size_t cb;
    const void* buf = neo_X_update(X, sel, &cb);

    if (buf != NULL) {
        // split lines
        int line = neo_split(L, buf, cb);
        lua_rawseti(L, -2, 1);
        // push regtype
        lua_pushlstring(L, line ? "V" : "v", sizeof(char));
        lua_rawseti(L, -2, 2);
        // unlock context
        neo_X_unlock(X);
    }

    // always return a table (empty on error)
    return 1;
}


// set selection
// neoclip.set(regname, lines, regtype) -> boolean
int neo_set(lua_State* L)
{
    luaL_checktype(L, 1, LUA_TSTRING);  // regname
    luaL_checktype(L, 2, LUA_TTABLE);   // lines
    luaL_checktype(L, 3, LUA_TSTRING);  // regtype (unused)
    int sel = *lua_tostring(L, 1) != '*' ? clip : prim;

    // convert to string
    lua_pushvalue(L, 2);
    neo_join(L, "\n");

    // get user data
    size_t cb;
    const char* ptr = lua_tolstring(L, -1, &cb);

    // owned
    neo_X_ready(X, sel, ptr, cb);
    neo_X_send(X, neo_owned, sel);

    lua_pop(L, 1);
    lua_pushboolean(L, 1);
    return 1;
}
