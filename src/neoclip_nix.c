/*
 * neoclip - Neovim clipboard provider
 * Last Change:  2024 Aug 08
 * License:      https://unlicense.org
 * URL:          https://github.com/matveyt/neoclip
 */


#include "neoclip.h"
#include "neoclip_nix.h"


// userdata
typedef struct {
    int first_run;
    void* X;
} UD;


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

    lua_pushvalue(L, 1);                        // upvalue 1: module name
    UD* ud = lua_newuserdata(L, sizeof(UD));    // upvalue 2: userdata
    ud->first_run = 1;
    ud->X = NULL;
    // metatable for userdata
    luaL_newmetatable(L, lua_tostring(L, 1));
    lua_pushcfunction(L, neo__gc);
    lua_setfield(L, -2, "__gc");
    lua_setmetatable(L, -2);
#if defined(luaL_newlibtable)
    luaL_newlibtable(L, methods);
    lua_insert(L, -3);  // move table before upvalues
    luaL_setfuncs(L, methods, 2);
#else
    lua_createtable(L, 0, sizeof(methods) / sizeof(methods[0]) - 1);
    lua_insert(L, -3);  // move table before upvalues
    luaL_openlib(L, NULL, methods, 2);
#endif
    return 1;
}


// run X thread
int neo_start(lua_State* L)
{
    UD* ud = neo_ud(L);

    if (ud->X == NULL) {
        const char* err;
        ud->X = neo_create(ud->first_run, neo_vimg(L, "neoclip_targets_atom", 1),
            &err);

        if (ud->first_run)
            ud->first_run = 0;

        if (ud->X == NULL)
            return luaL_error(L, err);
    }

    return neo_nil(L);
}


// stop X thread
int neo_stop(lua_State* L)
{
    UD* ud = neo_ud(L);
    neo_kill(ud->X);
    ud->X = NULL;
    return neo_nil(L);
}


// garbage collect: also stop X thread
int neo__gc(lua_State* L)
{
    UD* ud = lua_touserdata(L, 1);
    neo_kill(ud->X);
    return 0;
}


// is X thread running?
int neo_status(lua_State* L)
{
    lua_pushboolean(L, neo_ud(L)->X != NULL);
    return 1;
}


// get selection
// neoclip.get(regname) -> [lines, regtype]
int neo_get(lua_State* L)
{
    luaL_checktype(L, 1, LUA_TSTRING);  // regname
    int sel = *lua_tostring(L, 1) == '*' ? prim : clip;
    UD* ud = neo_ud(L);

    // a table to return
    lua_newtable(L);

    // query selection data
    if (ud->X != NULL) {
        size_t cb;
        int type;
        const void* buf = neo_fetch(ud->X, sel, &cb, &type);

        if (buf != NULL) {
            neo_split(L, lua_gettop(L), buf, cb, type);
            neo_lock(ud->X, 0);
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
    UD* ud = neo_ud(L);

    // change selection data
    if (ud->X != NULL) {
        neo_join(L, 2, "\n");
        // get user data
        size_t cb;
        const char* ptr = lua_tolstring(L, -1, &cb);
        // owned
        neo_own(ud->X, 1, sel, ptr, cb, type);
    }

    lua_pushboolean(L, ud->X != NULL);
    return 1;
}
