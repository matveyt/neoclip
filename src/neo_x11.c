/*
 * neoclip - Neovim clipboard provider
 * Last Change:  2024 Aug 23
 * License:      https://unlicense.org
 * URL:          https://github.com/matveyt/neoclip
 */


#include "neoclip_nix.h"
#include <limits.h>
#include <pthread.h>
#include <time.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>


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
    pthread_cond_t c_rdy[sel_total];    // Selection: "ready" condition
    bool f_rdy[sel_total];              // Selection: "ready" flag
    pthread_mutex_t lock;               // Mutex lock
    pthread_t tid;                      // Thread ID
};


// forward prototypes
static int neo__gc(lua_State* L);
static bool neo_lock(neo_X* x);
static bool neo_unlock(neo_X* x);
static bool neo_signal(neo_X* x, int sel);
static void* thread_main(void* X);
static bool on_sel_notify(neo_X* x, XSelectionEvent* xse);
static void on_sel_request(neo_X* x, XSelectionRequestEvent* xsre);
static bool on_client_message(neo_X* X, XClientMessageEvent* xcme);
static size_t alloc_data(neo_X* x, int sel, size_t cb);
static int atom2sel(neo_X* x, Atom atom);
static Atom best_target(neo_X* x, Atom* atom, int count);
static void client_message(neo_X* x, int message, int param);
static Bool is_incr_notify(Display* d, XEvent* xe, XPointer arg);
static Time time_diff(Time ref);
static void to_multiple(neo_X* x, int sel, XSelectionEvent* xse);
static void to_property(neo_X* x, int sel, Window w, Atom property, Atom type);


// init state and start thread
int neo_start(lua_State* L)
{
    neo_X* x = neo_x(L);
    if (x == NULL) {
        // initialize X threads (required for xcb)
        if (!neo_did(L, "XInitThreads")) {
            if (XInitThreads() == False) {
                lua_pushliteral(L, "XInitThreads failed");
                return lua_error(L);
            }
        }

        // create new state
        x = lua_newuserdata(L, sizeof(neo_X));

        // try to open display
        x->d = XOpenDisplay(NULL);
        if (x->d == NULL) {
            lua_pushliteral(L, "XOpenDisplay failed");
            return lua_error(L);
        }

        // atom names
        static /*const*/ char* atom_name[total] = {
            [sel_prim] = "PRIMARY",
            [sel_sec] = "SECONDARY",
            [sel_clip] = "CLIPBOARD",
            [atom] = "ATOM",
            [atom_pair] = "ATOM_PAIR",
            [clipman] = "CLIPBOARD_MANAGER",
            [incr] = "INCR",
            [integer] = "INTEGER",
            [null] = "NULL",
            [wm_proto] = "WM_PROTOCOLS",
            [wm_dele] = "WM_DELETE_WINDOW",
            [neo_ready] = "NEO_READY",
            [neo_offer] = "NEO_OFFER",
            [targets] = "TARGETS",
            [dele] = "DELETE",
            [multi] = "MULTIPLE",
            [save] = "SAVE_TARGETS",
            [timestamp] = "TIMESTAMP",
            [vimenc] = "_VIMENC_TEXT",
            [vimtext] = "_VIM_TEXT",
            [plain_utf8] = "text/plain;charset=utf-8",
            [utf8_string] = "UTF8_STRING",
            [plain] = "text/plain",
            [compound] = "COMPOUND_TEXT",
            [string] = "STRING",
            [text] = "TEXT",
        };

        // init state
        x->w = XCreateSimpleWindow(x->d, XDefaultRootWindow(x->d), 0, 0, 1, 1, 0, 0, 0);
        x->delta = CurrentTime;
        XInternAtoms(x->d, atom_name, total, False, x->atom);
        XSetWMProtocols(x->d, x->w, &x->atom[wm_dele], 1);
        for (int i = 0; i < sel_total; ++i) {
            x->data[i] = NULL;
            x->cb[i] = 0;
            x->stamp[i] = CurrentTime;
            pthread_cond_init(&x->c_rdy[i], NULL);
            x->f_rdy[i] = false;
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
    client_message(x, wm_proto, wm_dele);
    pthread_join(x->tid, NULL);
    pthread_mutex_destroy(&x->lock);
    for (int i = 0; i < sel_total; ++i) {
        pthread_cond_destroy(&x->c_rdy[i]);
        free(x->data[i]);
    }
    XDestroyWindow(x->d, x->w);
    XCloseDisplay(x->d);

    return 0;
}


// fetch new selection
void neo_fetch(lua_State* L, int ix, int sel)
{
    neo_X* x = neo_x(L);
    if (x != NULL && neo_lock(x)) {
        // send request
        x->f_rdy[sel] = false;
        client_message(x, neo_ready, sel);

        // wait upto 1 second
        struct timespec t;
        if (clock_gettime(CLOCK_REALTIME, &t) == 0) {
            ++t.tv_sec;
            while (!x->f_rdy[sel]
                && pthread_cond_timedwait(&x->c_rdy[sel], &x->lock, &t) == 0)
                /*nothing*/;
        }

        // split selection into t[ix]
        if (x->f_rdy[sel] && x->cb[sel] > 0)
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
        x->stamp[sel] = time_diff(x->delta);

        if (offer)
            client_message(x, neo_offer, sel);
        else
            neo_signal(x, sel);

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


// signal data ready
static inline bool neo_signal(neo_X* x, int sel)
{
    x->f_rdy[sel] = true;
    return (pthread_cond_signal(&x->c_rdy[sel]) == 0);
}


// thread entry point
static void* thread_main(void* X)
{
    neo_X* x = (neo_X*)X;

    // force property change to get timestamp from X server
    XSelectInput(x->d, x->w, PropertyChangeMask);
    XChangeProperty(x->d, x->w, x->atom[timestamp], x->atom[timestamp], 32,
        PropModeAppend, NULL, 0);

    bool stop = false;
    do {
        XEvent xe;
        XNextEvent(x->d, &xe);
        switch (xe.type) {
        case PropertyNotify:
            if (xe.xproperty.atom == x->atom[timestamp]) {
                x->delta = time_diff(xe.xproperty.time);
                XSelectInput(x->d, x->w, NoEventMask);
            }
        break;
        case SelectionClear:
            if (xe.xselectionclear.window == x->w && neo_lock(x)) {
                alloc_data(x, atom2sel(x, xe.xselectionclear.selection), 0);
                neo_unlock(x);
            }
        break;
        case SelectionNotify:
            stop = on_sel_notify(x, &xe.xselection);
        break;
        case SelectionRequest:
            on_sel_request(x, &xe.xselectionrequest);
        break;
        case ClientMessage:
            stop = on_client_message(x, &xe.xclient);
        break;
        }
    } while (!stop);

    return NULL;
}


// SelectionNotify event handler
static bool on_sel_notify(neo_X* x, XSelectionEvent* xse)
{
    int sel = atom2sel(x, xse->selection);

    if (xse->property == x->atom[neo_ready]) {
        // read our property
        Atom type = None;
        uint8_t* ptr = NULL;
        unsigned char* xptr = NULL;
        unsigned long cxptr = 0;
        XGetWindowProperty(x->d, x->w, x->atom[neo_ready], 0, LONG_MAX, True,
            AnyPropertyType, &type, &(int){0}, &cxptr, &(unsigned long){0}, &xptr);

        do {
            uint8_t* buf = (uint8_t*)xptr;
            size_t cb = cxptr;

            if (type == x->atom[incr]) {
                // INCR
                for (cb = 0; ; cb += cxptr) {
                    XFree(xptr);
                    XIfEvent(x->d, &(XEvent){0}, is_incr_notify, (XPointer)xse);
                    XGetWindowProperty(x->d, x->w, x->atom[neo_ready], 0, LONG_MAX,
                        True, AnyPropertyType, &(Atom){None}, &(int){0}, &cxptr,
                        &(unsigned long){0}, &xptr);
                    void* ptr2 = cxptr ? realloc(ptr, cb + cxptr) : NULL;
                    if (ptr2 == NULL)
                        break;
                    ptr = ptr2;
                    memcpy(ptr + cb, xptr, cxptr);
                }
                type = xse->target;
                buf = ptr;
            }

            if (cb == 0) {
                // nothing to do
            } else if (type == x->atom[atom] || type == x->atom[targets]) {
                // TARGETS
                Atom target = best_target(x, (Atom*)buf, (int)cb);
                if (target != None) {
                    XConvertSelection(x->d, xse->selection, target, x->atom[neo_ready],
                        x->w, xse->time);
                    break;
                }
            } else if (type == x->atom[vimenc]) {
                // _VIMENC_TEXT
                if (cb >= 1 + sizeof("utf-8")
                    && memcmp(buf + 1, "utf-8", sizeof("utf-8")) == 0) {
                    // this is UTF-8
                    neo_own(x, false, sel, buf + 1 + sizeof("utf-8"),
                        cb - 1 - sizeof("utf-8"), buf[0]);
                } else {
                    // no UTF-8; ask then for UTF8_STRING
                    XConvertSelection(x->d, xse->selection, x->atom[utf8_string],
                        x->atom[neo_ready], x->w, xse->time);
                }
                break;
            } else if (type == x->atom[vimtext]) {
                // _VIM_TEXT: assume UTF-8
                neo_own(x, false, sel, buf + 1, cb - 1, buf[0]);
                break;
            } else if (type == x->atom[plain_utf8] || type == x->atom[utf8_string]
                || type == x->atom[plain]) {
                // no conversion
                neo_own(x, false, sel, buf, cb, MAUTO);
                break;
            } else if (type == x->atom[compound] || type == x->atom[string]
                || type == x->atom[text]) {
                // COMPOUND_TEXT, STRING or TEXT: attempt to convert to UTF-8
                XTextProperty xtp = {
                    .value = buf,
                    .encoding = type,
                    .format = 8,
                    .nitems = cb,
                };
                char** list;
                if (Xutf8TextPropertyToTextList(x->d, &xtp, &list, &(int){0})
                    == Success) {
                    neo_own(x, false, sel, list[0], strlen(list[0]), MAUTO);
                    XFreeStringList(list);
                    break;
                }
            }

            // conversion failed
            neo_own(x, false, sel, NULL, 0, 0);
        } while (0);

        free(ptr);
        if (xptr != NULL)
            XFree(xptr);
    } else if (xse->property == None) {
        // exit upon SAVE_TARGETS: anyone supporting this?
        if (xse->target == x->atom[save])
            return true;
        // peer error
        neo_own(x, false, sel, NULL, 0, 0);
    }

    return false;
}


// SelectionRequest event handler
static void on_sel_request(neo_X* x, XSelectionRequestEvent* xsre)
{
    // prepare SelectionNotify
    XSelectionEvent xse = {
        .type = SelectionNotify,
        .display = x->d,
        .requestor = xsre->requestor,
        .selection = xsre->selection,
        .target = xsre->target,
        .property = xsre->property ? xsre->property : xsre->target,
        .time = time_diff(x->delta),
    };

    if (neo_lock(x)) {
        int sel = atom2sel(x, xsre->selection);

        // TARGETS: DELETE, MULTIPLE, SAVE_TARGETS, TIMESTAMP, _VIMENC_TEXT, _VIM_TEXT,
        // UTF8_STRING, COMPOUND_TEXT, STRING, TEXT
        if (xsre->owner != x->w || (xsre->time != CurrentTime &&
            xsre->time < x->stamp[sel])) {
            // refuse non-matching request
            xse.property = None;
        } else if (xsre->target == x->atom[targets]) {
            // response is ATOM
            XChangeProperty(x->d, xse.requestor, xse.property, x->atom[atom], 32,
                PropModeReplace, (unsigned char*)&x->atom[targets], total - targets);
        } else if (xsre->target == x->atom[dele]) {
            // response is NULL
            alloc_data(x, sel, 0);
            XChangeProperty(x->d, xse.requestor, xse.property, x->atom[null], 32,
                PropModeReplace, NULL, 0);
        } else if (xsre->target == x->atom[save]) {
            // response is NULL
            XChangeProperty(x->d, xse.requestor, xse.property, x->atom[null], 32,
                PropModeReplace, NULL, 0);
        } else if (xsre->target == x->atom[multi]) {
            // response is ATOM_PAIR
            to_multiple(x, sel, &xse);
        } else if (xsre->target == x->atom[timestamp]) {
            // response is INTEGER
            XChangeProperty(x->d, xse.requestor, xse.property, x->atom[integer], 32,
                PropModeReplace, (unsigned char*)&x->stamp[sel], 1);
        } else if (best_target(x, &xsre->target, 1) != None) {
            // attempt to convert
            to_property(x, sel, xse.requestor, xse.property, xsre->target);
        } else {
            // unknown target
            xse.property = None;
        }
        neo_unlock(x);
    } else
        xse.property = None;

    // send SelectionNotify
    XSendEvent(x->d, xse.requestor, True, NoEventMask, (XEvent*)&xse);
}


// ClientMessage event handler
static bool on_client_message(neo_X* x, XClientMessageEvent* xcme)
{
    Atom param = (Atom)xcme->data.l[0];
    int sel = atom2sel(x, param);

    if (xcme->message_type == x->atom[neo_ready]) {
        // NEO_READY: fetch system selection
        Window owner = XGetSelectionOwner(x->d, param);
        if (owner == x->w) {
            // no conversion needed
            if (neo_lock(x)) {
                neo_signal(x, sel);
                neo_unlock(x);
            }
        } else if (owner == None) {
            // empty selection
            neo_own(x, false, sel, NULL, 0, 0);
        } else {
            // what TARGETS are supported?
            XConvertSelection(x->d, param, x->atom[targets], x->atom[neo_ready], x->w,
                (Time)xcme->data.l[1]);
        }
    } else if (xcme->message_type == x->atom[neo_offer]) {
        // NEO_OFFER: offer our selection
        XSetSelectionOwner(x->d, param, x->w, x->stamp[sel]);
    } else if (xcme->message_type == x->atom[wm_proto] && param == x->atom[wm_dele]) {
        // WM_DELETE_WINDOW
        for (int i = 0; i < sel_total; ++i) {
            if (XGetSelectionOwner(x->d, x->atom[i]) == x->w) {
                // ask CLIPBOARD_MANAGER to SAVE_TARGETS first
                XConvertSelection(x->d, x->atom[clipman], x->atom[save], None, x->w,
                    (Time)xcme->data.l[1]);
                return false;
            }
        }
        // stop by WM_DELETE_WINDOW
        return true;
    }

    return false;
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


// Atom => selection index
static int atom2sel(neo_X* x, Atom atom)
{
    for (int i = 0; i < sel_total; ++i)
        if (atom == x->atom[i])
            return i;

    return sel_clip;
}


// get best matching target atom
static Atom best_target(neo_X* x, Atom* atom, int count)
{
    int best = total;

    for (int i = 0; i < count && best > vimenc; ++i)
        for (int j = vimenc; j < best; ++j)
            if (atom[i] == x->atom[j]) {
                best = j;
                break;
            }

    return (best < total) ? x->atom[best] : None;
}


// send ClientMessage to our thread
static void client_message(neo_X* x, int message, int param)
{
    XClientMessageEvent xcme = {
        .type = ClientMessage,
        .display = x->d,
        .window = x->w,
        .message_type = x->atom[message],
        .format = 32,
        .data = {
            .l = {
                [0] = x->atom[param],
                [1] = time_diff(x->delta),
            },
        },
    };
    XSendEvent(x->d, x->w, False, NoEventMask, (XEvent*)&xcme);
    XFlush(x->d);
}


// X11 predicate function: PropertyNotify/PropertyNewValue
static Bool is_incr_notify(Display* d, XEvent* xe, XPointer arg)
{
    (void)d;    // unused
    if (xe->type != PropertyNotify)
        return False;

    XPropertyEvent* xpe = &xe->xproperty;
    XSelectionEvent* xse = (XSelectionEvent*)arg;

    return (xpe->window == xse->requestor && xpe->atom == xse->property
        && xpe->time >= xse->time && xpe->state == PropertyNewValue);
}


// get ms difference from reference time
static Time time_diff(Time ref)
{
    struct timespec t;

    if (ref == CurrentTime || clock_gettime(CLOCK_MONOTONIC, &t) < 0)
        return CurrentTime;

    return (t.tv_sec * 1000 + t.tv_nsec / 1000000) - ref;
}


// process MULTIPLE selection requests
static void to_multiple(neo_X* x, int sel, XSelectionEvent* xse)
{
    Atom* tgt = NULL;
    unsigned long ul_tgt = 0;
    XGetWindowProperty(x->d, xse->requestor, xse->property, 0, LONG_MAX, False,
        x->atom[atom_pair], &(Atom){None}, &(int){0}, &ul_tgt, &(unsigned long){0},
        (unsigned char**)&tgt);
    int i_tgt = (long)ul_tgt;

    for (int i = 0; i < i_tgt; i += 2)
        if (best_target(x, &tgt[i], 1) != None && tgt[i + 1] != None)
            to_property(x, sel, xse->requestor, tgt[i + 1], tgt[i]);
        else
            tgt[i + 1] = None;

    if (i_tgt > 0) {
        XChangeProperty(x->d, xse->requestor, xse->property, x->atom[atom_pair], 32,
            PropModeReplace, (unsigned char*)tgt, i_tgt);
        XFree(tgt);
    }
}


// put selection data into window property
static void to_property(neo_X* x, int sel, Window w, Atom property, Atom type)
{
    if (x->cb[sel] == 0) {
        XDeleteProperty(x->d, w, property);
        return;
    }

    XTextProperty xtp = {
        .value = (unsigned char*)x->data[sel],
        .encoding = type,
        .format = 8,
        .nitems = (unsigned long)x->cb[sel],
    };
    unsigned char* ptr = NULL;
    unsigned char* xptr = NULL;

    if (type == x->atom[vimenc]) {
        // _VIMENC_TEXT: type 'encoding' NUL text
        xtp.nitems += 1 + sizeof("utf-8");
    } else if (type == x->atom[vimtext]) {
        // _VIM_TEXT: type text
        ptr = malloc(1 + xtp.nitems);
        if (ptr != NULL) {
            ptr[0] = xtp.value[0];
            memcpy(ptr + 1, xtp.value + 1 + sizeof("utf-8"), xtp.nitems);
            xtp.value = ptr;
            ++xtp.nitems;
        }
    } else {
        // skip header
        xtp.value += 1 + sizeof("utf-8");
    }

    // Vim-alike behaviour: STRING == UTF8_STRING, TEXT == COMPOUND_TEXT
    if (type == x->atom[compound] || type == x->atom[text]) {
        // convert UTF-8 to COMPOUND_TEXT
        ptr = malloc(xtp.nitems + 1);
        if (ptr != NULL) {
            memcpy(ptr, xtp.value, xtp.nitems);
            ptr[xtp.nitems] = 0;
            Xutf8TextListToTextProperty(x->d, (char**)&ptr, 1, XCompoundTextStyle, &xtp);
            xptr = xtp.value;
        }
    }

    // set property
    XChangeProperty(x->d, w, property, type, xtp.format, PropModeReplace, xtp.value,
        (int)xtp.nitems);

    // free memory
    free(ptr);
    if (xptr != NULL)
        XFree(xptr);
}
