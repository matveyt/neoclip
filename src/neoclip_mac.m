/*
 * neoclip - Neovim clipboard provider
 * Last Change:  2024 Aug 21
 * License:      https://unlicense.org
 * URL:          https://github.com/matveyt/neoclip
 */


#include "neoclip.h"
#import <AppKit/AppKit.h>


// Vim compatible format
static NSString* VimPboardType = @"VimPboardType";


// forward prototypes
static int neo_nil(lua_State* L);
static int neo_true(lua_State* L);
static int neo_get(lua_State* L);
static int neo_set(lua_State* L);


// module registration
__attribute__((visibility("default")))
int luaopen_driver(lua_State* L)
{
    static struct luaL_Reg const iface[] = {
        { "id", neo_id },
        { "start", neo_nil },
        { "stop", neo_nil },
        { "status", neo_true },
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

#if defined(luaL_newlibtable)
    luaL_setfuncs(L, iface, 1);
#else
    luaL_openlib(L, NULL, iface, 1);
#endif
    return 1;
}


// lua_CFunction() => nil
static int neo_nil(lua_State* L)
{
    lua_pushnil(L);
    return 1;
}


// lua_CFunction() => true
static int neo_true(lua_State* L)
{
    lua_pushboolean(L, true);
    return 1;
}


// get(regname) => [lines, regtype]
static int neo_get(lua_State* L)
{
    luaL_checktype(L, 1, LUA_TSTRING);  // regname (unused)

    // a table to return
    lua_createtable(L, 2, 0);

    // check supported types
    NSPasteboard* pb = [NSPasteboard generalPasteboard];
    NSString* bestType = [pb availableTypeFromArray:[NSArray
        arrayWithObjects:VimPboardType, NSPasteboardTypeString, nil]];

    if (bestType) {
        NSString* str = nil;
        int type = MAUTO;

        // VimPboardType is [NSArray arrayWithObjects:[NSNumber], [NSString]]
        if ([bestType isEqual:VimPboardType]) {
            id plist = [pb propertyListForType:VimPboardType];
            if ([plist isKindOfClass:[NSArray class]] && [plist count] == 2) {
                type = [[plist objectAtIndex:0] intValue];
                str = [plist objectAtIndex:1];
            }
        }

        // fallback to NSPasteboardTypeString
        if (str == nil)
            str = [pb stringForType:NSPasteboardTypeString];

        // convert to UTF-8 and split into table
        NSData* buf = [str dataUsingEncoding:NSUTF8StringEncoding];
        if (buf.length > 0)
            neo_split(L, -1, buf.bytes, buf.length, type);
    }

    // always return table (empty on error)
    return 1;
}


// set(regname, lines, regtype) => boolean
static int neo_set(lua_State* L)
{
    luaL_checktype(L, 1, LUA_TSTRING);  // regname (unused)
    luaL_checktype(L, 2, LUA_TTABLE);   // lines
    luaL_checktype(L, 3, LUA_TSTRING);  // regtype
    int type = neo_type(*lua_tostring(L, 3));

    // table to string
    neo_join(L, 2, "\n");

    // get UTF-8
    size_t cb;
    const char* ptr = lua_tolstring(L, -1, &cb);

    // convert UTF-8 to NSString
    NSString* str = [[NSString alloc] initWithBytes:ptr length:cb
        encoding:NSUTF8StringEncoding];

    // set both VimPboardType and NSPasteboardTypeString
    NSPasteboard* pb = [NSPasteboard generalPasteboard];
    [pb declareTypes:[NSArray arrayWithObjects:VimPboardType, NSPasteboardTypeString,
        nil] owner:nil];
    bool success = [pb setString:str forType:NSPasteboardTypeString]
        && [pb setPropertyList:[NSArray arrayWithObjects:[NSNumber numberWithInt:type],
            str, nil] forType:VimPboardType];

    // cleanup
    [str release];

    lua_pushboolean(L, success);
    return 1;
}
