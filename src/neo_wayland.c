/*
 * neoclip - Neovim clipboard provider
 * Last Change:  2021 May 30
 * License:      https://unlicense.org
 * URL:          https://github.com/matveyt/neoclip
 */


#include "neoclip_x.h"
#include <poll.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
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
#define COUNT(obj) (int)(sizeof(obj) / sizeof(obj[0]))
#define INDEX(ix) INDEX##ix
#define LISTEN \
    static struct { \
        const int count; \
        void* impl; \
    } _listen[] =
#define OBJECT(ix, obj) [INDEX(ix)] = { \
    .count = sizeof(struct obj##_listener) / sizeof(void(*)(void)), \
    .impl = &(struct obj##_listener)


enum {
    INDEX(registry),
    INDEX(device),
    INDEX(offer),
    INDEX(source_prim),
    INDEX(source_clip),
};


LISTEN {
    OBJECT(registry, wl_registry) {
        .global = registry_global,
    }},
    OBJECT(device, zwlr_data_control_device_v1) {
        .data_offer = data_control_device_data_offer,
        .primary_selection = data_control_device_primary_selection,
        .selection = data_control_device_selection,
        .finished = data_control_device_finished,
    }},
    OBJECT(offer, zwlr_data_control_offer_v1) {
        .offer = data_control_offer_offer,
    }},
    OBJECT(source_prim, zwlr_data_control_source_v1) {
        .send = data_control_source_send_prim,
        .cancelled = data_control_source_cancelled,
    }},
    OBJECT(source_clip, zwlr_data_control_source_v1) {
        .send = data_control_source_send_clip,
        .cancelled = data_control_source_cancelled,
    }},
};


static void _nop(void) {}
static inline void listen_init(void)
{
    for (int i = 0; i < COUNT(_listen); ++i)
        for (int j = 0; j < _listen[i].count; ++j)
            if (((void(**)(void))_listen[i].impl)[j] == NULL)
                ((void(**)(void))_listen[i].impl)[j] = _nop;
}


static inline int listen_to(void* object, int index, void* data)
{
    return wl_proxy_add_listener(object, _listen[index].impl, data);
}


// context structure
typedef struct {
    struct wl_display* d;                       // Wayland display
    struct wl_seat* seat;                       // Wayland seat
    struct zwlr_data_control_manager_v1* dcm;   // wlroots data control manager
    struct zwlr_data_control_device_v1* dcd;    // wlroots data control device
    unsigned char* data[2];                     // Selection: _VIMENC_TEXT
    size_t cb[2];                               // Selection: text size only
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
static void sel_read(neo_W* w, int sel, struct zwlr_data_control_offer_v1* offer);
static void sel_write(neo_W* w, int sel, const char* mime_type, int fd);
static void* offer_read(neo_W* w, struct zwlr_data_control_offer_v1* offer,
    const char* mime, size_t* pcb);


// init context and start thread
void* neo_create(void)
{
    // try to open display first
    struct wl_display* d = wl_display_connect(NULL);
    if (d == NULL)
        return NULL;

    // context
    neo_W* w = calloc(1, sizeof(neo_W));
    w->d = d;
    listen_init();

    // read globals from registry
    struct wl_registry* reg = wl_display_get_registry(d);
    listen_to(reg, INDEX(registry), w);
    wl_display_roundtrip(d);
    wl_registry_destroy(reg);
    if (w->seat == NULL || w->dcm == NULL) {
        // not supported
        free(w);
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
int neo_lock(void* X, int lock)
{
    neo_W* w = (neo_W*)X;
    return lock ? pthread_mutex_lock(&w->lock) : pthread_mutex_unlock(&w->lock);
}


// fetch new selection
// note: caller must unlock unless NULL is returned
const void* neo_fetch(void* X, int sel, size_t* pcb, int* ptype)
{
    if (!neo_lock(X, 1)) {
        neo_W* w = (neo_W*)X;
        int ix_sel = (sel == prim) ? 0 : 1;

        // wlr_data_control_device should've informed us of new selection
        if (w->cb[ix_sel] > 0) {
            *pcb = w->cb[ix_sel];
            *ptype = w->data[ix_sel][0];
            return (w->data[ix_sel] + 1 + sizeof("utf-8"));
        }

        // unlock if clipboard is empty
        neo_lock(X, 0);
    }

    return NULL;
}


// own new selection
void neo_own(void* X, int offer, int sel, const void* ptr, size_t cb, int type)
{
    if (!neo_lock(X, 1)) {
        neo_W* w = (neo_W*)X;
        int ix_sel = (sel == prim) ? 0 : 1;

        // _VIMENC_TEXT: motion 'encoding' NUL text
        w->data[ix_sel] = realloc(w->data[ix_sel], cb ? 1 + sizeof("utf-8") + cb : 0);
        w->cb[ix_sel] = cb;
        if (cb) {
            w->data[ix_sel][0] = type;
            memcpy(w->data[ix_sel] + 1, "utf-8", sizeof("utf-8"));
            memcpy(w->data[ix_sel] + 1 + sizeof("utf-8"), ptr, cb);
        }

        if (offer) {
            // offer our selection
            struct zwlr_data_control_source_v1* dcs =
                zwlr_data_control_manager_v1_create_data_source(w->dcm);
            for (int i = 0; i < COUNT(mime); ++i)
                zwlr_data_control_source_v1_offer(dcs, mime[i]);
            if (sel == prim) {
                listen_to(dcs, INDEX(source_prim), w);
                zwlr_data_control_device_v1_set_primary_selection(w->dcd, dcs);
            } else {
                listen_to(dcs, INDEX(source_clip), w);
                zwlr_data_control_device_v1_set_selection(w->dcd, dcs);
            }
            wl_display_flush(w->d);
        }

        neo_lock(X, 0);
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
        { .fd = wl_display_get_fd(w->d), .events = POLLIN },
        { .fd = signalfd(-1, &mask, 0), .events = POLLIN },
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
            read(fds[1].fd, &ssi, sizeof(ssi));
            if (ssi.ssi_signo == SIGINT || ssi.ssi_signo == SIGTERM)
                break;
        }
    }

    close(fds[1].fd);
    return NULL;
}


// wl_registry::global
static void registry_global(void* X, struct wl_registry* registry, uint32_t name,
    const char* interface, uint32_t version)
{
    neo_W* w = (neo_W*)X;
    struct {
        void** pobj;
        const struct wl_interface* iface;
    } globl[] = {
        { (void**)&w->seat, &wl_seat_interface },
        { (void**)&w->dcm, &zwlr_data_control_manager_v1_interface },
    };

    for (int i = 0; i < COUNT(globl); ++i) {
        if (!strcmp(interface, globl[i].iface->name)) {
            if (*globl[i].pobj == NULL)
                *globl[i].pobj = wl_registry_bind(registry, name, globl[i].iface,
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

    int* best_mime = malloc(sizeof(int));
    *best_mime = COUNT(mime);
    listen_to(offer, INDEX(offer), best_mime);
}


// [z]wlr_data_control_device_[v1]::primary_selection
static void data_control_device_primary_selection(void* X,
    struct zwlr_data_control_device_v1* dcd, struct zwlr_data_control_offer_v1* offer)
{
    (void)dcd;  // unused
    sel_read((neo_W*)X, prim, offer);
}


// [z]wlr_data_control_device_[v1]::selection
static void data_control_device_selection(void* X,
    struct zwlr_data_control_device_v1* dcd, struct zwlr_data_control_offer_v1* offer)
{
    (void)dcd;  // unused
    sel_read((neo_W*)X, clip, offer);
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
    (void)offer;    // unused

    int best = *(int*)X;
    for (int i = 0; i < best; ++i) {
        if (!strcmp(mime_type, mime[i])) {
            best = i;
            break;
        }
    }
    *(int*)X = best;
}


// [z]wlr_data_control_source_[v1]::send
static void data_control_source_send_prim(void* X,
    struct zwlr_data_control_source_v1* dcs, const char* mime_type, int fd)
{
    (void)dcs;   // unused
    sel_write((neo_W*)X, prim, mime_type, fd);
}


// [z]wlr_data_control_source_[v1]::send
static void data_control_source_send_clip(void* X,
    struct zwlr_data_control_source_v1* dcs, const char* mime_type, int fd)
{
    (void)dcs;   // unused
    sel_write((neo_W*)X, clip, mime_type, fd);
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
        neo_own(w, 0, sel, NULL, 0, 0);
        return;
    }

    int* best_mime = wl_proxy_get_user_data((struct wl_proxy*)offer);
    if (best_mime != NULL && *best_mime >= 0 && *best_mime < COUNT(mime)) {
        size_t cb;
        unsigned char* ptr = offer_read(w, offer, mime[*best_mime], &cb);
        int type = (cb && *best_mime <= 1) ? ptr[0] : 255;

        void *data = ptr;
        if (!cb) {
            // nothing to do
        } else if (*best_mime == 0) {
            // _VIMENC_TEXT
            if (cb >= 1 + sizeof("utf-8")
                && !memcmp(ptr + 1, "utf-8", sizeof("utf-8"))) {
                // this is UTF-8
                data = ptr + 1 + sizeof("utf-8");
                cb -= 1 + sizeof("utf-8");
            } else {
                // Vim should also support UTF8_STRING
                free(ptr);
                data = ptr = offer_read(w, offer, "UTF8_STRING", &cb);
            }
        } else if (*best_mime == 1) {
            // _VIM_TEXT
            data = ptr + 1;
            cb--;
        }

        neo_own(w, 0, sel, data, cb, type);
        free(ptr);
    }

    free(best_mime);
    zwlr_data_control_offer_v1_destroy(offer);
}


// write selection data to file descriptor
static void sel_write(neo_W* w, int sel, const char* mime_type, int fd)
{
    int ix_sel = (sel == prim) ? 0 : 1;

    if (!neo_lock(w, 1)) {
        unsigned char* buf;
        size_t cb;

        if (!strcmp(mime_type, mime[0])) {
            // _VIMENC_TEXT
            buf = w->data[ix_sel];
            cb = 1 + sizeof("utf-8") + w->cb[ix_sel];
        } else {
            // _VIM_TEXT
            if (!strcmp(mime_type, mime[1]))
                write(fd, w->data[ix_sel], 1);

            buf = w->data[ix_sel] + 1 + sizeof("utf-8");
            cb = w->cb[ix_sel];
        }

        write(fd, buf, cb);
        neo_lock(w, 0);
    }

    close(fd);
}


// read specific mime type from offer
static void* offer_read(neo_W* w, struct zwlr_data_control_offer_v1* offer,
    const char* mime, size_t* pcb)
{
    int fds[2];
    pipe(fds);
    zwlr_data_control_offer_v1_receive(offer, mime, fds[1]);
    wl_display_roundtrip(w->d);
    close(fds[1]);

    void* ptr = NULL;
    size_t total = 0;
    void* buf = malloc(256 * 1024);
    ssize_t part;

    while ((part = read(fds[0], buf, 256 * 1024)) > 0) {
        ptr = realloc(ptr, total + part);
        memcpy((char*)ptr + total, buf, part);
        total += part;
    }

    free(buf);
    close(fds[0]);
    *pcb = total;
    return ptr;
}
