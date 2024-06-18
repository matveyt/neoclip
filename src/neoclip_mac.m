/*
 * neoclip - Neovim clipboard provider
 * Last Change:  2024 Jun 16
 * License:      https://unlicense.org
 * URL:          https://github.com/matveyt/neoclip
 */


#include "neoclip.h"
#import <AppKit/AppKit.h>


// Vim compatible format
static NSString* VimPboardType = @"VimPboardType";


// module registration for Lua 5.1
__attribute__((visibility("default")))
int luaopen_neoclip_mac(lua_State* L)
{
    static struct luaL_Reg const methods[] = {
        { "id", neo_id },
        { "start", neo_nil },
        { "stop", neo_nil },
        { "status", neo_true },
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


// get selection
// neoclip.get(regname) -> [lines, regtype]
int neo_get(lua_State* L)
{
    luaL_checktype(L, 1, LUA_TSTRING);  // regname (unused)

    // table to return
    lua_newtable(L);

    // check supported types
    NSPasteboard* pb = [NSPasteboard generalPasteboard];
    NSString* bestType = [pb availableTypeFromArray:[NSArray
        arrayWithObjects:VimPboardType, NSPasteboardTypeString, nil]];

    if (bestType) {
        NSString* str = nil;
        int type = 255; // MAUTO

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
            neo_split(L, lua_gettop(L), buf.bytes, buf.length, type);
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
    BOOL success = [pb setString:str forType:NSPasteboardTypeString]
        && [pb setPropertyList:[NSArray arrayWithObjects:[NSNumber numberWithInt:type],
            str, nil] forType:VimPboardType];

    // cleanup
    [str release];
    lua_pop(L, 1);

    lua_pushboolean(L, success);
    return 1;
}
