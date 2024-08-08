/*
 * neoclip - Neovim clipboard provider
 * Last Change:  2024 Aug 08
 * License:      https://unlicense.org
 * URL:          https://github.com/matveyt/neoclip
 */


#define WIN32_LEAN_AND_MEAN
#include "neoclip.h"
#include <windows.h>


// userdata
typedef struct {
    UINT uVimMeta;
    UINT uVimRaw;
} UD;


// forward prototypes
static HANDLE get_and_lock(UINT uFormat, LPVOID ppData, size_t* pcbMax);
static BOOL unlock_and_set(UINT uFormat, HANDLE hData);
static HANDLE mb2wc(UINT cp, LPCVOID pSrc, size_t cchSrc, LPVOID ppDst, size_t* pcch);
static HANDLE wc2mb(UINT cp, LPCVOID pSrc, size_t cchSrc, LPVOID ppDst, size_t* pcch);


// module registration
__declspec(dllexport)
int luaopen_driver(lua_State* L)
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

    lua_pushvalue(L, 1);                        // upvalue 1: module name
    UD* ud = lua_newuserdata(L, sizeof(UD));    // upvalue 2: userdata
    ud->uVimMeta = RegisterClipboardFormatW(L"VimClipboard2");
    ud->uVimRaw = RegisterClipboardFormatW(L"VimRawBytes");
    // metatable for userdata
    luaL_newmetatable(L, lua_tostring(L, 1));
    //lua_pushcfunction(L, neo__gc);
    //lua_setfield(L, -2, "__gc");
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


// neoclip.get(regname) -> [lines, regtype]
// read from system clipboard
int neo_get(lua_State* L)
{
    luaL_checktype(L, 1, LUA_TSTRING);  // regname (unused)
    UD* ud = neo_ud(L);

    // a table to return
    lua_newtable(L);
    if (!OpenClipboard(NULL))
        return 1;

    // get Vim meta
    int meta[4] = {
        255,        // type
        INT_MAX,    // ACP len
        INT_MAX,    // UCS len
        0,          // Raw len
    };
    HANDLE hData, hBuf = NULL;
    LPVOID pBuf;
    size_t count = sizeof(meta);
    if ((hData = get_and_lock(ud->uVimMeta, &pBuf, &count)) != NULL) {
        memcpy(meta, pBuf, count & ~(sizeof(int) - 1));
        GlobalUnlock(hData);
        hData = NULL;
    }

    do {
        // VimRawBytes
        if ((count = meta[3]) >= sizeof("utf-8")
            && (hData = get_and_lock(ud->uVimRaw, &pBuf, &count)) != NULL) {
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
        // note: pBuf may contain trailing NUL but neo_split() will take care of it
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
    UD* ud = neo_ud(L);

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
        bSuccess = unlock_and_set(CF_TEXT,
            wc2mb(CP_ACP, pBuf, cchUCS, &(LPVOID){NULL}, &cchACP));
        bSuccess = unlock_and_set(CF_UNICODETEXT, hBuf) && bSuccess;

        // VimRawBytes
        hBuf = GlobalAlloc(GMEM_MOVEABLE, sizeof("utf-8") + cchSrc);
        pBuf = GlobalLock(hBuf);
        memcpy(pBuf, "utf-8", sizeof("utf-8"));                 // NUL terminated
        memcpy((LPSTR)pBuf + sizeof("utf-8"), pSrc, cchSrc);    // NUL terminated
        bSuccess = unlock_and_set(ud->uVimRaw, hBuf) && bSuccess;

        // VimClipboard2
        hBuf = GlobalAlloc(GMEM_MOVEABLE, sizeof(int) * 4);
        int* pMeta = GlobalLock(hBuf);
        *pMeta++ = neo_type(*lua_tostring(L, 3));   // type
        *pMeta++ = cchACP ? cchACP - 1 : 0;         // ACP len
        *pMeta++ = cchUCS ? cchUCS - 1 : 0;         // UCS len
        *pMeta   = sizeof("utf-8") + cchSrc - 1;    // Raw len
        bSuccess = unlock_and_set(ud->uVimMeta, hBuf) && bSuccess;

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
static BOOL unlock_and_set(UINT uFormat, HANDLE hData)
{
    GlobalUnlock(hData);
    if (SetClipboardData(uFormat, hData) == NULL) {
        GlobalFree(hData);
        return FALSE;
    }
    return TRUE;
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
