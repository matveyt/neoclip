/*
 * neoclip - Neovim clipboard provider
 * Last Change:  2024 Aug 13
 * License:      https://unlicense.org
 * URL:          https://github.com/matveyt/neoclip
 */


#include "neoclip_nix.h"
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
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


// context structure
typedef struct {
    struct wl_display* d;                       // Wayland display
    struct wl_seat* seat;                       // Wayland seat
    struct zwlr_data_control_manager_v1* dcm;   // wlroots data control manager
    struct zwlr_data_control_device_v1* dcd;    // wlroots data control device
    uint8_t* data[sel_total];                   // Selection: _VIMENC_TEXT
    size_t cb[sel_total];                       // Selection: text size only
    pthread_mutex_t lock;                       // Mutex lock
    pthread_t tid;                              // Thread ID
} neo_W;


// supported mime types (from best to worst)
static const char* mime[] = {
    "_VIMENC_TEXT",
    "_VIM_TEXT",
    "text/plain;charset=utf-8",
    "text/plain",
    "UTF8_STRING",
    "STRING",
    "TEXT",
};


// forward prototypes
static void* thread_main(void* X);
static size_t alloc_data(neo_W* w, int sel, size_t cb);
static void sel_read(neo_W* w, int sel, struct zwlr_data_control_offer_v1* offer);
static void sel_write(neo_W* w, int sel, const char* mime_type, int fd);
static void* offer_read(neo_W* w, struct zwlr_data_control_offer_v1* offer,
    const char* mime, size_t* pcb);


// init context and start thread
void* neo_create(bool first_run, bool targets_atom, const char** perr)
{
    (void)first_run;    // unused
    (void)targets_atom; // unused

    // create context
    neo_W* w = calloc(1, sizeof(neo_W));
    if (w == NULL) {
        *perr = "Memory allocation error";
        return NULL;
    }

    // try to open display
    w->d = wl_display_connect(NULL);
    if (w->d == NULL) {
        free(w);
        *perr = "wl_display_connect failed";
        return NULL;
    }
    listen_init();

    // read globals from registry
    struct wl_registry* reg = wl_display_get_registry(w->d);
    listen_to(reg, INDEX(registry), w);
    wl_display_roundtrip(w->d);
    wl_registry_destroy(reg);
    if (w->seat == NULL || w->dcm == NULL) {
        wl_display_disconnect(w->d);
        free(w);
        *perr = "no support for wlr-data-control protocol";
        return NULL;
    }

    // listen for new offers on our data device
    w->dcd = zwlr_data_control_manager_v1_get_data_device(w->dcm, w->seat);
    listen_to(w->dcd, INDEX(device), w);

    // start thread
    pthread_mutex_init(&w->lock, NULL);
    pthread_create(&w->tid, NULL, thread_main, w);
    return w;
}


// destroy context
void neo_kill(void* X)
{
    if (X != NULL) {
        neo_W* w = (neo_W*)X;

        pthread_kill(w->tid, SIGTERM);
        pthread_join(w->tid, NULL);
        pthread_mutex_destroy(&w->lock);
        free(w->data[0]);
        free(w->data[1]);
        zwlr_data_control_device_v1_destroy(w->dcd);
        zwlr_data_control_manager_v1_destroy(w->dcm);
        wl_display_disconnect(w->d);
        free(w);
    }
}


// lock or unlock selection data
bool neo_lock(void* X, bool lock)
{
    neo_W* w = (neo_W*)X;
    int error = lock ? pthread_mutex_lock(&w->lock) : pthread_mutex_unlock(&w->lock);
    return (error == 0);
}


// fetch new selection
// note: caller must unlock a pointer returned unless it is NULL
const void* neo_fetch(void* X, int sel, size_t* pcb, int* ptype)
{
    if (neo_lock(X, true)) {
        neo_W* w = (neo_W*)X;

        // wlr_data_control_device should've informed us of a new selection
        if (w->cb[sel] > 0) {
            *pcb = w->cb[sel];
            *ptype = w->data[sel][0];
            return (w->data[sel] + 1 + sizeof("utf-8"));
        }

        // unlock if clipboard is empty
        neo_lock(X, false);
    }

    return NULL;
}


// own new selection
// (cb == 0) => empty selection, (cb == SIZE_MAX) => keep selection
void neo_own(void* X, bool offer, int sel, const void* ptr, size_t cb, int type)
{
    if (neo_lock(X, true)) {
        neo_W* w = (neo_W*)X;

        // set new data
        if (cb < SIZE_MAX) {
            // _VIMENC_TEXT: type 'encoding' NUL text
            cb = alloc_data(w, sel, cb);
            if (cb > 0) {
                w->data[sel][0] = type;
                memcpy(w->data[sel] + 1, "utf-8", sizeof("utf-8"));
                memcpy(w->data[sel] + 1 + sizeof("utf-8"), ptr, cb);
            }
        }

        if (offer) {
            // offer our selection
            struct zwlr_data_control_source_v1* dcs =
                zwlr_data_control_manager_v1_create_data_source(w->dcm);
            for (int i = 0; i < COUNTOF(mime); ++i)
                zwlr_data_control_source_v1_offer(dcs, mime[i]);
            switch (sel) {
            case sel_prim:
                listen_to(dcs, INDEX(source_prim), w);
                zwlr_data_control_device_v1_set_primary_selection(w->dcd, dcs);
            break;
            case sel_clip:
                listen_to(dcs, INDEX(source_clip), w);
                zwlr_data_control_device_v1_set_selection(w->dcd, dcs);
            break;
            }
            wl_display_flush(w->d);
        }

        neo_lock(X, false);
    }
}


// thread entry point
static void* thread_main(void* X)
{
    neo_W* w = (neo_W*)X;

    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGINT);
    sigaddset(&mask, SIGTERM);
    pthread_sigmask(SIG_BLOCK, &mask, NULL);

    struct pollfd fds[2] = {
        { .fd = wl_display_get_fd(w->d), .events = POLLIN, },
        { .fd = signalfd(-1, &mask, 0), .events = POLLIN, },
    };

    for (;;) {
        while (wl_display_prepare_read(w->d) != 0)
            wl_display_dispatch_pending(w->d);
        wl_display_flush(w->d);

        if (poll(fds, 2, -1) < 0) {
            wl_display_cancel_read(w->d);
            break;
        }

        if (fds[0].revents == POLLIN) {
            if (wl_display_read_events(w->d) != -1)
                wl_display_dispatch_pending(w->d);
        } else {
            wl_display_cancel_read(w->d);
        }

        if (fds[1].revents & POLLIN) {
            struct signalfd_siginfo ssi;
            size_t cb = read(fds[1].fd, &ssi, sizeof(ssi));
            if (cb != sizeof(ssi) || ssi.ssi_signo == SIGINT || ssi.ssi_signo == SIGTERM)
                break;
        }
    }

    close(fds[1].fd);
    return NULL;
}


// (re-)allocate data buffer for selection
// Note: caller must acquire neo_lock() first
static size_t alloc_data(neo_W* w, int sel, size_t cb)
{
    if (cb > 0) {
        void* ptr = realloc(w->data[sel], 1 + sizeof("utf-8") + cb);
        if (ptr != NULL) {
            w->data[sel] = ptr;
            w->cb[sel] = cb;
        }
    } else {
        free(w->data[sel]);
        w->data[sel] = NULL;
        w->cb[sel] = 0;
    }

    return w->cb[sel];
}


// wl_registry::global
static void registry_global(void* X, struct wl_registry* registry, uint32_t name,
    const char* interface, uint32_t version)
{
    neo_W* w = (neo_W*)X;
    struct {
        void** pobject;
        const struct wl_interface* iface;
    } globl[] = {
        { (void**)&w->seat, &wl_seat_interface },
        { (void**)&w->dcm, &zwlr_data_control_manager_v1_interface },
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
    sel_read((neo_W*)X, sel_prim, offer);
}


// [z]wlr_data_control_device_[v1]::selection
static void data_control_device_selection(void* X,
    struct zwlr_data_control_device_v1* dcd, struct zwlr_data_control_offer_v1* offer)
{
    (void)dcd;  // unused
    sel_read((neo_W*)X, sel_clip, offer);
}


// [z]wlr_data_control_device_[v1]::finished
static void data_control_device_finished(void* X,
    struct zwlr_data_control_device_v1* dcd)
{
    neo_W* w = (neo_W*)X;

    if (w->dcd == dcd) {
        w->dcd = zwlr_data_control_manager_v1_get_data_device(w->dcm, w->seat);
        listen_to(w->dcd, INDEX(device), w);
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
    sel_write((neo_W*)X, sel_prim, mime_type, fd);
}


// [z]wlr_data_control_source_[v1]::send
static void data_control_source_send_clip(void* X,
    struct zwlr_data_control_source_v1* dcs, const char* mime_type, int fd)
{
    (void)dcs;   // unused
    sel_write((neo_W*)X, sel_clip, mime_type, fd);
}


// [z]wlr_data_control_source_[v1]::cancelled
static void data_control_source_cancelled(void* X,
    struct zwlr_data_control_source_v1* dcs)
{
    (void)X;    // unused
    zwlr_data_control_source_v1_destroy(dcs);
}


// read selection data from offer
static void sel_read(neo_W* w, int sel, struct zwlr_data_control_offer_v1* offer)
{
    if (offer == NULL) {
        neo_own(w, false, sel, NULL, 0, 0);
        return;
    }

    int best_mime = (intptr_t)wl_proxy_get_user_data((struct wl_proxy*)offer);
    if (best_mime >= 0 && best_mime < COUNTOF(mime)) {
        size_t cb;
        uint8_t* ptr = offer_read(w, offer, mime[best_mime], &cb);
        int type = (cb > 0 && best_mime <= 1) ? ptr[0] : 255;

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
                data = ptr = offer_read(w, offer, "UTF8_STRING", &cb);
            }
        } else if (best_mime == 1) {
            // _VIM_TEXT
            data = ptr + 1;
            --cb;
        }

        neo_own(w, false, sel, data, cb, type);
        free(ptr);
    }

    zwlr_data_control_offer_v1_destroy(offer);
}


// write selection data to file descriptor
static void sel_write(neo_W* w, int sel, const char* mime_type, int fd)
{
    if (neo_lock(w, true)) {
        // assume _VIMENC_TEXT
        uint8_t* buf = w->data[sel];
        size_t cb = 1 + sizeof("utf-8") + w->cb[sel];
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
        neo_lock(w, false);
    }

    close(fd);
}


// read specific mime type from offer
static void* offer_read(neo_W* w, struct zwlr_data_control_offer_v1* offer,
    const char* mime, size_t* pcb)
{
    uint8_t* ptr = NULL;
    size_t total = 0;
    int fds[2];

    if (pipe(fds) == 0) {
        zwlr_data_control_offer_v1_receive(offer, mime, fds[1]);
        wl_display_roundtrip(w->d);
        close(fds[1]);

        void* buf = malloc(32 * 1024);
        if (buf != NULL) {
            ssize_t part;
            while ((part = read(fds[0], buf, 32 * 1024)) > 0) {
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
