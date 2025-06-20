/*
 * neoclip - Neovim clipboard provider
 * Last Change:  2025 Jun 20
 * License:      https://unlicense.org
 * URL:          https://github.com/matveyt/neoclip
 */


#ifndef NEO_X11_H
#define NEO_X11_H

#include "neoclip_nix.h"
#include <limits.h>
#include <time.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>

#ifdef WITH_THREADS
#include <pthread.h>
#endif // WITH_THREADS


// X11 atoms
enum {
    // sel_prim,        // PRIMARY
    // sel_sec,         // SECONDARY
    // sel_clip,        // CLIPBOARD
    atom = sel_total,   // ATOM
    atom_pair,          // ATOM_PAIR
    clipman,            // CLIPBOARD_MANAGER
    incr,               // INCR
    integer,            // INTEGER
    null,               // NULL
    wm_proto,           // WM_PROTOCOLS
    wm_dele,            // WM_DELETE_WINDOW
    neo_ready,          // NEO_READY
    neo_offer,          // NEO_OFFER
    // supported targets
    targets,            // TARGETS
    dele,               // DELETE
    multi,              // MULTIPLE
    save,               // SAVE_TARGETS
    timestamp,          // TIMESTAMP
    // encodings from best to worst
    vimenc,             // _VIMENC_TEXT
    vimtext,            // _VIM_TEXT
    plain_utf8,         // text/plain;charset=utf-8
    utf8_string,        // UTF8_STRING
    plain,              // text/plain
    compound,           // COMPOUND_TEXT
    string,             // STRING
    text,               // TEXT
    // total count
    total
};

// driver state
struct neo_X {
    Display* d;                         // X Display
    Window w;                           // X Window
    Time delta;                         // X server startup time (ms from Unix epoch)
    Atom atom[total];                   // X Atoms list
    uint8_t* data[sel_total];           // Selection: _VIMENC_TEXT
    size_t cb[sel_total];               // Selection: text size only
    Time stamp[sel_total];              // Selection: time stamp
    bool f_rdy[sel_total];              // Selection: "ready" flag
#ifdef WITH_THREADS
    pthread_cond_t c_rdy[sel_total];    // Selection: "ready" condition
    pthread_mutex_t lock;               // Mutex lock
    pthread_t tid;                      // Thread ID
#endif // WITH_THREADS
};

static int neo__gc(lua_State* L);
static bool dispatch_event(neo_X* x, XEvent* xe);
static void on_sel_notify(neo_X* x, XSelectionEvent* xse);
static void on_sel_request(neo_X* x, XSelectionRequestEvent* xsre);
static size_t alloc_data(neo_X* x, int sel, size_t cb);
static void ask_timestamp(neo_X* x);
static int atom2sel(neo_X* x, Atom atom);
static Atom best_target(neo_X* x, Atom* atom, size_t count);
static Bool is_incr_notify(Display* d, XEvent* xe, XPointer arg);
static Time time_diff(Time ref);
static void to_multiple(neo_X* x, int sel, XSelectionEvent* xse);
static void to_property(neo_X* x, int sel, Window w, Atom property, Atom type);

#ifdef WITH_LUV
static int cb_prepare(lua_State* L);
static int cb_poll(lua_State* L);
static void modal_loop(lua_State* L, bool* stop, uint32_t timeout);
#endif // WITH_LUV

#ifdef WITH_THREADS
static void* thread_main(void* X);
static bool on_client_message(neo_X* X, XClientMessageEvent* xcme);
static void client_message(neo_X* x, int message, int param);
#endif // WITH_THREADS


// inline helpers
static inline bool neo_lock(neo_X* x)
{
#ifdef WITH_THREADS
    return (pthread_mutex_lock(&x->lock) == 0);
#else
    (void)x;    // unused
    return true;
#endif // WITH_THREADS
}
static inline bool neo_unlock(neo_X* x)
{
#ifdef WITH_THREADS
    return (pthread_mutex_unlock(&x->lock) == 0);
#else
    (void)x;    // unused
    return true;
#endif // WITH_THREADS
}
static inline bool neo_signal(neo_X* x, int sel)
{
    x->f_rdy[sel] = true;
#ifdef WITH_THREADS
    return (pthread_cond_signal(&x->c_rdy[sel]) == 0);
#else
    return true;
#endif // WITH_THREADS
}


#endif // NEO_X11_H
