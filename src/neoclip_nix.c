/*
 * neoclip - Neovim clipboard provider
 * Last Change:  2024 May 30
 * License:      https://unlicense.org
 * URL:          https://github.com/matveyt/neoclip
 */


#include "neoclip.h"
#include "neoclip_nix.h"


// global context
static void* X = NULL;


// module registration for Lua 5.1
__attribute__((visibility("default")))
#if (PLATFORM_Type == PLATFORM_X11)
int luaopen_neoclip_x11(lua_State* L)
#elif (PLATFORM_Type == PLATFORM_Wayland)
int luaopen_neoclip_wl(lua_State* L)
#endif
{
#if (PLATFORM_Type == PLATFORM_X11)
    if (!neo_xinit(neo_vimg(L, "neoclip_targets_atom", 1)))
        return luaL_error(L, "XInitThreads failed");
#endif

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
#if (PLATFORM_Type == PLATFORM_X11)
void luaclose_neoclip_x11(void)
#elif (PLATFORM_Type == PLATFORM_Wayland)
void luaclose_neoclip_wl(void)
#endif
{
    neo_kill(X);
}


// module ID
int neo_id(lua_State* L)
{
    lua_pushliteral(L, "neoclip/" _STRINGIZE(PLATFORM));
    return 1;
}


// run X thread
int neo_start(lua_State* L)
{
    if (X == NULL)
        X = neo_create();

    if (lua_isboolean(L, 1))
        lua_pushboolean(L, X != NULL);
    else
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
