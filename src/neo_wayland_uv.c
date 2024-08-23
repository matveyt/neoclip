/*
 * neoclip - Neovim clipboard provider
 * Last Change:  2024 Aug 23
 * License:      https://unlicense.org
 * URL:          https://github.com/matveyt/neoclip
 */


#include "neoclip_nix.h"
#include <unistd.h>
#include <wayland-client.h>
#include <wayland-wlr-data-control-client-protocol.h>


// our listeners
static void registry_global(void* X, struct wl_registry* registry, uint32_t name,
    const char* interface, uint32_t version);
static void data_control_device_data_offer(void* X,
    struct zwlr_data_control_device_v1* dcd, struct zwlr_data_control_offer_v1* offer);
static void data_control_device_primary_selection(void* X,
    struct zwlr_data_control_device_v1* dcd, struct zwlr_data_control_offer_v1* offer);
static void data_control_device_selection(void* X,
    struct zwlr_data_control_device_v1* dcd, struct zwlr_data_control_offer_v1* offer);
static void data_control_device_finished(void* X,
    struct zwlr_data_control_device_v1* dcd);
static void data_control_offer_offer(void* X, struct zwlr_data_control_offer_v1* offer,
    const char* mime_type);
static void data_control_source_send_prim(void* X,
    struct zwlr_data_control_source_v1* dcs, const char* mime_type, int fd);
static void data_control_source_send_clip(void* X,
    struct zwlr_data_control_source_v1* dcs, const char* mime_type, int fd);
static void data_control_source_cancelled(void* X,
    struct zwlr_data_control_source_v1* dcs);


// simplify Wayland listeners declaration
#define COUNTOF(o) (int)(sizeof(o) / sizeof(o[0]))
#define INDEX(ix) INDEX_##ix
#define LISTEN              \
    struct {                \
        const int count;    \
        void* pimpl;        \
    } _listen[] =
#define OBJECT(o, ix) [ix] = {                                      \
    .count = sizeof(struct o##_listener) / sizeof(LISTENER_FUNC),   \
    .pimpl = &(struct o##_listener)


enum {
    INDEX(registry),
    INDEX(device),
    INDEX(offer),
    INDEX(source_prim),
    INDEX(source_clip),
};
typedef void (*LISTENER_FUNC)(void);


static LISTEN {
    OBJECT(wl_registry, INDEX(registry)) {
        .global = registry_global,
    }},
    OBJECT(zwlr_data_control_device_v1, INDEX(device)) {
        .data_offer = data_control_device_data_offer,
        .primary_selection = data_control_device_primary_selection,
        .selection = data_control_device_selection,
        .finished = data_control_device_finished,
    }},
    OBJECT(zwlr_data_control_offer_v1, INDEX(offer)) {
        .offer = data_control_offer_offer,
    }},
    OBJECT(zwlr_data_control_source_v1, INDEX(source_prim)) {
        .send = data_control_source_send_prim,
        .cancelled = data_control_source_cancelled,
    }},
    OBJECT(zwlr_data_control_source_v1, INDEX(source_clip)) {
        .send = data_control_source_send_clip,
        .cancelled = data_control_source_cancelled,
    }},
};


// As Wayland segfaults on NULL listener we must provide stubs. Dirt!
static void _nop(void) {}
static inline void listen_init(void)
{
    for (int i = 0; i < COUNTOF(_listen); ++i)
        for (int j = 0; j < _listen[i].count; ++j)
            if (((LISTENER_FUNC*)_listen[i].pimpl)[j] == NULL)
                ((LISTENER_FUNC*)_listen[i].pimpl)[j] = _nop;
}


static inline int listen_to(void* object, int i, void* data)
{
    return wl_proxy_add_listener(object, _listen[i].pimpl, data);
}


// driver state
struct neo_X {
    struct wl_display* d;                       // Wayland display
    struct wl_seat* seat;                       // Wayland seat
    struct zwlr_data_control_manager_v1* dcm;   // wlroots data control manager
    struct zwlr_data_control_device_v1* dcd;    // wlroots data control device
    uint8_t* data[sel_total];                   // Selection: _VIMENC_TEXT
    size_t cb[sel_total];                       // Selection: text size only
};


// supported mime types (from best to worst)
static const char* mime[] = {
    [0] = "_VIMENC_TEXT",
    [1] = "_VIM_TEXT",
    "text/plain;charset=utf-8",
    "text/plain",
    "UTF8_STRING",
    "STRING",
    "TEXT",
};


// forward prototypes
static int neo__gc(lua_State* L);
static int cb_prepare(lua_State* L);
static int cb_poll(lua_State* L);
static size_t alloc_data(neo_X* x, int sel, size_t cb);
static void sel_read(neo_X* x, int sel, struct zwlr_data_control_offer_v1* offer);
static void sel_write(neo_X* x, int sel, const char* mime_type, int fd);
static void* offer_read(neo_X* x, struct zwlr_data_control_offer_v1* offer,
    const char* mime, size_t* pcb);


// init state and start thread
int neo_start(lua_State* L)
{
    neo_X* x = neo_x(L);
    if (x == NULL) {
        // create new state
        x = lua_newuserdata(L, sizeof(neo_X));

        // try to open display
        x->d = wl_display_connect(NULL);
        if (x->d == NULL) {
            lua_pushliteral(L, "wl_display_connect failed");
            return lua_error(L);
        }
        listen_init();

        // read globals from Wayland registry
        struct wl_registry* registry = wl_display_get_registry(x->d);
        listen_to(registry, INDEX(registry), x);
        x->seat = NULL, x->dcm = NULL;
        wl_display_roundtrip(x->d);
        wl_registry_destroy(registry);
        if (x->dcm == NULL) {
            wl_seat_release(x->seat);
            wl_display_disconnect(x->d);
            lua_pushliteral(L, "no support for wlr-data-control protocol");
            return lua_error(L);
        }

        // listen for new offers on our data device
        x->dcd = zwlr_data_control_manager_v1_get_data_device(x->dcm, x->seat);
        listen_to(x->dcd, INDEX(device), x);

        // clear data
        for (int i = 0; i < sel_total; ++i) {
            x->data[i] = NULL;
            x->cb[i] = 0;
        }

        // metatable for state
        luaL_newmetatable(L, lua_tostring(L, uv_module));
        neo_pushcfunction(L, neo__gc);
        lua_setfield(L, -2, "__gc");
        lua_setmetatable(L, -2);

        // uv_share.x = x
        lua_setfield(L, uv_share, "x");

        // start polling the display
        lua_getglobal(L, "vim");                // vim.uv or vim.loop => stack
        lua_getfield(L, -1, "uv");
        if (lua_isnil(L, -1)) {
            lua_pop(L, 1);
            lua_getfield(L, -1, "loop");
        }
        lua_replace(L, -2);

        // local prepare = uv.new_prepare()
        lua_getfield(L, -1, "new_prepare");
        lua_call(L, 0, 1);                      // prepare => stack
        // uv.prepare_start(prepare, cb_prepare)
        lua_getfield(L, -2, "prepare_start");
        lua_pushvalue(L, -2);
        neo_pushcfunction(L, cb_prepare);
        lua_call(L, 2, 0);

        // local poll = uv.new_poll(wl_display_get_fd(x->d))
        lua_getfield(L, -2, "new_poll");
        lua_pushinteger(L, wl_display_get_fd(x->d));
        lua_call(L, 1, 1);                      // poll => stack
        // uv.poll_start(poll, "rw", cb_poll)
        lua_getfield(L, -3, "poll_start");
        lua_pushvalue(L, -2);
        lua_pushliteral(L, "rw");
        neo_pushcfunction(L, cb_poll);
        lua_call(L, 3, 0);

        // uv_share.poll = poll
        lua_setfield(L, uv_share, "poll");      // poll <= stack
        // uv_share.prepare = prepare
        lua_setfield(L, uv_share, "prepare");   // prepare <= stack
        // uv_share.uv = vim.uv or vim.loop
        lua_setfield(L, uv_share, "uv");        // vim.uv or vim.loop <= stack
    }

    lua_pushnil(L);
    return 1;
}


// destroy state
static int neo__gc(lua_State* L)
{
    neo_X* x = (neo_X*)neo_checkud(L, 1);

    lua_getfield(L, uv_share, "uv");    // uv or loop => stack
    // uv.poll_stop(uv_share.poll)
    lua_getfield(L, -1, "poll_stop");
    lua_getfield(L, uv_share, "poll");
    if (lua_isnil(L, -1)) {
        lua_pop(L, 2);
    } else {
        lua_call(L, 1, 0);
        // uv.close(poll)
        lua_getfield(L, -1, "close");
        lua_getfield(L, uv_share, "poll");
        lua_call(L, 1, 0);
        // uv_share.poll = nil
        lua_pushnil(L);
        lua_setfield(L, uv_share, "poll");
    }

    // uv.prepare_stop(prepare)
    lua_getfield(L, -1, "prepare_stop");
    lua_getfield(L, uv_share, "prepare");
    if (lua_isnil(L, -1)) {
        lua_pop(L, 2);
    } else {
        lua_call(L, 1, 0);
        // uv.close(prepare)
        lua_getfield(L, -1, "close");
        lua_getfield(L, uv_share, "prepare");
        lua_call(L, 1, 0);
        // uv_share.prepare = nil
        lua_pushnil(L);
        lua_setfield(L, uv_share, "prepare");
    }

    // clear data
    for (int i = 0; i < sel_total; ++i)
        free(x->data[i]);
    zwlr_data_control_device_v1_destroy(x->dcd);
    zwlr_data_control_manager_v1_destroy(x->dcm);
    wl_seat_release(x->seat);
    wl_display_disconnect(x->d);

    return 0;
}


// fetch new selection
void neo_fetch(lua_State* L, int ix, int sel)
{
    neo_X* x = neo_x(L);
    if (x != NULL) {
        // wlr_data_control_device should've informed us of a new selection
        if (x->cb[sel] > 0)
            neo_split(L, ix, x->data[sel] + 1 + sizeof("utf-8"), x->cb[sel],
                x->data[sel][0]);
    }
}


// own new selection
// (cb == 0) => empty selection
void neo_own(neo_X* x, bool offer, int sel, const void* ptr, size_t cb, int type)
{
    // _VIMENC_TEXT: type 'encoding' NUL text
    cb = alloc_data(x, sel, cb);
    if (cb > 0) {
        x->data[sel][0] = type;
        memcpy(x->data[sel] + 1, "utf-8", sizeof("utf-8"));
        memcpy(x->data[sel] + 1 + sizeof("utf-8"), ptr, cb);
    }

    if (offer) {
        // offer our selection
        struct zwlr_data_control_source_v1* dcs =
            zwlr_data_control_manager_v1_create_data_source(x->dcm);
        for (int i = 0; i < COUNTOF(mime); ++i)
            zwlr_data_control_source_v1_offer(dcs, mime[i]);
        switch (sel) {
        case sel_prim:
            listen_to(dcs, INDEX(source_prim), x);
            zwlr_data_control_device_v1_set_primary_selection(x->dcd, dcs);
        break;
        case sel_clip:
            listen_to(dcs, INDEX(source_clip), x);
            zwlr_data_control_device_v1_set_selection(x->dcd, dcs);
        break;
        }
        wl_display_flush(x->d);
        wl_display_roundtrip(x->d);
    }
}


// uv_prepare_t callback
static int cb_prepare(lua_State* L)
{
    neo_X* x = neo_x(L);
    if (x != NULL) {
        while (wl_display_prepare_read(x->d) != 0)
            wl_display_dispatch_pending(x->d);
        wl_display_flush(x->d);
    }

    return 0;
}


// uv_poll_t callback
static int cb_poll(lua_State* L)
{
    neo_X* x = neo_x(L);
    if (x != NULL) {
        if (lua_isnil(L, 1) && strchr(lua_tostring(L, 2), 'r') != NULL)
            wl_display_read_events(x->d);
        else
            wl_display_cancel_read(x->d);
        wl_display_dispatch_pending(x->d);
    }

    return 0;
}


// wl_registry::global
static void registry_global(void* X, struct wl_registry* registry, uint32_t name,
    const char* interface, uint32_t version)
{
    neo_X* x = (neo_X*)X;
    struct {
        void** pobject;
        const struct wl_interface* iface;
    } globl[] = {
        { (void**)&x->seat, &wl_seat_interface, },
        { (void**)&x->dcm, &zwlr_data_control_manager_v1_interface, },
    };

    for (int i = 0; i < COUNTOF(globl); ++i) {
        if (strcmp(interface, globl[i].iface->name) == 0) {
            if (*globl[i].pobject == NULL)
                *globl[i].pobject = wl_registry_bind(registry, name, globl[i].iface,
                    version);
            break;
        }
    }
}


// [z]wlr_data_control_device_[v1]::data_offer
static void data_control_device_data_offer(void* X,
    struct zwlr_data_control_device_v1* dcd, struct zwlr_data_control_offer_v1* offer)
{
    (void)X;    // unused
    (void)dcd;  // unused

    listen_to(offer, INDEX(offer), (void*)(intptr_t)COUNTOF(mime));
}


// [z]wlr_data_control_device_[v1]::primary_selection
static void data_control_device_primary_selection(void* X,
    struct zwlr_data_control_device_v1* dcd, struct zwlr_data_control_offer_v1* offer)
{
    (void)dcd;  // unused
    sel_read((neo_X*)X, sel_prim, offer);
}


// [z]wlr_data_control_device_[v1]::selection
static void data_control_device_selection(void* X,
    struct zwlr_data_control_device_v1* dcd, struct zwlr_data_control_offer_v1* offer)
{
    (void)dcd;  // unused
    sel_read((neo_X*)X, sel_clip, offer);
}


// [z]wlr_data_control_device_[v1]::finished
static void data_control_device_finished(void* X,
    struct zwlr_data_control_device_v1* dcd)
{
    neo_X* x = (neo_X*)X;

    if (x->dcd == dcd) {
        x->dcd = zwlr_data_control_manager_v1_get_data_device(x->dcm, x->seat);
        listen_to(x->dcd, INDEX(device), x);
    }
    zwlr_data_control_device_v1_destroy(dcd);
}


// [z]wlr_data_control_offer_[v1]::offer
static void data_control_offer_offer(void* X, struct zwlr_data_control_offer_v1* offer,
    const char* mime_type)
{
    (void)X;    // unused

    int best = (intptr_t)wl_proxy_get_user_data((struct wl_proxy*)offer);
    for (int i = 0; i < best; ++i) {
        if (strcmp(mime_type, mime[i]) == 0) {
            best = i;
            break;
        }
    }
    wl_proxy_set_user_data((struct wl_proxy*)offer, (void*)(intptr_t)best);
}


// [z]wlr_data_control_source_[v1]::send
static void data_control_source_send_prim(void* X,
    struct zwlr_data_control_source_v1* dcs, const char* mime_type, int fd)
{
    (void)dcs;   // unused
    sel_write((neo_X*)X, sel_prim, mime_type, fd);
}


// [z]wlr_data_control_source_[v1]::send
static void data_control_source_send_clip(void* X,
    struct zwlr_data_control_source_v1* dcs, const char* mime_type, int fd)
{
    (void)dcs;   // unused
    sel_write((neo_X*)X, sel_clip, mime_type, fd);
}


// [z]wlr_data_control_source_[v1]::cancelled
static void data_control_source_cancelled(void* X,
    struct zwlr_data_control_source_v1* dcs)
{
    (void)X;    // unused
    zwlr_data_control_source_v1_destroy(dcs);
}


// (re-)allocate data buffer for selection
static size_t alloc_data(neo_X* x, int sel, size_t cb)
{
    if (cb > 0) {
        void* ptr = realloc(x->data[sel], 1 + sizeof("utf-8") + cb);
        if (ptr != NULL) {
            x->data[sel] = ptr;
            x->cb[sel] = cb;
        }
    } else {
        free(x->data[sel]);
        x->data[sel] = NULL;
        x->cb[sel] = 0;
    }

    return x->cb[sel];
}


// read selection data from offer
static void sel_read(neo_X* x, int sel, struct zwlr_data_control_offer_v1* offer)
{
    if (offer == NULL) {
        neo_own(x, false, sel, NULL, 0, 0);
        return;
    }

    int best_mime = (intptr_t)wl_proxy_get_user_data((struct wl_proxy*)offer);
    if (best_mime >= 0 && best_mime < COUNTOF(mime)) {
        size_t cb;
        uint8_t* ptr = offer_read(x, offer, mime[best_mime], &cb);
        int type = (cb > 0 && best_mime <= 1) ? ptr[0] : MAUTO;

        void* data = ptr;
        if (cb == 0) {
            // nothing to do
        } else if (best_mime == 0) {
            // _VIMENC_TEXT
            if (cb >= 1 + sizeof("utf-8")
                && memcmp(ptr + 1, "utf-8", sizeof("utf-8")) == 0) {
                // this is UTF-8
                data = ptr + 1 + sizeof("utf-8");
                cb -= 1 + sizeof("utf-8");
            } else {
                // Vim must have UTF8_STRING
                free(ptr);
                data = ptr = offer_read(x, offer, "UTF8_STRING", &cb);
            }
        } else if (best_mime == 1) {
            // _VIM_TEXT
            data = ptr + 1;
            --cb;
        }

        neo_own(x, false, sel, data, cb, type);
        free(ptr);
    }

    zwlr_data_control_offer_v1_destroy(offer);
}


// write selection data to file descriptor
static void sel_write(neo_X* x, int sel, const char* mime_type, int fd)
{
    // assume _VIMENC_TEXT
    uint8_t* buf = x->data[sel];
    size_t cb = 1 + sizeof("utf-8") + x->cb[sel];
    ssize_t n = 1;

    // not _VIMENC_TEXT?
    if (strcmp(mime_type, mime[0]) != 0) {
        // _VIM_TEXT: output type
        if (strcmp(mime_type, mime[1]) == 0)
            n = write(fd, buf, 1);

        // skip over header
        buf += 1 + sizeof("utf-8");
        cb -= 1 + sizeof("utf-8");
    }

    // output selection
    if (n > 0)
        n = write(fd, buf, cb);

    close(fd);
}


// read specific mime type from offer
static void* offer_read(neo_X* x, struct zwlr_data_control_offer_v1* offer,
    const char* mime, size_t* pcb)
{
    uint8_t* ptr = NULL;
    size_t total = 0;
    int fds[2];

    if (pipe(fds) == 0) {
        zwlr_data_control_offer_v1_receive(offer, mime, fds[1]);
        wl_display_roundtrip(x->d);
        close(fds[1]);

        void* buf = malloc(64 * 1024);
        if (buf != NULL) {
            ssize_t part;
            while ((part = read(fds[0], buf, 64 * 1024)) > 0) {
                void* ptr2 = realloc(ptr, total + part);
                if (ptr2 == NULL)
                    break;
                ptr = ptr2;
                memcpy(ptr + total, buf, part);
                total += part;
            }
            free(buf);
        }
        close(fds[0]);
    }

    *pcb = total;
    return ptr;
}
