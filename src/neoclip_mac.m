/*
 * neoclip - Neovim clipboard provider
 * Last Change:  2021 Mar 09
 * License:      https://unlicense.org
 * URL:          https://github.com/matveyt/neoclip
 */


#include "neoclip.h"
#import <AppKit/AppKit.h>


// module registration for Lua 5.1
__declspec(dllexport)
int luaopen_neoclip_mac(lua_State* L)
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


// module ID
int neo_id(lua_State* L)
{
    lua_pushliteral(L, "neoclip/AppKit");
    return 1;
}


// no-op
int neo_start(lua_State* L)
{
    lua_pushboolean(L, 1);
    return 1;
}


// no-op
int neo_stop(lua_State* L)
{
    (void)L;    // unused
    return 0;
}


// get selection
// neoclip.get(regname) -> [lines, regtype]
int neo_get(lua_State* L)
{
    luaL_checktype(L, 1, LUA_TSTRING);  // regname (unused)

    // a table to return
    lua_newtable(L);

    // read NSString from generalPasteboard
    NSPasteboard* pb = [NSPasteboard generalPasteboard];
    NSString* str = [pb stringForType:NSPasteboardTypeString];

    // convert NSString to UTF-8
    NSData* buf = [str dataUsingEncoding:NSUTF8StringEncoding];

    if (buf.length > 0) {
        // split lines
        int line = neo_split(L, buf.bytes, buf.length);
        lua_rawseti(L, -2, 1);
        // push regtype
        lua_pushlstring(L, line ? "V" : "v", sizeof(char));
        lua_rawseti(L, -2, 2);
    }

    // always return a table (empty on error)
    return 1;
}


// set selection
// neoclip.set(regname, lines, regtype) -> boolean
int neo_set(lua_State* L)
{
    luaL_checktype(L, 1, LUA_TSTRING);  // regname (unused)
    luaL_checktype(L, 2, LUA_TTABLE);   // lines
    luaL_checktype(L, 3, LUA_TSTRING);  // regtype (unused)

    // convert to string
    lua_pushvalue(L, 2);
    neo_join(L, "\n");

    // get UTF-8
    size_t cb;
    const char* ptr = lua_tolstring(L, -1, &cb);

    // convert UTF-8 to NSString
    NSString* str = [[NSString alloc] initWithBytes:ptr length:cb
        encoding:NSUTF8StringEncoding];

    // write NSString to generalPasteboard
    NSPasteboard* pb = [NSPasteboard generalPasteboard];
    [pb declareTypes:[NSArray arrayWithObject:NSPasteboardTypeString] owner:nil];
    BOOL ok = [pb setString:str forType:NSPasteboardTypeString];

    // cleanup
    [str release];
    lua_pop(L, 1);

    lua_pushboolean(L, ok);
    return 1;
}
