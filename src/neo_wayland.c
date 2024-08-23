/*
 * neoclip - Neovim clipboard provider
 * Last Change:  2024 Aug 23
 * License:      https://unlicense.org
 * URL:          https://github.com/matveyt/neoclip
 */


#include "neoclip_nix.h"
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <unistd.h>
#include <sys/signalfd.h>
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
    pthread_mutex_t lock;                       // Mutex lock
    pthread_t tid;                              // Thread ID
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
static bool neo_lock(neo_X* x);
static bool neo_unlock(neo_X* x);
static void* thread_main(void* X);
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

        // start thread
        pthread_mutex_init(&x->lock, NULL);
        pthread_create(&x->tid, NULL, thread_main, x);
    }

    lua_pushnil(L);
    return 1;
}


// destroy state
static int neo__gc(lua_State* L)
{
    neo_X* x = (neo_X*)neo_checkud(L, 1);

    // clear data
    pthread_kill(x->tid, SIGTERM);
    pthread_join(x->tid, NULL);
    pthread_mutex_destroy(&x->lock);
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
    if (x != NULL && neo_lock(x)) {
        // wlr_data_control_device should've informed us of a new selection
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
            // dispatch in the polling thread only!
            //wl_display_roundtrip(x->d);
        }

        neo_unlock(x);
    }
}


// lock selection data
static inline bool neo_lock(neo_X* x)
{
    return (pthread_mutex_lock(&x->lock) == 0);
}


// unlock selection data
static inline bool neo_unlock(neo_X* x)
{
    return (pthread_mutex_unlock(&x->lock) == 0);
}


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

    for (;;) {
        while (wl_display_prepare_read(x->d) != 0)
            wl_display_dispatch_pending(x->d);
        wl_display_flush(x->d);

        if (poll(fds, COUNTOF(fds), -1) < 0)
            break;

        if (fds[0].revents & POLLIN) {
            struct signalfd_siginfo ssi;
            size_t cb = read(fds[0].fd, &ssi, sizeof(ssi));
            if (cb != sizeof(ssi) || ssi.ssi_signo == SIGINT || ssi.ssi_signo == SIGTERM)
                break;
        }

        if (fds[1].revents & POLLIN)
            wl_display_read_events(x->d);
        else
            wl_display_cancel_read(x->d);
        wl_display_dispatch_pending(x->d);
    }

    close(fds[0].fd);
    wl_display_cancel_read(x->d);
    return NULL;
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
