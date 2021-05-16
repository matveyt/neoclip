/*
 * neoclip - Neovim clipboard provider
 * Last Change:  2021 May 15
 * License:      https://unlicense.org
 * URL:          https://github.com/matveyt/neoclip
 */


#include "neoclip.h"
#include "neo_w.h"


// global context
static void* W;


// module registration for Lua 5.1
__attribute__((visibility("default")))
int luaopen_neoclip_wl(lua_State* L)
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
void luaclose_neoclip_wl(void)
{
    neo_stop(NULL);
}


// module ID
int neo_id(lua_State* L)
{
    lua_pushliteral(L, "neoclip/Wayland");
    return 1;
}


// init Wayland connection
int neo_start(lua_State* L)
{
    if (W == NULL)
        W = neo_W_start();

    lua_pushboolean(L, W != NULL);
    return 1;
}


// close Wayland connection
int neo_stop(lua_State* L)
{
    (void)L;    // unused

    if (W != NULL) {
        neo_W_cleanup(W);
        W = NULL;
    }

    return 0;
}


// get selection
// neoclip.get(regname) -> [lines, regtype]
int neo_get(lua_State* L)
{
    luaL_checktype(L, 1, LUA_TSTRING);  // regname (unused)

    // table to return
    lua_newtable(L);

    // query selection data
    if (W != NULL) {
        size_t cb;
        int type;
        const void* buf = neo_W_update(W, &cb, &type);

        if (buf != NULL) {
            neo_split(L, lua_gettop(L), buf, cb, type);
            neo_W_lock(W, 0);
        }
    }

    // always return table (empty on error)
    return 1;
}


// set selection
// neoclip.set(regname, lines, regtype) -> boolean
int neo_set(lua_State* L)
{
    luaL_checktype(L, 1, LUA_TSTRING);  // regname (unused)
    luaL_checktype(L, 2, LUA_TTABLE);   // lines
    luaL_checktype(L, 3, LUA_TSTRING);  // regtype
    int type = neo_type(*lua_tostring(L, 3));

    // change selection data
    if (W != NULL) {
        neo_join(L, 2, "\n");

        // get user data
        size_t cb;
        const char* ptr = lua_tolstring(L, -1, &cb);

        // offer new selection
        neo_W_offer(W, ptr, cb, type);

        lua_pop(L, 1);
    }

    lua_pushboolean(L, W != NULL);
    return 1;
}
