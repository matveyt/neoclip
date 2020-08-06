/*
 * neoclip - Neovim clipboard provider
 * Last Change:  2020 Aug 06
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


// context structure
typedef struct {
    Display* d;                 // X Display
    Window w;                   // X Window
    Atom atom[total];           // X Atoms
    void* data[2];              // Selection data
    size_t cb[2];               // Selection size
    pthread_cond_t c_rdy[2];    // Selection "ready" condition
    int f_rdy[2];               // Selection "ready" flag
    pthread_mutex_t lock;       // Mutex lock
    pthread_t tid;              // Thread ID
} neo_X;


// forward prototypes
static void* thread_main(void* X);
static void on_sel_notify(neo_X* x, XSelectionEvent* xse);
static void on_sel_request(neo_X* x, XSelectionRequestEvent* xsre);
static int on_client_message(neo_X* x, XClientMessageEvent* xcme);


// init context and start thread
void* neo_X_start(void)
{
    // try to open display first
    const char* display_name = getenv("DISPLAY");
    Display* d = XOpenDisplay(display_name ? display_name : ":0");
    if (d == NULL)
        return NULL;

    // atom names
    static char* names[] = {
        [prim] = "PRIMARY",
        [clip] = "CLIPBOARD",
        [targets] = "TARGETS",
        [utf8] = "UTF8_STRING",
        [proto] = "WM_PROTOCOLS",
        [dele] = "WM_DELETE_WINDOW",
        [neo_update] = "NEO_UPDATE",
        [neo_owned] = "NEO_OWNED"
    };

    // context
    neo_X* x = calloc(1, sizeof(neo_X));
    x->d = d;
    x->w = XCreateSimpleWindow(d, DefaultRootWindow(d), 0, 0, 1, 1, 0, 0, 0);
    XInternAtoms(d, names, sizeof(names) / sizeof(names[0]), 0, x->atom);
    XSetWMProtocols(d, x->w, &x->atom[dele], 1);
    pthread_cond_init(&x->c_rdy[0], NULL);
    pthread_cond_init(&x->c_rdy[1], NULL);
    pthread_mutex_init(&x->lock, NULL);
    pthread_create(&x->tid, NULL, thread_main, x);

    return x;
}


// destroy context
// the thread must finish at this point
void neo_X_cleanup(void* X)
{
    neo_X* x = (neo_X*)X;
    pthread_mutex_destroy(&x->lock);
    pthread_cond_destroy(&x->c_rdy[0]);
    pthread_cond_destroy(&x->c_rdy[1]);
    free(x->data[0]);
    free(x->data[1]);
    XDestroyWindow(x->d, x->w);
    XCloseDisplay(x->d);
    free(x);
}


// pthread_join proxy
void neo_X_join(void* X)
{
    neo_X* x = (neo_X*)X;
    pthread_join(x->tid, NULL);
}


// change selection buffer contents
// cb: 0 = empty buffer; SIZE_MAX = keep, signal only
void neo_X_ready(void* X, int sel, const void* ptr, size_t cb)
{
    neo_X* x = (neo_X*)X;

    if (!pthread_mutex_lock(&x->lock)) {
        int ix = (sel != prim) ? 1 : 0;
        // set new data
        if (cb != SIZE_MAX) {
            free(x->data[ix]);
            if (cb) {
                x->data[ix] = memcpy(malloc(cb), ptr, cb);
                x->cb[ix] = cb;
            } else {
                x->data[ix] = NULL;
                x->cb[ix] = 0;
            }
        }
        // signal data is ready
        x->f_rdy[ix] = 1;
        pthread_cond_signal(&x->c_rdy[ix]);
        pthread_mutex_unlock(&x->lock);
    }
}


// send ClientMessage to the thread
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


// pthread_mutex_unlock proxy
void neo_X_unlock(void* X)
{
    neo_X* x = (neo_X*)X;
    pthread_mutex_unlock(&x->lock);
}


// update selection data from system
// return NULL on error
const void* neo_X_update(void* X, int sel, size_t* len)
{
    neo_X* x = (neo_X*)X;
    int ix = (sel != prim) ? 1 : 0;

    // send request
    x->f_rdy[ix] = 0;
    neo_X_send(x, neo_update, sel);

    if (!pthread_mutex_lock(&x->lock)) {
        // wait upto one second
        struct timespec t = {0, 0};
        clock_gettime(CLOCK_REALTIME, &t); ++t.tv_sec;
        while (!x->f_rdy[ix] && !pthread_cond_timedwait(&x->c_rdy[ix], &x->lock, &t))
        {}  // nothing

        // success
        if (x->f_rdy[ix]) {
            *len = x->cb[ix];
            return x->data[ix];
        }

        // unlock on error
        pthread_mutex_unlock(&x->lock);
    }

    return NULL;
}


// thread entry point
static void* thread_main(void* X)
{
    neo_X* x = (neo_X*)X;

    int stop = 0;
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
    if (xse->property) {
        // read property
        unsigned char* ptr = NULL;
        unsigned long cb = 0;
        XGetWindowProperty(x->d, x->w, xse->property, 0, LONG_MAX, 1, x->atom[utf8],
            &(Atom){0}, &(int){0}, &cb, &(unsigned long){0}, &ptr);

        // signal new selection
        int sel = (xse->selection != x->atom[prim]) ? clip : prim;
        neo_X_ready(x, sel, ptr, (size_t)cb);

        // free Xlib memory
        if (ptr)
            XFree(ptr);
    }
}


// SelectionRequest event handler
static void on_sel_request(neo_X* x, XSelectionRequestEvent* xsre)
{
    // prepare SelectionNotify event
    XSelectionEvent xse;
    xse.type = SelectionNotify;
    xse.requestor = xsre->requestor;
    xse.selection = xsre->selection;
    xse.target = xsre->target;
    xse.property = xsre->property ? xsre->property : xsre->target;
    xse.time = xsre->time;

    int err = pthread_mutex_lock(&x->lock);
    if (!err) {
        if (xse.target == x->atom[targets]) {
            // TARGETS: UTF8_STRING
            XChangeProperty(x->d, xse.requestor, xse.property, xse.target, 32,
                PropModeReplace, (unsigned char*)&x->atom[utf8], 1);
        } else if (xse.target == x->atom[utf8]) {
            // UTF8_STRING: data[ix_sel]
            int ix_sel = (xse.selection != x->atom[prim]) ? 1 : 0;
            XChangeProperty(x->d, xse.requestor, xse.property, xse.target, 8,
                PropModeReplace, (unsigned char*)x->data[ix_sel], (int)x->cb[ix_sel]);
        } else {
            // unknown target
            err = 1;
        }
        pthread_mutex_unlock(&x->lock);
    }
    if (err)
        xse.property = 0;

    // send SelectionNotify
    XSendEvent(x->d, xse.requestor, 1, 0, (XEvent*)&xse);
}


// ClientMessage event handler
static int on_client_message(neo_X* x, XClientMessageEvent* xcme)
{
    Atom param = (Atom)xcme->data.l[0];

    // NEO_UPDATE
    if (xcme->message_type == x->atom[neo_update]) {
        // sync our selection data with the system's
        Window owner = XGetSelectionOwner(x->d, param);
        if (owner == 0 || owner == x->w) {
            // no need to convert
            int sel = (param != x->atom[prim]) ? clip : prim;
            neo_X_ready(x, sel, NULL, owner ? SIZE_MAX : 0);
        } else {
            // initiate conversion from another window
            // the data will be ready upon SelectionNotify
            XConvertSelection(x->d, param, x->atom[utf8], x->atom[neo_update], x->w,
                CurrentTime);
        }

    // NEO_OWNED
    } else if (xcme->message_type == x->atom[neo_owned]) {
        // become selection owner
        XSetSelectionOwner(x->d, param, x->w, CurrentTime);

    // WM_PROTOCOLS + WM_DELETE_WINDOW
    } else if (xcme->message_type == x->atom[proto]) {
        return (param == x->atom[dele]);
    }

    return 0;
}
