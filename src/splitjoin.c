/*
 * neoclip - Neovim clipboard provider
 * Last Change:  2020 Jul 23
 * License:      https://unlicense.org
 * URL:          https://github.com/matveyt/neoclip
 */


#include "neoclip.h"
#include <stdint.h>


// split UTF-8 string into lines (LF or CRLF)
// returns table on Lua stack
// returns 1 iff data ends with the newline
int neo_split(lua_State* L, const void* data, size_t cb)
{
    // pb points to the start of the current line
    const uint8_t* pb = data;
    // off + rest = length of the text remained
    size_t off = 0, rest = cb;
    // idx is Lua table index (one-based)
    int idx = 1;
    // state is: -1 after CR; 0 normal; 1, 2, 3 skip continuation octets
    int state = 0;

    // a table to return
    lua_newtable(L);

    do {
        // get next octet
        int c = pb[off];
        ++off; --rest;

        if (state > 0) {    // skip continuation octets
            if (c < 0x80 || c >= 0xc0)
                break;  // not a continuation octet
            --state;
        } else if (c == 10) {   // LF or CRLF
            // push current line
            lua_pushlstring(L, (const char*)pb, off - 1 - (state < 0));
            lua_rawseti(L, -2, idx++);
            // adjust pb and off
            pb += off;
            off = state = 0;
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
    } while (rest);

    // push last string
    lua_pushlstring(L, (const char*)pb, off + rest);
    lua_rawseti(L, -2, idx);

    // did it end with the newline?
    return off ? 0 : 1;
}


// invoke table.concat
// returns string on Lua stack
void neo_join(lua_State* L, const char* sep)
{
    // top = table.concat(top, sep)
    lua_getglobal(L, "table");
    lua_getfield(L, -1, "concat");
    lua_remove(L, -2);
    lua_insert(L, -2);
    lua_pushstring(L, sep);
    lua_call(L, 2, 1);
}
