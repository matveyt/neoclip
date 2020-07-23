/*
 * neoclip - Neovim clipboard provider
 * Last Change:  2020 Jul 23
 * License:      https://unlicense.org
 * URL:          https://github.com/matveyt/neoclip
 */


#include "neoclip.h"
#include <windef.h>
#include <winbase.h>
#include <winnls.h>
#include <winuser.h>


// module registration for Lua 5.1
__declspec(dllexport)
int luaopen_neoclip_w32(lua_State* L)
{
    static struct luaL_Reg const methods[] = {
        { "getstar", neo_get },
        { "getplus", neo_get },
        { "setstar", neo_set },
        { "setplus", neo_set },
        { NULL, NULL }
    };
    luaL_register(L, "neoclip", methods);
    return 1;
}


// neoclip.get() -> [lines, regtype]
// read from the system clipboard
int neo_get(lua_State* L)
{
    // a table to return
    lua_newtable(L);

    if (IsClipboardFormatAvailable(CF_UNICODETEXT) && OpenClipboard(NULL)) {
        // read UTF-16 from clipboard
        HGLOBAL hMem = GetClipboardData(CF_UNICODETEXT);
        LPCWSTR lpSrc = GlobalLock(hMem);
        size_t cchSrc = lstrlenW(lpSrc);

        // allocate buffer for UTF-8
        size_t cchDst = WideCharToMultiByte(CP_UTF8, 0, lpSrc, (int)cchSrc, NULL, 0,
            NULL, NULL);
        LPSTR lpDst = (LPSTR)LocalAlloc(LMEM_FIXED, cchDst + 1);

        // convert UTF-16 to UTF-8
        cchDst = WideCharToMultiByte(CP_UTF8, 0, lpSrc, (int)cchSrc, lpDst, (int)cchDst,
            NULL, NULL);
        GlobalUnlock(hMem);
        CloseClipboard();

        // store result
        if (cchDst) {
            // split lines
            int line = neo_split(L, lpDst, cchDst);
            lua_rawseti(L, -2, 1);
            // push regtype
            lua_pushlstring(L, line ? "V" : "v", sizeof(char));
            lua_rawseti(L, -2, 2);
        }

        // free UTF-8 buffer
        LocalFree(lpDst);
    }

    // always return a table (empty on error)
    return 1;
}


// neoclip.set([lines, regtype]) -> boolean
// write to the system clipboard
int neo_set(lua_State* L)
{
    luaL_checktype(L, 1, LUA_TTABLE);   // [lines, regtype]
    size_t cchDst = 0;

    if (OpenClipboard(NULL)) {
        EmptyClipboard();

        // join lines
        lua_rawgeti(L, 1, 1);
        neo_join(L, "\r\n");

        // get UTF-8
        size_t cchSrc;
        LPCSTR lpSrc = lua_tolstring(L, -1, &cchSrc); ++cchSrc;

        // allocate buffer for UTF-16
        cchDst = (size_t)MultiByteToWideChar(CP_UTF8, 0, lpSrc, (int)cchSrc, NULL, 0);
        HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, sizeof(WCHAR) * cchDst + 1);

        // convert UTF-8 to UTF-16
        cchDst = (size_t)MultiByteToWideChar(CP_UTF8, 0, lpSrc, (int)cchSrc,
            GlobalLock(hMem), (int)cchDst);
        GlobalUnlock(hMem);
        lua_pop(L, 1);

        // save UTF-16 in the clipboard
        SetClipboardData(CF_UNICODETEXT, hMem);
        CloseClipboard();
    }

    lua_pushboolean(L, cchDst ? 1 : 0);
    return 1;
}
