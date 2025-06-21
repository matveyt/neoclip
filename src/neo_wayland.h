/*
 * neoclip - Neovim clipboard provider
 * Last Change:  2025 Jun 21
 * License:      https://unlicense.org
 * URL:          https://github.com/matveyt/neoclip
 */


#if !defined(NEO_WAYLAND_H)
#define NEO_WAYLAND_H

#include "neoclip_nix.h"
#include <wayland-client-core.h>
#include <wayland-ext-data-control-client-protocol.h>
#include <wayland-wlr-data-control-client-protocol.h>

#if defined(WITH_THREADS)
#include <pthread.h>
#endif // WITH_THREADS

// simplify Wayland listeners declaration
#define INDEX(ix) INDEX_##ix
#define LISTEN              \
    const struct {          \
        const size_t count; \
        void* pimpl;        \
    } const _listen[] =
#define OBJECT(o, ix) [ix] = {                                          \
    .count = sizeof(struct o##_listener) / sizeof(WAYLAND_LISTENER),    \
    .pimpl = &(struct o##_listener)


enum {
    INDEX(registry),
    INDEX(device),
    INDEX(offer),
    INDEX(source_prim),
    INDEX(source_clip),
};

typedef void (*WAYLAND_LISTENER)(void);

// driver state
struct neo_X {
    struct wl_display* d;                       // Wayland display
    struct wl_seat* seat;                       // Wayland seat
    struct ext_data_control_manager_v1* dcm;    // ext data control manager
    struct ext_data_control_device_v1* dcd;     // ext data control device
    const struct wl_interface* dcd_iface;       // ext or zwlr
    const struct wl_interface* dcs_iface;       // ext or zwlr
    uint8_t* data[sel_total];                   // Selection: _VIMENC_TEXT
    size_t cb[sel_total];                       // Selection: text size only
#if defined(WITH_THREADS)
    pthread_mutex_t lock;                       // Mutex lock
    pthread_t tid;                              // Thread ID
#endif // WITH_THREADS
};

// Wayland listeners
static void registry_global(void* X, struct wl_registry* registry, uint32_t name,
    const char* interface, uint32_t version);
static void data_control_device_data_offer(void* X,
    struct ext_data_control_device_v1* dcd, struct ext_data_control_offer_v1* offer);
static void data_control_device_primary_selection(void* X,
    struct ext_data_control_device_v1* dcd, struct ext_data_control_offer_v1* offer);
static void data_control_device_selection(void* X,
    struct ext_data_control_device_v1* dcd, struct ext_data_control_offer_v1* offer);
static void data_control_device_finished(void* X,
    struct ext_data_control_device_v1* dcd);
static void data_control_offer_offer(void* X, struct ext_data_control_offer_v1* offer,
    const char* mime_type);
static void data_control_source_send_prim(void* X,
    struct ext_data_control_source_v1* dcs, const char* mime_type, int fd);
static void data_control_source_send_clip(void* X,
    struct ext_data_control_source_v1* dcs, const char* mime_type, int fd);
static void data_control_source_cancelled(void* X,
    struct ext_data_control_source_v1* dcs);

static size_t alloc_data(neo_X* x, int sel, size_t cb);
static int dispatch_event(struct wl_display* d, bool valid);
static int prepare_event(struct wl_display* d);
static void sel_read(neo_X* x, int sel, struct ext_data_control_offer_v1* offer);
static void sel_write(neo_X* x, int sel, const char* mime_type, int fd);
static void* offer_read(neo_X* x, struct ext_data_control_offer_v1* offer,
    const char* mime, size_t* pcb);

#if defined(WITH_LUV)
static int cb_prepare(lua_State* L);
static int cb_poll(lua_State* L);
#endif // WITH_LUV

#if defined(WITH_THREADS)
static void* thread_main(void* X);
#endif // WITH_THREADS

// inline helpers
static inline bool neo_lock(neo_X* x)
{
#if defined(WITH_THREADS)
    return (pthread_mutex_lock(&x->lock) == 0);
#else
    (void)x;    // unused
    return true;
#endif // WITH_THREADS
}
static inline bool neo_unlock(neo_X* x)
{
#if defined(WITH_THREADS)
    return (pthread_mutex_unlock(&x->lock) == 0);
#else
    (void)x;    // unused
    return true;
#endif // WITH_THREADS
}
static inline void* get_data_device(struct neo_X* x)
{
    return wl_proxy_marshal_constructor((struct wl_proxy*)x->dcm,
        EXT_DATA_CONTROL_MANAGER_V1_GET_DATA_DEVICE, x->dcd_iface, NULL, x->seat);
}
static inline void* create_data_source(struct neo_X* x)
{
    return wl_proxy_marshal_constructor((struct wl_proxy*)x->dcm,
        EXT_DATA_CONTROL_MANAGER_V1_CREATE_DATA_SOURCE, x->dcs_iface, NULL);
}


#endif // NEO_WAYLAND_H
