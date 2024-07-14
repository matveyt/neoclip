/*
 * neoclip - Neovim clipboard provider
 * Last Change:  2024 Jul 14
 * License:      https://unlicense.org
 * URL:          https://github.com/matveyt/neoclip
 */


#include "neoclip.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>


// convert v/V/^V to MCHAR/MLINE/MBLOCK
__attribute__((const))
int neo_type(int ch)
{
    switch (ch) {
    case 'c':
    case 'v':
        return 0;   // MCHAR
    case 'l':
    case 'V':
        return 1;   // MLINE
    case 'b':
    case '\026':
        return 2;   // MBLOCK
    default:
        return 255; // MAUTO
    }
}


// split UTF-8 string into lines (LF or CRLF) and save in table [lines, regtype]
// chop invalid data, e.g. trailing zero in Windows Clipboard
void neo_split(lua_State* L, int ix, const void* data, size_t cb, int type)
{
    // validate input
    luaL_checktype(L, ix, LUA_TTABLE);
    if (data == NULL || cb < 1)
        return;

    // pb points to start of line
    const uint8_t* pb = data;
    // off + rest = size of remaining text
    size_t off = 0, rest = cb;
    // i is Lua table index (one-based)
    int i = 1;
    // state: -1 after CR; 0 normal; 1, 2, 3 skip continuation octets
    int state = 0;

    // lines table
    lua_newtable(L);

    do {
        int c = pb[off];        // get next octet

        if (state > 0) {        // skip continuation octet(s)
            if (c < 0x80 || c >= 0xc0)
                break;          // non-continuation octet
            --state;
        } else if (c == 0) {    // NUL
            break;
        } else if (c == 10) {   // LF or CRLF
            // push current line
            lua_pushlstring(L, (const char*)pb, off - (state < 0));
            lua_rawseti(L, -2, i++);
            // adjust pb and off
            pb += off + 1;
            off = state = 0;
            continue;           // don't increment off
        } else if (c == 13) {   // have CR
            state = -1;
        } else if (c < 0x80) {  // 7 bits code
            state = 0;
        } else if (c < 0xc0) {  // unexpected continuation octet
            break;
        } else if (c < 0xe0) {  // 11 bits code
            state = 1;
        } else if (c < 0xf0) {  // 16 bits code
            state = 2;
        } else if (c < 0xf8) {  // 21 bits code
            state = 3;
        } else  // bad octet
            break;

        ++off;
    } while (--rest);

    // push last string w/o invalid rest
    lua_pushlstring(L, (const char*)pb, off/* + rest*/);
    lua_rawseti(L, -2, i);

    // save result
    lua_rawseti(L, ix, 1);
    lua_pushlstring(L, type == 0 ? "v" : type == 1 ? "V" : type == 2 ? "\026" :
        off ? "v" : "V" , sizeof(char));
    lua_rawseti(L, ix, 2);
}


// table concatenation (numeric indices only)
// return string on Lua stack
void neo_join(lua_State* L, int ix, const char* sep)
{
    luaL_Buffer b;
    luaL_buffinit(L, &b);

    int n = lua_objlen(L, ix);
    if (n > 0) {
        for (int i = 1; i < n; ++i) {
            lua_rawgeti(L, ix, i);
            luaL_addvalue(&b);
            luaL_addstring(&b, sep);
        }
        lua_rawgeti(L, ix, n);
        luaL_addvalue(&b);
    }

    luaL_pushresult(&b);
}


// return nil
int neo_nil(lua_State* L)
{
    lua_pushnil(L);
    return 1;
}


// return true
int neo_true(lua_State* L)
{
    lua_pushboolean(L, 1);
    return 1;
}


// get ID string
int neo_id(lua_State* L)
{
    static char* platform = NULL;

    // set ID
    if (lua_isstring(L, 1)) {
        free(platform);

        // name = string.match(arg[1], "%.?(%w+)-")
        lua_getglobal(L, "string");
        lua_getfield(L, -1, "match");
        lua_pushvalue(L, 1);
        lua_pushliteral(L, "%.?(%w+)-");
        lua_call(L, 2, 1);
        const char* name = lua_tostring(L, -1);

        if (name == NULL) {
            platform = NULL;
        } else {
            // translate name
            if (!strcmp(name, "w32"))
                name = "WinAPI";
            else if (!strcmp(name, "mac"))
                name = "AppKit";
            else if (!strcmp(name, "wl"))
                name = "Wayland";
            else if (!strcmp(name, "x11"))
                name = "X11";

            platform = strdup(name);
        }

        lua_pop(L, 2);  // name + string global
    }

    lua_pushfstring(L, "neoclip/%s", platform ? platform : "Unknown");
    return 1;
}


// get vim.g[var] as integer
int neo_vimg(lua_State* L, const char* var, int dflt)
{
    lua_getglobal(L, "vim");
    lua_getfield(L, -1, "g");
    lua_getfield(L, -1, var);
    int value = lua_isnil(L, -1) ? dflt
        : lua_isboolean(L, -1) ? lua_toboolean(L, -1)
        : lua_tointeger(L, -1);
    lua_pop(L, 3);

    return value;
}
