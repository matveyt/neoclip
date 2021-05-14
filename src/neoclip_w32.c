/*
 * neoclip - Neovim clipboard provider
 * Last Change:  2021 May 14
 * License:      https://unlicense.org
 * URL:          https://github.com/matveyt/neoclip
 */


#include "neoclip.h"
#include <windef.h>
#include <winbase.h>
#include <winnls.h>
#include <winuser.h>


// forward prototypes
static HANDLE get_and_lock(UINT uFormat, LPVOID ppData, size_t* pcbMax);
static void unlock_and_set(UINT uFormat, HANDLE hData);
static HANDLE mb2wc(UINT cp, LPCVOID pSrc, size_t cchSrc, LPVOID ppDst, size_t* pcch);
static HANDLE wc2mb(UINT cp, LPCVOID pSrc, size_t cchSrc, LPVOID ppDst, size_t* pcch);


// Vim compatible clipboard format
static UINT g_uVimMeta, g_uVimRaw;


// module registration for Lua 5.1
__declspec(dllexport)
int luaopen_neoclip_w32(lua_State* L)
{
    static struct luaL_Reg const methods[] = {
        { "id", neo_id },
        { "start", neo_start },
        { "stop", neo_stop },
        { "get", neo_get },
        { "set", neo_set },
        { NULL, NULL }
    };

    g_uVimMeta = RegisterClipboardFormatW(L"VimClipboard2");
    g_uVimRaw = RegisterClipboardFormatW(L"VimRawBytes");

    lua_newtable(L);
    luaL_register(L, NULL, methods);
    return 1;
}


// module ID
int neo_id(lua_State* L)
{
    lua_pushliteral(L, "neoclip/WinAPI");
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


// neoclip.get(regname) -> [lines, regtype]
// read from system clipboard
int neo_get(lua_State* L)
{
    luaL_checktype(L, 1, LUA_TSTRING);  // regname (unused)

    // table to return
    lua_newtable(L);
    if (!OpenClipboard(NULL))
        return 1;

    // get Vim meta
    int meta[4] = {
        255,        // type
        INT_MAX,    // ACP len
        INT_MAX,    // UCS len
        0           // Raw len
    };
    HANDLE hData, hBuf = NULL;
    LPVOID pBuf;
    size_t count = sizeof(meta);
    if ((hData = get_and_lock(g_uVimMeta, &pBuf, &count)) != NULL) {
        memcpy(meta, pBuf, count & ~(sizeof(int) - 1));
        GlobalUnlock(hData);
        hData = NULL;
    }

    do {
        // VimRawBytes
        if ((count = meta[3]) >= sizeof("utf-8")
            && (hData = get_and_lock(g_uVimRaw, &pBuf, &count)) != NULL) {
            if (count >= sizeof("utf-8") && !memcmp(pBuf, "utf-8", sizeof("utf-8"))) {
                *(LPSTR*)&pBuf += sizeof("utf-8");
                count -= sizeof("utf-8");
                break;
            }
            GlobalUnlock(hData);
            hData = NULL;
        }

        // CF_UNICODETEXT
        if ((count = meta[2] * sizeof(WCHAR)) > 0
            && (hData = get_and_lock(CF_UNICODETEXT, &pBuf, &count)) != NULL) {
            hBuf = wc2mb(CP_UTF8, pBuf, count / sizeof(WCHAR), &pBuf, &count);
            break;
        }

        // CF_TEXT
        if ((count = meta[1]) > 0
            && (hData = get_and_lock(CF_TEXT, &pBuf, &count)) != NULL) {
            HANDLE hTemp = mb2wc(CP_ACP, pBuf, count, &pBuf, &count);
            hBuf = wc2mb(CP_UTF8, pBuf, count, &pBuf, &count);
            GlobalUnlock(hTemp);
            GlobalFree(hTemp);
            break;
        }
    } while (0);

    if (hData != NULL) {
        neo_split(L, lua_gettop(L), pBuf, count, meta[0]);
        GlobalUnlock(hData);
        if (hBuf != NULL) {
            GlobalUnlock(hBuf);
            GlobalFree(hBuf);
        }
    }
    CloseClipboard();
    return 1;
}


// neoclip.set(regname, lines, regtype) -> boolean
// write to system clipboard
int neo_set(lua_State* L)
{
    luaL_checktype(L, 1, LUA_TSTRING);  // regname (unused)
    luaL_checktype(L, 2, LUA_TTABLE);   // lines
    luaL_checktype(L, 3, LUA_TSTRING);  // regtype

    BOOL bSuccess = OpenClipboard(NULL);

    if (bSuccess) {
        EmptyClipboard();
        neo_join(L, 2, "\r\n");

        // get UTF-8
        size_t cchSrc;
        LPCSTR pSrc = lua_tolstring(L, -1, &cchSrc); ++cchSrc;
        HANDLE hBuf;
        LPVOID pBuf;

        // CF_UNICODETEXT + CF_TEXT
        size_t cchUCS, cchACP;
        hBuf = mb2wc(CP_UTF8, pSrc, (int)cchSrc, &pBuf, &cchUCS);
        unlock_and_set(CF_TEXT, wc2mb(CP_ACP, pBuf, cchUCS, &(LPVOID){NULL}, &cchACP));
        unlock_and_set(CF_UNICODETEXT, hBuf);

        // VimRawBytes
        hBuf = GlobalAlloc(GMEM_MOVEABLE, sizeof("utf-8") + cchSrc);
        pBuf = GlobalLock(hBuf);
        memcpy(pBuf, "utf-8", sizeof("utf-8"));                 // NUL terminated
        memcpy((LPSTR)pBuf + sizeof("utf-8"), pSrc, cchSrc);    // NUL terminated
        unlock_and_set(g_uVimRaw, hBuf);

        // VimClipboard2
        hBuf = GlobalAlloc(GMEM_MOVEABLE, sizeof(int) * 4);
        int* pMeta = GlobalLock(hBuf);
        *pMeta++ = neo_type(*lua_tostring(L, 3));   // type
        *pMeta++ = cchACP ? cchACP - 1 : 0;         // txtlen
        *pMeta++ = cchUCS ? cchUCS - 1 : 0;         // ucslen
        *pMeta   = sizeof("utf-8") + cchSrc - 1;    // rawlen
        unlock_and_set(g_uVimMeta, hBuf);

        lua_pop(L, 1);
        CloseClipboard();
    }

    lua_pushboolean(L, bSuccess);
    return 1;
}


// safe get clipboard data
static HANDLE get_and_lock(UINT uFormat, LPVOID ppData, size_t* pcbMax)
{
    HANDLE hData = IsClipboardFormatAvailable(uFormat) ?
        GetClipboardData(uFormat) : NULL;

    if (hData != NULL) {
        *(LPVOID*)ppData = GlobalLock(hData);
        size_t sz = GlobalSize(hData);
        if (*pcbMax > sz)
            *pcbMax = sz;
    }

    return hData;
}


// safe set clipboard data
static void unlock_and_set(UINT uFormat, HANDLE hData)
{
    GlobalUnlock(hData);
    if (SetClipboardData(uFormat, hData) == NULL)
        GlobalFree(hData);
}


// MultiByte to WideChar
static HANDLE mb2wc(UINT cp, LPCVOID pSrc, size_t cchSrc, LPVOID ppDst, size_t* pcch)
{
    int cchDst = MultiByteToWideChar(cp, 0, pSrc, (int)cchSrc, NULL, 0);
    HANDLE hBuf = GlobalAlloc(GMEM_MOVEABLE, cchDst ? sizeof(WCHAR) * cchDst : 1);
    *(LPVOID*)ppDst = GlobalLock(hBuf);
    *pcch = (size_t)MultiByteToWideChar(cp, 0, pSrc, (int)cchSrc, *(LPVOID*)ppDst,
        cchDst);
    return hBuf;
}


// WideChar to MultiByte
static HANDLE wc2mb(UINT cp, LPCVOID pSrc, size_t cchSrc, LPVOID ppDst, size_t* pcch)
{
    int cchDst = WideCharToMultiByte(cp, 0, pSrc, (int)cchSrc, NULL, 0, NULL, NULL);
    HANDLE hBuf = GlobalAlloc(GMEM_MOVEABLE, cchDst ? cchDst : 1);
    *(LPVOID*)ppDst = GlobalLock(hBuf);
    *pcch = (size_t)WideCharToMultiByte(cp, 0, pSrc, (int)cchSrc, *(LPVOID*)ppDst,
        cchDst, NULL, NULL);
    return hBuf;
}
