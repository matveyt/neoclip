/*
 * neoclip - Neovim clipboard provider
 * Last Change:  2025 Jun 21
 * License:      https://unlicense.org
 * URL:          https://github.com/matveyt/neoclip
 */


#define WIN32_LEAN_AND_MEAN
#include "neoclip.h"
#include <windows.h>


// shared data
struct neo_UD {
    UINT uVimMeta, uVimRaw; // Vim clipboard format
    UINT uOEMCP, uACP;      // OEM/ANSI code page
};


// forward prototypes
static HANDLE get_and_lock(UINT uFormat, LPVOID ppData, size_t* pcbMax);
static bool unlock_and_set(UINT uFormat, HANDLE hData);
static HANDLE mb2wc(UINT cp, LPCVOID pSrc, size_t cchSrc, LPVOID ppDst, size_t* pcch);
static HANDLE wc2mb(UINT cp, LPCVOID pSrc, size_t cchSrc, LPVOID ppDst, size_t* pcch);


// module registration
__declspec(dllexport)
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

    lua_pushvalue(L, 1);                                // upvalue 1 : module name
    neo_UD* ud = lua_newuserdata(L, sizeof(neo_UD));    // upvalue 2 : shared data
    ud->uVimMeta = RegisterClipboardFormatW(L"VimClipboard2");
    ud->uVimRaw = RegisterClipboardFormatW(L"VimRawBytes");
    GetLocaleInfoW(LOCALE_USER_DEFAULT, LOCALE_IDEFAULTCODEPAGE
        | LOCALE_RETURN_NUMBER, (WCHAR*)&ud->uOEMCP, sizeof(UINT) / sizeof(WCHAR));
    GetLocaleInfoW(LOCALE_USER_DEFAULT, LOCALE_IDEFAULTANSICODEPAGE
        | LOCALE_RETURN_NUMBER, (WCHAR*)&ud->uACP, sizeof(UINT) / sizeof(WCHAR));

    // metatable for shared data
    luaL_newmetatable(L, lua_tostring(L, 1));
    //neo_pushcfunction(L, neo__gc);
    //lua_setfield(L, -2, "__gc");
    lua_setmetatable(L, -2);

#if defined(luaL_newlibtable)
    luaL_setfuncs(L, iface, 2);
#else
    luaL_openlib(L, NULL, iface, 2);
#endif
    return 1;
}


// get(regname) => [lines, regtype]
int neo_get(lua_State* L)
{
    luaL_checktype(L, 1, LUA_TSTRING);  // regname (unused)
    neo_UD* ud = neo_checkud(L, uv_share);

    // a table to return
    lua_createtable(L, 2, 0);
    if (!OpenClipboard(NULL))
        return 1;

    // get Vim meta
    int meta[4] = {
        MAUTO,      // type
        INT_MAX,    // ACP len
        INT_MAX,    // UCS len
        0,          // Raw len
    };
    HANDLE hBuf = NULL;
    LPVOID pBuf;
    size_t count = sizeof(meta);
    HANDLE hData;
    if ((hData = get_and_lock(ud->uVimMeta, &pBuf, &count)) != NULL) {
        memcpy(meta, pBuf, count & ~(sizeof(int) - 1));
        GlobalUnlock(hData);
        hData = NULL;
        pBuf = NULL;
    }

    do {
        // VimRawBytes
        if ((count = meta[3]) >= sizeof("utf-8")
            && (hData = get_and_lock(ud->uVimRaw, &pBuf, &count)) != NULL) {
            if (count >= sizeof("utf-8")
                && memcmp(pBuf, "utf-8", sizeof("utf-8")) == 0) {
                *(LPSTR*)&pBuf += sizeof("utf-8");
                count -= sizeof("utf-8");
                break;
            }
            GlobalUnlock(hData);
            hData = NULL;
            pBuf = NULL;
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
            HANDLE hTemp = mb2wc(ud->uACP, pBuf, count, &pBuf, &count);
            if (hTemp != NULL) {
                hBuf = wc2mb(CP_UTF8, pBuf, count, &pBuf, &count);
                GlobalUnlock(hTemp);
                GlobalFree(hTemp);
            }
            break;
        }
    } while (0);

    if (hData != NULL) {
        // note: pBuf may contain trailing NUL
        if (pBuf != NULL)
            neo_split(L, -1, pBuf, count, meta[0]);
        if (hBuf != NULL)
            GlobalUnlock(hBuf), GlobalFree(hBuf);
        GlobalUnlock(hData);
    }
    CloseClipboard();
    return 1;
}


// set(regname, lines, regtype) => boolean
int neo_set(lua_State* L)
{
    luaL_checktype(L, 1, LUA_TSTRING);  // regname (unused)
    luaL_checktype(L, 2, LUA_TTABLE);   // lines
    luaL_checktype(L, 3, LUA_TSTRING);  // regtype
    neo_UD* ud = neo_checkud(L, uv_share);

    bool success = OpenClipboard(NULL);
    if (success) {
        EmptyClipboard();
        neo_join(L, 2, "\r\n");

        // get UTF-8
        size_t cchACP = 0;
        size_t cchUCS, cchSrc;
        LPCSTR pSrc = lua_tolstring(L, -1, &cchSrc); ++cchSrc;
        HANDLE hBuf;
        LPVOID pBuf;

        // CF_UNICODETEXT
        hBuf = mb2wc(CP_UTF8, pSrc, (int)cchSrc, &pBuf, &cchUCS);
        if (hBuf != NULL) {
            // CF_OEMTEXT + CF_TEXT
            unlock_and_set(CF_OEMTEXT,
                wc2mb(ud->uOEMCP, pBuf, cchUCS, &(LPVOID){NULL}, &cchACP));
            unlock_and_set(CF_TEXT,
                wc2mb(ud->uACP, pBuf, cchUCS, &(LPVOID){NULL}, &cchACP));
        }
        success = unlock_and_set(CF_UNICODETEXT, hBuf) && success;

        // VimRawBytes
        hBuf = GlobalAlloc(GMEM_MOVEABLE, sizeof("utf-8") + cchSrc);
        if (hBuf != NULL) {
            pBuf = GlobalLock(hBuf);
            memcpy(pBuf, "utf-8", sizeof("utf-8"));                 // NUL terminated
            memcpy((LPSTR)pBuf + sizeof("utf-8"), pSrc, cchSrc);    // NUL terminated
        }
        success = unlock_and_set(ud->uVimRaw, hBuf) && success;

        // VimClipboard2
        hBuf = GlobalAlloc(GMEM_MOVEABLE, sizeof(int) * 4);
        if (hBuf != NULL) {
            int* pMeta = GlobalLock(hBuf);
            *pMeta++ = neo_type(*lua_tostring(L, 3));   // type
            *pMeta++ = cchACP ? cchACP - 1 : INT_MAX;   // ACP len
            *pMeta++ = cchUCS ? cchUCS - 1 : INT_MAX;   // UCS len
            *pMeta   = sizeof("utf-8") + cchSrc - 1;    // Raw len
        }
        success = unlock_and_set(ud->uVimMeta, hBuf) && success;

        CloseClipboard();
    }

    lua_pushboolean(L, success);
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
static bool unlock_and_set(UINT uFormat, HANDLE hData)
{
    if (hData == NULL || GlobalUnlock(hData) != 0)
        return false;

    DWORD dwError = GetLastError();
    if ((dwError == NO_ERROR || dwError == ERROR_NOT_LOCKED)
        && SetClipboardData(uFormat, hData) != NULL)
        return true;

    GlobalFree(hData);
    return false;
}


// MultiByte to WideChar
static HANDLE mb2wc(UINT cp, LPCVOID pSrc, size_t cchSrc, LPVOID ppDst, size_t* pcch)
{
    int cchDst = MultiByteToWideChar(cp, 0, pSrc, (int)cchSrc, NULL, 0);
    HANDLE hBuf = GlobalAlloc(GMEM_MOVEABLE, sizeof(WCHAR) * cchDst);
    if (hBuf != NULL) {
        *(LPVOID*)ppDst = GlobalLock(hBuf);
        *pcch = (size_t)MultiByteToWideChar(cp, 0, pSrc, (int)cchSrc, *(LPVOID*)ppDst,
            cchDst);
    } else {
        *(LPVOID*)ppDst = NULL;
        *pcch = 0;
    }
    return hBuf;
}


// WideChar to MultiByte
static HANDLE wc2mb(UINT cp, LPCVOID pSrc, size_t cchSrc, LPVOID ppDst, size_t* pcch)
{
    int cchDst = WideCharToMultiByte(cp, 0, pSrc, (int)cchSrc, NULL, 0, NULL, NULL);
    HANDLE hBuf = GlobalAlloc(GMEM_MOVEABLE, cchDst);
    if (hBuf != NULL) {
        *(LPVOID*)ppDst = GlobalLock(hBuf);
        *pcch = (size_t)WideCharToMultiByte(cp, 0, pSrc, (int)cchSrc, *(LPVOID*)ppDst,
            cchDst, NULL, NULL);
    } else {
        *(LPVOID*)ppDst = NULL;
        *pcch = 0;
    }
    return hBuf;
}
