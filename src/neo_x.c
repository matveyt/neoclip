/*
 * neoclip - Neovim clipboard provider
 * Last Change:  2021 May 16
 * License:      https://unlicense.org
 * URL:          https://github.com/matveyt/neoclip
 */


#include "neo_x.h"
#include <limits.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>


// context structure
typedef struct {
    Display* d;                 // X Display
    Window w;                   // X Window
    Atom atom[total];           // X Atoms
    unsigned char* data[2];     // Selection: _VIMENC_TEXT
    size_t cb[2];               // Selection: text size only
    pthread_cond_t c_rdy[2];    // Selection: "ready" condition
    int f_rdy[2];               // Selection: "ready" flag
    pthread_mutex_t lock;       // Mutex lock
    pthread_t tid;              // Thread ID
} neo_X;


// forward prototypes
static void* thread_main(void* X);
static void on_sel_notify(neo_X* x, XSelectionEvent* xse);
static void on_sel_request(neo_X* x, XSelectionRequestEvent* xsre);
static int on_client_message(neo_X* x, XClientMessageEvent* xcme);
static Atom best_target(neo_X* x, Atom* atom, int count);
static void set_property(neo_X* x, int sel, Window w, Atom property, Atom* type);


// init context and start thread
void* neo_X_start(void)
{
    // try to open display first
    const char* display_name = getenv("DISPLAY");
    Display* d = XOpenDisplay(display_name ? display_name : ":0");
    if (d == NULL)
        return NULL;

    // atom names
    static /*const*/ char* atom_name[total] = {
        [prim] = "PRIMARY",
        [clip] = "CLIPBOARD",
        [atom] = "ATOM",
        [proto] = "WM_PROTOCOLS",
        [dele] = "WM_DELETE_WINDOW",
        [neo_update] = "NEO_UPDATE",
        [neo_owned] = "NEO_OWNED",
        [targets] = "TARGETS",
        [vimenc] = "_VIMENC_TEXT",
        [vimtext] = "_VIM_TEXT",
        [utf8] = "UTF8_STRING",
        [compound] = "COMPOUND_TEXT",
        [string] = "STRING",
        [text] = "TEXT"
    };

    // context
    neo_X* x = calloc(1, sizeof(neo_X));
    x->d = d;
    x->w = XCreateSimpleWindow(d, DefaultRootWindow(d), 0, 0, 1, 1, 0, 0, 0);
    XInternAtoms(d, atom_name, total, 0, x->atom);
    XSetWMProtocols(d, x->w, &x->atom[dele], 1);
    pthread_cond_init(&x->c_rdy[0], NULL);
    pthread_cond_init(&x->c_rdy[1], NULL);
    pthread_mutex_init(&x->lock, NULL);
    pthread_create(&x->tid, NULL, thread_main, x);

    return x;
}


// destroy context
void neo_X_cleanup(void* X)
{
    neo_X* x = (neo_X*)X;
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


// lock or unlock selection data
int neo_X_lock(void* X, int lock)
{
    neo_X* x = (neo_X*)X;
    return lock ? pthread_mutex_lock(&x->lock) : pthread_mutex_unlock(&x->lock);
}


// signal selection buffer contents changed
// cb: 0 = empty buffer; SIZE_MAX = keep old, signal only
void neo_X_ready(void* X, int sel, const void* ptr, size_t cb, int type)
{
    if (!neo_X_lock(X, 1)) {
        neo_X* x = (neo_X*)X;
        int ix_sel = (sel == prim) ? 0 : 1;

        // set new data
        if (cb != SIZE_MAX) {
            free(x->data[ix_sel]);
            if (cb) {
                // _VIMENC_TEXT: motion 'encoding' NUL text
                x->data[ix_sel] = malloc(1 + sizeof("utf-8") + cb);
                x->data[ix_sel][0] = type;
                memcpy(x->data[ix_sel] + 1, "utf-8", sizeof("utf-8"));
                memcpy(x->data[ix_sel] + 1 + sizeof("utf-8"), ptr, cb);
                x->cb[ix_sel] = cb;
            } else {
                x->data[ix_sel] = NULL;
                x->cb[ix_sel] = 0;
            }
        }
        // signal data is ready
        x->f_rdy[ix_sel] = 1;
        pthread_cond_signal(&x->c_rdy[ix_sel]);

        neo_X_lock(X, 0);
    }
}


// send ClientMessage to our thread
void neo_X_send(void* X, int message, int param)
{
    neo_X* x = (neo_X*)X;

    XClientMessageEvent xcme;
    xcme.type = ClientMessage;
    xcme.display = x->d;
    xcme.window = x->w;
    xcme.message_type = x->atom[message];
    xcme.format = 32;
    xcme.data.l[0] = x->atom[param];
    xcme.data.l[1] = CurrentTime;

    XSendEvent(x->d, x->w, 0, 0, (XEvent*)&xcme);
    XFlush(x->d);
}


// update selection data from system
// note: caller must unlock unless NULL is returned
const void* neo_X_update(void* X, int sel, size_t* pcb, int* ptype)
{
    if (!neo_X_lock(X, 1)) {
        neo_X* x = (neo_X*)X;
        int ix_sel = (sel == prim) ? 0 : 1;

        // send request
        x->f_rdy[ix_sel] = 0;
        neo_X_send(x, neo_update, sel);

        // wait upto 1 second
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
        neo_X_lock(X, 0);
    }

    return NULL;
}


// thread entry point
static void* thread_main(void* X)
{
    neo_X* x = (neo_X*)X;

    int stop = False;
    do {
        XEvent xe;
        XNextEvent(x->d, &xe);
        switch (xe.type) {
        case SelectionNotify:
            on_sel_notify(x, &xe.xselection);
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
static void on_sel_notify(neo_X* x, XSelectionEvent* xse)
{
    int sel = (xse->selection == x->atom[prim]) ? prim : clip;

    if (xse->property == x->atom[neo_update]) {
        // read our property
        Atom type = None;
        unsigned char* xptr = NULL;
        unsigned long cb = 0;
        XGetWindowProperty(x->d, x->w, x->atom[neo_update], 0, LONG_MAX, True,
            AnyPropertyType, &type, &(int){0}, &cb, &(unsigned long){0}, &xptr);

        do {
            if (type == x->atom[atom] || type == x->atom[targets]) {
                // TARGETS list
                Atom target = best_target(x, (Atom*)xptr, (int)cb);
                if (target != None) {
                    XConvertSelection(x->d, xse->selection, target, x->atom[neo_update],
                        x->w, CurrentTime);
                    break;
                }
            } else if (type == x->atom[vimenc]) {
                // _VIMENC_TEXT
                if (cb >= 1 + sizeof("utf-8")
                    && !memcmp(xptr + 1, "utf-8", sizeof("utf-8"))) {
                    // this is UTF-8, hurray!
                    neo_X_ready(x, sel, xptr + 1 + sizeof("utf-8"),
                        (size_t)cb - 1 - sizeof("utf-8"), xptr[0]);
                } else {
                    // no UTF-8, sigh... ask for COMPOUND_TEXT then
                    XConvertSelection(x->d, xse->selection, x->atom[compound],
                        x->atom[neo_update], x->w, CurrentTime);
                }
                break;
            } else if (type == x->atom[vimtext]) {
                // _VIM_TEXT: assume UTF-8
                neo_X_ready(x, sel, xptr + 1, (size_t)cb - 1, xptr[0]);
                break;
            } else if (type == x->atom[utf8]) {
                // UTF8_STRING
                neo_X_ready(x, sel, xptr, (size_t)cb, 255);
                break;
            } else if (type == x->atom[compound] || type == x->atom[string]
                || type == x->atom[text]) {
                // COMPOUND_TEXT, STRING or TEXT: attempt to convert to UTF-8
                XTextProperty xtp = {
                    .value = xptr,
                    .encoding = type,
                    .format = 8,
                    .nitems = cb
                };
                char** list;
                if (Xutf8TextPropertyToTextList(x->d, &xtp, &list, &(int){0})
                    == Success) {
                    neo_X_ready(x, sel, list[0], strlen(list[0]), 255);
                    XFreeStringList(list);
                    break;
                }
            }
            // failed to convert
            neo_X_ready(x, sel, NULL, 0, 0);
        } while (0);

        if (xptr != NULL)
            XFree(xptr);
    } else if (xse->property == None) {
        // peer error
        neo_X_ready(x, sel, NULL, 0, 0);
    }
}


// SelectionRequest event handler
static void on_sel_request(neo_X* x, XSelectionRequestEvent* xsre)
{
    // prepare SelectionNotify
    XSelectionEvent xse;
    xse.type = SelectionNotify;
    xse.requestor = xsre->requestor;
    xse.selection = xsre->selection;
    xse.target = xsre->target;
    xse.property = xsre->property ? xsre->property : xsre->target;
    xse.time = xsre->time;

    if (!neo_X_lock(x, 1)) {
        // TARGETS: _VIMENC_TEXT, _VIM_TEXT, UTF8_STRING, COMPOUND_TEXT, STRING, TEXT
        if (xse.target == x->atom[targets]) {
            // response type is ATOM
            xse.target = x->atom[atom];
            XChangeProperty(x->d, xse.requestor, xse.property, xse.target, 32,
                PropModeReplace, (unsigned char*)&x->atom[targets], total - targets);
        } else if (best_target(x, &xse.target, 1) != None) {
            // type may change due to conversion
            set_property(x, (xse.selection == x->atom[prim]) ? prim : clip,
                xse.requestor, xse.property, &xse.target);
        } else {
            // target is unknown
            xse.property = None;
        }
        neo_X_lock(x, 0);
    } else {
        xse.property = None;
    }

    // send SelectionNotify
    XSendEvent(x->d, xse.requestor, 1, 0, (XEvent*)&xse);
}


// ClientMessage event handler
static int on_client_message(neo_X* x, XClientMessageEvent* xcme)
{
    Atom param = (Atom)xcme->data.l[0];

    if (xcme->message_type == x->atom[neo_update]) {        // NEO_UPDATE
        // sync our selection data to system
        Window owner = XGetSelectionOwner(x->d, param);
        if (owner == 0 || owner == x->w) {
            // no conversion needed
            neo_X_ready(x, (param == x->atom[prim]) ? prim : clip, NULL,
                owner ? SIZE_MAX : 0, 0);
        } else {
            // what are supported TARGETS?
            // we'll get answer by SelectionNotify
            XConvertSelection(x->d, param, x->atom[targets], x->atom[neo_update],
                x->w, CurrentTime);
        }
    } else if (xcme->message_type == x->atom[neo_owned]) {  // NEO_OWNED
        // become selection owner
        XSetSelectionOwner(x->d, param, x->w, CurrentTime);
    } else if (xcme->message_type == x->atom[proto]) {      // WM_DELETE_WINDOW
        return (param == x->atom[dele]) ? True : False;
    }

    return False;
}


// best matching target atom
static Atom best_target(neo_X* x, Atom* atom, int count)
{
    int best = total;

    for (int i = 0; i < count && best > targets + 1; ++i)
        for (int j = targets + 1; j < best; ++j)
            if (atom[i] == x->atom[j]) {
                best = j;
                break;
            }

    return best < total ? x->atom[best] : None;
}


// set window property
static void set_property(neo_X* x, int sel, Window w, Atom property, Atom* type)
{
    XTextProperty xtp = {
        .value = x->data[(sel == prim) ? 0 : 1],
        .encoding = *type,
        .format = 8,
        .nitems = x->cb[(sel == prim) ? 0 : 1]
    };
    unsigned char* ptr = NULL;
    unsigned char* xptr = NULL;

    if (xtp.encoding == x->atom[vimenc]) {
        // _VIMENC_TEXT: motion 'encoding' NUL text
        xtp.nitems += 1 + sizeof("utf-8");
    } else if (xtp.encoding == x->atom[vimtext]) {
        // _VIM_TEXT: motion text
        ptr = malloc(1 + xtp.nitems);
        ptr[0] = xtp.value[0];
        memcpy(ptr + 1, xtp.value + 1 + sizeof("utf-8"), xtp.nitems);
        xtp.value = ptr;
        xtp.nitems++;
    } else {
        // skip header
        xtp.value += 1 + sizeof("utf-8");
        // Vim-alike behaviour: STRING -> UTF8_STRING, TEXT -> COMPOUND_TEXT
        if (xtp.encoding == x->atom[string])
            xtp.encoding = x->atom[utf8];
        else if (xtp.encoding == x->atom[text])
            xtp.encoding = x->atom[compound];
    }

    if (xtp.encoding == x->atom[compound]) {
        // convert UTF-8 to COMPOUND_TEXT
        ptr = memcpy(malloc(xtp.nitems + 1), xtp.value, xtp.nitems);
        ptr[xtp.nitems] = 0;
        Xutf8TextListToTextProperty(x->d, (char**)&ptr, 1, XCompoundTextStyle, &xtp);
        xptr = xtp.value;
    }

    // set property
    XChangeProperty(x->d, w, property, xtp.encoding, xtp.format, PropModeReplace,
        xtp.value, (int)xtp.nitems);
    *type = xtp.encoding;

    // free memory
    free(ptr);
    if (xptr != NULL)
        XFree(xptr);
}
