/*
 * neoclip - Neovim clipboard provider
 * Last Change:  2024 Aug 08
 * License:      https://unlicense.org
 * URL:          https://github.com/matveyt/neoclip
 */


#include "neoclip_nix.h"
#include <limits.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>


// context structure
typedef struct {
    Display* d;                 // X Display
    Window w;                   // X Window
    Time delta;                 // X server startup time (ms from Unix epoch)
    Atom atom[total];           // X Atoms list
    Atom notify_targets;        // response type for TARGETS (ATOM or TARGETS)
                                // see https://www.edwardrosten.com/code/x11.html
    unsigned char* data[2];     // Selection: _VIMENC_TEXT
    size_t cb[2];               // Selection: text size only
    Time stamp[2];              // Selection: time stamp
    pthread_cond_t c_rdy[2];    // Selection: "ready" condition
    int f_rdy[2];               // Selection: "ready" flag
    pthread_mutex_t lock;       // Mutex lock
    pthread_t tid;              // Thread ID
} neo_X;


// forward prototypes
static void* thread_main(void* X);
static Bool on_sel_notify(neo_X* x, XSelectionEvent* xse);
static Bool on_sel_request(neo_X* x, XSelectionRequestEvent* xsre);
static Bool on_client_message(neo_X* x, XClientMessageEvent* xcme);
static void alloc_data(neo_X* x, int ix_sel, size_t cb);
static Atom best_target(neo_X* x, Atom* atom, int count);
static void client_message(neo_X* x, int message, int param);
static Bool is_incr_notify(Display* d, XEvent* xe, XPointer arg);
static Time time_stamp(Time ref);
static void to_multiple(neo_X* x, int ix_sel, XSelectionEvent* xse);
static void to_property(neo_X* x, int ix_sel, Window w, Atom property, Atom type);


// init context and start thread
void* neo_create(int first_run, int targets_atom, const char** perr)
{
    // initialize X threads (required for xcb)
    if (first_run && XInitThreads() == False) {
        *perr = "XInitThreads failed";
        return NULL;
    }

    // try to open display
    Display* d = XOpenDisplay(NULL);
    if (d == NULL) {
        *perr = "XOpenDisplay failed";
        return NULL;
    }

    // atom names
    static /*const*/ char* atom_name[total] = {
        [prim] = "PRIMARY",
        [clip] = "CLIPBOARD",
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
        [utf8] = "UTF8_STRING",
        [plain] = "text/plain",
        [compound] = "COMPOUND_TEXT",
        [string] = "STRING",
        [text] = "TEXT",
    };

    // context
    neo_X* x = calloc(1, sizeof(neo_X));
    x->d = d;
    x->w = XCreateSimpleWindow(d, DefaultRootWindow(d), 0, 0, 1, 1, 0, 0, 0);
    XInternAtoms(d, atom_name, total, 0, x->atom);
    x->notify_targets = targets_atom ? x->atom[atom] : x->atom[targets];
    XSetWMProtocols(d, x->w, &x->atom[wm_dele], 1);
    pthread_cond_init(&x->c_rdy[0], NULL);
    pthread_cond_init(&x->c_rdy[1], NULL);
    pthread_mutex_init(&x->lock, NULL);
    pthread_create(&x->tid, NULL, thread_main, x);

    return x;
}


// destroy context
void neo_kill(void* X)
{
    if (X != NULL) {
        neo_X* x = (neo_X*)X;

        client_message(x, wm_proto, wm_dele);
        pthread_join(x->tid, NULL);
        pthread_mutex_destroy(&x->lock);
        pthread_cond_destroy(&x->c_rdy[0]);
        pthread_cond_destroy(&x->c_rdy[1]);
        free(x->data[0]);
        free(x->data[1]);
        XDestroyWindow(x->d, x->w);
        XCloseDisplay(x->d);
        free(x);
    }
}


// lock or unlock selection data
int neo_lock(void* X, int lock)
{
    neo_X* x = (neo_X*)X;
    return lock ? pthread_mutex_lock(&x->lock) : pthread_mutex_unlock(&x->lock);
}


// fetch new selection
// note: caller must unlock unless NULL is returned
const void* neo_fetch(void* X, int sel, size_t* pcb, int* ptype)
{
    if (!neo_lock(X, 1)) {
        neo_X* x = (neo_X*)X;
        int ix_sel = (sel == prim) ? 0 : 1;

        // send request
        x->f_rdy[ix_sel] = 0;
        client_message(x, neo_ready, sel);

        // wait upto one second
        struct timespec t = { 0, 0 };
        clock_gettime(CLOCK_REALTIME, &t); ++t.tv_sec;
        while (!x->f_rdy[ix_sel]
            && !pthread_cond_timedwait(&x->c_rdy[ix_sel], &x->lock, &t))
            /*nothing*/;

        // success
        if (x->f_rdy[ix_sel] && x->cb[ix_sel] > 0) {
            *pcb = x->cb[ix_sel];
            *ptype = x->data[ix_sel][0];
            return (x->data[ix_sel] + 1 + sizeof("utf-8"));
        }

        // unlock on error or clipboard is empty
        neo_lock(X, 0);
    }

    return NULL;
}


// own new selection
// cb = 0 -- empty selection, type < 0 -- keep old selection
void neo_own(void* X, int offer, int sel, const void* ptr, size_t cb, int type)
{
    if (!neo_lock(X, 1)) {
        neo_X* x = (neo_X*)X;
        int ix_sel = (sel == prim) ? 0 : 1;

        // set new data
        if (type >= 0) {
            // _VIMENC_TEXT: type 'encoding' NUL text
            alloc_data(x, ix_sel, cb);
            if (cb) {
                x->data[ix_sel][0] = type;
                memcpy(x->data[ix_sel] + 1, "utf-8", sizeof("utf-8"));
                memcpy(x->data[ix_sel] + 1 + sizeof("utf-8"), ptr, cb);
            }
            x->stamp[ix_sel] = time_stamp(x->delta);
        }

        if (offer) {
            client_message(x, neo_offer, sel);
        } else {
            // signal data ready
            x->f_rdy[ix_sel] = 1;
            pthread_cond_signal(&x->c_rdy[ix_sel]);
        }

        neo_lock(X, 0);
    }
}


// thread entry point
static void* thread_main(void* X)
{
    neo_X* x = (neo_X*)X;
    XSelectInput(x->d, x->w, PropertyChangeMask);

    // force property change to get timestamp from X server
    XChangeProperty(x->d, x->w, x->atom[timestamp], x->atom[timestamp], 32,
        PropModeAppend, NULL, 0);

    Bool ok = True;
    do {
        XEvent xe;
        XNextEvent(x->d, &xe);
        switch (xe.type) {
        case PropertyNotify:
            if (xe.xproperty.atom == x->atom[timestamp])
                x->delta = time_stamp(xe.xproperty.time);
        break;
        case SelectionNotify:
            ok = on_sel_notify(x, &xe.xselection);
        break;
        case SelectionRequest:
            ok = on_sel_request(x, &xe.xselectionrequest);
        break;
        case ClientMessage:
            ok = on_client_message(x, &xe.xclient);
        break;
        }
    } while (ok);

    return NULL;
}


// SelectionNotify event handler
static Bool on_sel_notify(neo_X* x, XSelectionEvent* xse)
{
    int sel = (xse->selection == x->atom[prim]) ? prim : clip;

    if (xse->property == x->atom[neo_ready]) {
        // read our property
        Atom type = None;
        unsigned char* ptr = NULL;
        unsigned char* xptr = NULL;
        unsigned long cxptr = 0;
        XGetWindowProperty(x->d, x->w, x->atom[neo_ready], 0, LONG_MAX, True,
            AnyPropertyType, &type, &(int){0}, &cxptr, &(unsigned long){0}, &xptr);

        do {
            unsigned char* buf = xptr;
            size_t cb = cxptr;

            if (type == x->atom[incr]) {
                // INCR
                for (cb = 0; ; cb += cxptr) {
                    XFree(xptr);
                    XIfEvent(x->d, &(XEvent){0}, is_incr_notify, (XPointer)xse);
                    XGetWindowProperty(x->d, x->w, x->atom[neo_ready], 0, LONG_MAX,
                        True, AnyPropertyType, &(Atom){None}, &(int){0}, &cxptr,
                        &(unsigned long){0}, &xptr);
                    if (!cxptr)
                        break;
                    ptr = realloc(ptr, cb + cxptr);
                    memcpy(ptr + cb, xptr, cxptr);
                }
                type = xse->target;
                buf = ptr;
            }

            if (!cb) {
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
                    && !memcmp(buf + 1, "utf-8", sizeof("utf-8"))) {
                    // this is UTF-8, hurray!
                    neo_own(x, 0, sel, buf + 1 + sizeof("utf-8"),
                        cb - 1 - sizeof("utf-8"), buf[0]);
                } else {
                    // no UTF-8, sigh... ask for UTF8_STRING then
                    XConvertSelection(x->d, xse->selection, x->atom[utf8],
                        x->atom[neo_ready], x->w, xse->time);
                }
                break;
            } else if (type == x->atom[vimtext]) {
                // _VIM_TEXT: assume UTF-8
                neo_own(x, 0, sel, buf + 1, cb - 1, buf[0]);
                break;
            } else if (type == x->atom[plain_utf8] || type == x->atom[utf8]
                || type == x->atom[plain]) {
                // no conversion
                neo_own(x, 0, sel, buf, cb, 255);
                break;
            } else if (type == x->atom[compound] || type == x->atom[string]
                || type == x->atom[text]) {
                // COMPOUND_TEXT, STRING or TEXT: attempt to convert to UTF-8
                XTextProperty xtp = {
                    .value = buf,
                    .encoding = type,
                    .format = 8,
                    .nitems = cb
                };
                char** list;
                if (Xutf8TextPropertyToTextList(x->d, &xtp, &list, &(int){0})
                    == Success) {
                    neo_own(x, 0, sel, list[0], strlen(list[0]), 255);
                    XFreeStringList(list);
                    break;
                }
            }

            // conversion failed
            neo_own(x, 0, sel, NULL, 0, 0);
        } while (0);

        free(ptr);
        if (xptr != NULL)
            XFree(xptr);
    } else if (xse->property == None) {
        // exit upon SAVE_TARGETS: anyone supporting this?
        if (xse->target == x->atom[save])
            return False;
        // peer error
        neo_own(x, 0, sel, NULL, 0, 0);
    }

    return True;
}


// SelectionRequest event handler
static Bool on_sel_request(neo_X* x, XSelectionRequestEvent* xsre)
{
    // prepare SelectionNotify
    XSelectionEvent xse;
    xse.type = SelectionNotify;
    xse.requestor = xsre->requestor;
    xse.selection = xsre->selection;
    xse.target = xsre->target;
    xse.property = xsre->property ? xsre->property : xsre->target;
    xse.time = xsre->time;

    if (!neo_lock(x, 1)) {
        int ix_sel = (xse.selection == x->atom[prim]) ? 0 : 1;

        // TARGETS: DELETE, MULTIPLE, SAVE_TARGETS, TIMESTAMP, _VIMENC_TEXT, _VIM_TEXT,
        // UTF8_STRING, COMPOUND_TEXT, STRING, TEXT
        if (xse.time != CurrentTime && xse.time < x->stamp[ix_sel]) {
            // refuse request for non-matching timestamp
            xse.property = None;
        } else if (xse.target == x->atom[targets]) {
            xse.target = x->notify_targets;
            XChangeProperty(x->d, xse.requestor, xse.property, xse.target, 32,
                PropModeReplace, (unsigned char*)&x->atom[targets], total - targets);
        } else if (xse.target == x->atom[dele] || xse.target == x->atom[save]) {
            // response type is NULL
            if (xse.target == x->atom[dele])
                alloc_data(x, ix_sel, 0);
            xse.target = x->atom[null];
            XChangeProperty(x->d, xse.requestor, xse.property, xse.target, 32,
                PropModeReplace, NULL, 0);
        } else if (xse.target == x->atom[multi]) {
            // response type is ATOM_PAIR
            xse.target = x->atom[atom_pair];
            to_multiple(x, ix_sel, &xse);
        } else if (xse.target == x->atom[timestamp]) {
            // response type is INTEGER
            xse.target = x->atom[integer];
            XChangeProperty(x->d, xse.requestor, xse.property, xse.target, 32,
                PropModeReplace, (unsigned char*)&x->stamp[ix_sel], 1);
        } else if (best_target(x, &xse.target, 1) != None) {
            // attempt to convert
            to_property(x, ix_sel, xse.requestor, xse.property, xse.target);
        } else {
            // unknown target
            xse.property = None;
        }
        neo_lock(x, 0);
    } else
        xse.property = None;

    // send SelectionNotify
    return XSendEvent(x->d, xse.requestor, 1, 0, (XEvent*)&xse) ? True : False;
}


// ClientMessage event handler
static Bool on_client_message(neo_X* x, XClientMessageEvent* xcme)
{
    Atom param = (Atom)xcme->data.l[0];

    if (xcme->message_type == x->atom[neo_ready]) {
        // NEO_READY: fetch system selection
        Window owner = XGetSelectionOwner(x->d, param);
        if (owner == 0 || owner == x->w) {
            // no conversion needed
            int sel = (param == x->atom[prim]) ? prim : clip;
            neo_own(x, 0, sel, NULL, 0, owner ? -1 : 0);
        } else {
            // what TARGETS are supported?
            XConvertSelection(x->d, param, x->atom[targets], x->atom[neo_ready], x->w,
                (Time)xcme->data.l[1]);
        }
    } else if (xcme->message_type == x->atom[neo_offer]) {
        // NEO_OFFER: offer our selection
        int ix_sel = (param == x->atom[prim]) ? 0 : 1;
        XSetSelectionOwner(x->d, param, x->cb[ix_sel] ? x->w : None,
            x->cb[ix_sel] ? x->stamp[ix_sel] : CurrentTime);
    } else if (xcme->message_type == x->atom[wm_proto] && param == x->atom[wm_dele]) {
        // WM_DELETE_WINDOW
        if (x->w != XGetSelectionOwner(x->d, x->atom[prim])
            && x->w != XGetSelectionOwner(x->d, x->atom[clip]))
            return False;
        // ask CLIPBOARD_MANAGER to SAVE_TARGETS first
        XConvertSelection(x->d, x->atom[clipman], x->atom[save], None, x->w,
            (Time)xcme->data.l[1]);
    }

    return True;
}


// (re-)allocate data buffer for selection
// Note: caller must acquire neo_lock() first
static void alloc_data(neo_X* x, int ix_sel, size_t cb)
{
    x->data[ix_sel] = realloc(x->data[ix_sel], cb ? 1 + sizeof("utf-8") + cb : 0);
    x->cb[ix_sel] = cb;
}


// best matching target atom
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
    XClientMessageEvent xcme;
    xcme.type = ClientMessage;
    xcme.display = x->d;
    xcme.window = x->w;
    xcme.message_type = x->atom[message];
    xcme.format = 32;
    xcme.data.l[0] = x->atom[param];
    xcme.data.l[1] = time_stamp(x->delta);
    XSendEvent(x->d, x->w, 0, 0, (XEvent*)&xcme);
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

    return xpe->window == xse->requestor && xpe->atom == xse->property
        && xpe->time >= xse->time && xpe->state == PropertyNewValue;
}


// get msec difference from reference time
static Time time_stamp(Time ref)
{
    if (ref == CurrentTime)
        return CurrentTime;

    struct timespec t = { 0, 0 };
    clock_gettime(CLOCK_REALTIME, &t);
    return t.tv_sec * 1000 + t.tv_nsec / 1000000 - ref;
}


// process MULTIPLE selection requests
static void to_multiple(neo_X* x, int ix_sel, XSelectionEvent* xse)
{
    Atom* tgt = NULL;
    unsigned long c_tgt = 0;
    XGetWindowProperty(x->d, xse->requestor, xse->property, 0, LONG_MAX, False,
        xse->target, &(Atom){None}, &(int){0}, &c_tgt, &(unsigned long){0},
        (unsigned char**)&tgt);

    for (int ix = 0; ix < (int)c_tgt; ix += 2)
        if (best_target(x, &tgt[ix], 1) != None && tgt[ix + 1] != None)
            to_property(x, ix_sel, xse->requestor, tgt[ix + 1], tgt[ix]);
        else
            tgt[ix + 1] = None;

    if (c_tgt) {
        XChangeProperty(x->d, xse->requestor, xse->property, xse->target, 32,
            PropModeReplace, (unsigned char*)tgt, (int)c_tgt);
        XFree(tgt);
    }
}


// put selection data into window property
static void to_property(neo_X* x, int ix_sel, Window w, Atom property, Atom type)
{
    if (!x->cb[ix_sel]) {
        XDeleteProperty(x->d, w, property);
        return;
    }

    XTextProperty xtp = {
        .value = x->data[ix_sel],
        .encoding = type,
        .format = 8,
        .nitems = x->cb[ix_sel]
    };
    unsigned char* ptr = NULL;
    unsigned char* xptr = NULL;

    if (type == x->atom[vimenc]) {
        // _VIMENC_TEXT: type 'encoding' NUL text
        xtp.nitems += 1 + sizeof("utf-8");
    } else if (type == x->atom[vimtext]) {
        // _VIM_TEXT: type text
        ptr = malloc(1 + xtp.nitems);
        ptr[0] = xtp.value[0];
        memcpy(ptr + 1, xtp.value + 1 + sizeof("utf-8"), xtp.nitems);
        xtp.value = ptr;
        ++xtp.nitems;
    } else {
        // skip header
        xtp.value += 1 + sizeof("utf-8");
    }

    // Vim-alike behaviour: STRING == UTF8_STRING, TEXT == COMPOUND_TEXT
    if (type == x->atom[compound] || type == x->atom[text]) {
        // convert UTF-8 to COMPOUND_TEXT
        ptr = memcpy(malloc(xtp.nitems + 1), xtp.value, xtp.nitems);
        ptr[xtp.nitems] = 0;
        Xutf8TextListToTextProperty(x->d, (char**)&ptr, 1, XCompoundTextStyle, &xtp);
        xptr = xtp.value;
    }

    // set property
    XChangeProperty(x->d, w, property, type, xtp.format, PropModeReplace, xtp.value,
        (int)xtp.nitems);

    // free memory
    free(ptr);
    if (xptr != NULL)
        XFree(xptr);
}
