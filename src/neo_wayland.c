/*
 * neoclip - Neovim clipboard provider
 * Last Change:  2025 Jun 20
 * License:      https://unlicense.org
 * URL:          https://github.com/matveyt/neoclip
 */


#include "neo_wayland.h"


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


// Wayland listeners
static LISTEN {
    OBJECT(wl_registry, INDEX(registry)) {
        .global = registry_global,
    }},
    OBJECT(ext_data_control_device_v1, INDEX(device)) {
        .data_offer = data_control_device_data_offer,
        .primary_selection = data_control_device_primary_selection,
        .selection = data_control_device_selection,
        .finished = data_control_device_finished,
    }},
    OBJECT(ext_data_control_offer_v1, INDEX(offer)) {
        .offer = data_control_offer_offer,
    }},
    OBJECT(ext_data_control_source_v1, INDEX(source_prim)) {
        .send = data_control_source_send_prim,
        .cancelled = data_control_source_cancelled,
    }},
    OBJECT(ext_data_control_source_v1, INDEX(source_clip)) {
        .send = data_control_source_send_clip,
        .cancelled = data_control_source_cancelled,
    }},
};


// As Wayland segfaults on NULL listener we must provide stubs. Dirt!
static void _nop(void) {}
static inline void listen_init(void)
{
    for (size_t i = 0; i < _countof(_listen); ++i)
        for (size_t j = 0; j < _listen[i].count; ++j)
            if (((WAYLAND_LISTENER*)_listen[i].pimpl)[j] == NULL)
                ((WAYLAND_LISTENER*)_listen[i].pimpl)[j] = _nop;
}
static inline int listen_to(void* object, int i, void* data)
{
    return wl_proxy_add_listener(object, _listen[i].pimpl, data);
}


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
            lua_pushliteral(L, "no support for ext-data-control protocol");
            return lua_error(L);
        }

        // ext-data-control or zwlr-data-control?
        if (strcmp(wl_proxy_get_class((struct wl_proxy*)x->dcm),
            ext_data_control_manager_v1_interface.name) == 0) {
            x->dcd_iface = &ext_data_control_device_v1_interface;
            x->dcs_iface = &ext_data_control_source_v1_interface;
        } else {
            x->dcd_iface = &zwlr_data_control_device_v1_interface;
            x->dcs_iface = &zwlr_data_control_source_v1_interface;
        }

        // listen for new offers on our data device
        x->dcd = get_data_device(x);
        listen_to(x->dcd, INDEX(device), x);

        // clear data
        for (size_t i = 0; i < sel_total; ++i) {
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

#ifdef WITH_LUV
        // start polling display
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
#endif // WITH_LUV

#ifdef WITH_THREADS
        // start thread
        pthread_mutex_init(&x->lock, NULL);
        pthread_create(&x->tid, NULL, thread_main, x);
#endif // WITH_THREADS
    }

    lua_pushnil(L);
    return 1;
}


// destroy state
static int neo__gc(lua_State* L)
{
    neo_X* x = (neo_X*)neo_checkud(L, 1);

#ifdef WITH_LUV
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
#endif // WITH_LUV

#ifdef WITH_THREADS
    pthread_kill(x->tid, SIGTERM);
    pthread_join(x->tid, NULL);
    pthread_mutex_destroy(&x->lock);
#endif // WITH_THREADS

    // clear data
    for (size_t i = 0; i < sel_total; ++i)
        free(x->data[i]);
    ext_data_control_device_v1_destroy(x->dcd);
    ext_data_control_manager_v1_destroy(x->dcm);
    wl_seat_release(x->seat);
    wl_display_disconnect(x->d);

    return 0;
}


// fetch new selection
void neo_fetch(lua_State* L, int ix, int sel)
{
    neo_X* x = neo_x(L);
    if (x != NULL && neo_lock(x)) {
        // ext_data_control_device should've informed us of a new selection
        if (x->cb[sel] > 0)
            neo_split(L, ix, x->data[sel] + 1 + sizeof("utf-8"), x->cb[sel],
                x->data[sel][0]);

        // release lock
        neo_unlock(x);
    }
}


// own new selection
// (cb == 0) => empty selection
void neo_own(neo_X* x, bool offer, int sel, const void* ptr, size_t cb, int type)
{
    if (neo_lock(x)) {
        // _VIMENC_TEXT: type 'encoding' NUL text
        cb = alloc_data(x, sel, cb);
        if (cb > 0) {
            x->data[sel][0] = type;
            memcpy(x->data[sel] + 1, "utf-8", sizeof("utf-8"));
            memcpy(x->data[sel] + 1 + sizeof("utf-8"), ptr, cb);
        }

        if (offer) {
            // offer our selection
            struct ext_data_control_source_v1* dcs = create_data_source(x);
            for (size_t i = 0; i < _countof(mime); ++i)
                ext_data_control_source_v1_offer(dcs, mime[i]);
            switch (sel) {
            case sel_prim:
                listen_to(dcs, INDEX(source_prim), x);
                ext_data_control_device_v1_set_primary_selection(x->dcd, dcs);
            break;
            case sel_clip:
                listen_to(dcs, INDEX(source_clip), x);
                ext_data_control_device_v1_set_selection(x->dcd, dcs);
            break;
            }
            wl_display_flush(x->d);
#ifdef WITH_LUV
            // dispatch in the polling thread only!
            wl_display_roundtrip(x->d);
#endif // WITH_LUV
        }

        neo_unlock(x);
    }
}


#ifdef WITH_LUV
// uv_prepare_t callback
static int cb_prepare(lua_State* L)
{
    neo_X* x = neo_x(L);
    if (x != NULL)
        prepare_event(x->d);

    return 0;
}
#endif // WITH_LUV


#ifdef WITH_LUV
// uv_poll_t callback
static int cb_poll(lua_State* L)
{
    neo_X* x = neo_x(L);
    if (x != NULL)
        dispatch_event(x->d,
            lua_isnil(L, 1) && strchr(lua_tostring(L, 2), 'r') != NULL);

    return 0;
}
#endif // WITH_LUV


#ifdef WITH_THREADS
// thread entry point
static void* thread_main(void* X)
{
    neo_X* x = (neo_X*)X;

    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGINT);
    sigaddset(&mask, SIGTERM);
    pthread_sigmask(SIG_BLOCK, &mask, NULL);

    struct pollfd fds[] = {
        { .fd = signalfd(-1, &mask, 0), .events = POLLIN, },
        { .fd = wl_display_get_fd(x->d), .events = POLLIN, },
    };

    do {
        prepare_event(x->d);

        if (poll(fds, _countof(fds), -1) < 0)
            break;

        if (fds[0].revents & POLLIN) {
            struct signalfd_siginfo ssi;
            size_t cb = read(fds[0].fd, &ssi, sizeof(ssi));
            if (cb != sizeof(ssi) || ssi.ssi_signo == SIGINT || ssi.ssi_signo == SIGTERM)
                break;
        }
    } while (dispatch_event(x->d, fds[1].revents & POLLIN) >= 0);

    close(fds[0].fd);
    wl_display_cancel_read(x->d);
    return NULL;
}
#endif // WITH_THREADS


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
        { (void**)&x->dcm, &ext_data_control_manager_v1_interface, },
        { (void**)&x->dcm, &zwlr_data_control_manager_v1_interface, },
    };

    for (size_t i = 0; i < _countof(globl); ++i) {
        if (strcmp(interface, globl[i].iface->name) == 0) {
            if (*globl[i].pobject == NULL)
                *globl[i].pobject = wl_registry_bind(registry, name, globl[i].iface,
                    version);
            break;
        }
    }
}


// ext_data_control_device_v1::data_offer
static void data_control_device_data_offer(void* X,
    struct ext_data_control_device_v1* dcd, struct ext_data_control_offer_v1* offer)
{
    (void)X;    // unused
    (void)dcd;  // unused

    listen_to(offer, INDEX(offer), (void*)(uintptr_t)_countof(mime));
}


// ext_data_control_device_v1::primary_selection
static void data_control_device_primary_selection(void* X,
    struct ext_data_control_device_v1* dcd, struct ext_data_control_offer_v1* offer)
{
    (void)dcd;  // unused
    sel_read((neo_X*)X, sel_prim, offer);
}


// ext_data_control_device_v1::selection
static void data_control_device_selection(void* X,
    struct ext_data_control_device_v1* dcd, struct ext_data_control_offer_v1* offer)
{
    (void)dcd;  // unused
    sel_read((neo_X*)X, sel_clip, offer);
}


// ext_data_control_device_v1::finished
static void data_control_device_finished(void* X,
    struct ext_data_control_device_v1* dcd)
{
    neo_X* x = (neo_X*)X;

    if (x->dcd == dcd) {
        x->dcd = get_data_device(x);
        listen_to(x->dcd, INDEX(device), x);
    }
    ext_data_control_device_v1_destroy(dcd);
}


// ext_data_control_offer_v1::offer
static void data_control_offer_offer(void* X, struct ext_data_control_offer_v1* offer,
    const char* mime_type)
{
    (void)X;    // unused

    size_t best_mime = (uintptr_t)ext_data_control_offer_v1_get_user_data(offer);
    for (size_t i = 0; i < best_mime; ++i) {
        if (strcmp(mime_type, mime[i]) == 0) {
            best_mime = i;
            break;
        }
    }
    ext_data_control_offer_v1_set_user_data(offer, (void*)(uintptr_t)best_mime);
}


// ext_data_control_source_v1::send
static void data_control_source_send_prim(void* X,
    struct ext_data_control_source_v1* dcs, const char* mime_type, int fd)
{
    (void)dcs;   // unused
    sel_write((neo_X*)X, sel_prim, mime_type, fd);
}


// ext_data_control_source_v1::send
static void data_control_source_send_clip(void* X,
    struct ext_data_control_source_v1* dcs, const char* mime_type, int fd)
{
    (void)dcs;   // unused
    sel_write((neo_X*)X, sel_clip, mime_type, fd);
}


// ext_data_control_source_v1::cancelled
static void data_control_source_cancelled(void* X,
    struct ext_data_control_source_v1* dcs)
{
    (void)X;    // unused
    ext_data_control_source_v1_destroy(dcs);
}


// (re-)allocate data buffer for selection
// Note: caller must acquire neo_lock() first
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


// read or cancel wl_display event
static int dispatch_event(struct wl_display* d, bool valid)
{
    if (valid)
        wl_display_read_events(d);
    else
        wl_display_cancel_read(d);
    return wl_display_dispatch_pending(d);
}


// prepare to read wl_display event
static int prepare_event(struct wl_display* d)
{
    while (wl_display_prepare_read(d) != 0)
        wl_display_dispatch_pending(d);
    return wl_display_flush(d);
}


// read selection data from offer
static void sel_read(neo_X* x, int sel, struct ext_data_control_offer_v1* offer)
{
    if (offer == NULL) {
        neo_own(x, false, sel, NULL, 0, 0);
        return;
    }

    size_t best_mime = (uintptr_t)ext_data_control_offer_v1_get_user_data(offer);
    if (best_mime < _countof(mime)) {
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

    ext_data_control_offer_v1_destroy(offer);
}


// write selection data to file descriptor
static void sel_write(neo_X* x, int sel, const char* mime_type, int fd)
{
    if (neo_lock(x)) {
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
        neo_unlock(x);
    }

    close(fd);
}


// read specific mime type from offer
static void* offer_read(neo_X* x, struct ext_data_control_offer_v1* offer,
    const char* mime, size_t* pcb)
{
    uint8_t* ptr = NULL;
    size_t total = 0;
    int fds[2];

    if (pipe(fds) == 0) {
        ext_data_control_offer_v1_receive(offer, mime, fds[1]);
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
