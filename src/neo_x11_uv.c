/*
 * neoclip - Neovim clipboard provider
 * Last Change:  2024 Aug 20
 * License:      https://unlicense.org
 * URL:          https://github.com/matveyt/neoclip
 */


#include "neoclip_nix.h"
#include <time.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>


// X11 Atoms
enum {
    // sel_prim,        // PRIMARY
    // sel_clip,        // CLIPBOARD
    atom = sel_total,   // ATOM
    atom_pair,          // ATOM_PAIR
    incr,               // INCR
    integer,            // INTEGER
    null,               // NULL
    neo_ready,          // NEO_READY
    // supported targets
    targets,            // TARGETS
    dele,               // DELETE
    multi,              // MULTIPLE
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
    Display* d;                 // X Display
    Window w;                   // X Window
    Time delta;                 // X server startup time (ms from Unix epoch)
    Atom atom[total];           // X Atoms list
    Atom notify_targets;        // Response type for TARGETS (ATOM or TARGETS),
                                // see https://www.edwardrosten.com/code/x11.html
    uint8_t* data[sel_total];   // Selection: _VIMENC_TEXT
    size_t cb[sel_total];       // Selection: text size only
    Time stamp[sel_total];      // Selection: time stamp
    bool f_rdy[sel_total];      // Selection: "ready" flag
};


// forward prototypes
static int neo__gc(lua_State* L);
static int cb_prepare(lua_State* L);
static int cb_poll(lua_State* L);
static void dispatch_event(neo_X* x, XEvent* xe);
static void on_sel_notify(neo_X* x, XSelectionEvent* xse);
static void on_sel_request(neo_X* x, XSelectionRequestEvent* xsre);
static size_t alloc_data(neo_X* x, int sel, size_t cb);
static int atom2sel(neo_X* x, Atom atom);
static Atom best_target(neo_X* x, Atom* atom, int count);
static Bool is_incr_notify(Display* d, XEvent* xe, XPointer arg);
static Time time_diff(Time ref);
static void to_multiple(neo_X* x, int sel, XSelectionEvent* xse);
static void to_property(neo_X* x, int sel, Window w, Atom property, Atom type);
static int vimg(lua_State* L, const char* var, int d);


// init state and start thread
int neo_start(lua_State* L)
{
    neo_X* x = neo_x(L);
    if (x == NULL) {
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
            [sel_clip] = "CLIPBOARD",
            [atom] = "ATOM",
            [atom_pair] = "ATOM_PAIR",
            [incr] = "INCR",
            [integer] = "INTEGER",
            [null] = "NULL",
            [neo_ready] = "NEO_READY",
            [targets] = "TARGETS",
            [dele] = "DELETE",
            [multi] = "MULTIPLE",
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
        XInternAtoms(x->d, atom_name, total, 0, x->atom);
        x->notify_targets = vimg(L, "neoclip_targets_atom", true) ?
            x->atom[atom] : x->atom[targets];
        for (int i = 0; i < sel_total; ++i) {
            x->data[i] = NULL;
            x->cb[i] = 0;
            x->stamp[i] = CurrentTime;
            x->f_rdy[i] = false;
        }

        // metatable for state
        luaL_newmetatable(L, lua_tostring(L, uv_module));
        neo_pushcfunction(L, neo__gc);
        lua_setfield(L, -2, "__gc");
        lua_setmetatable(L, -2);

        // uv_share.x = x
        lua_setfield(L, uv_share, "x");

        // start polling the display
        lua_getglobal(L, "vim");                // vim => stack
        lua_getfield(L, -1, "uv");              // uv or loop => stack
        if (lua_isnil(L, -1)) {
            lua_pop(L, 1);
            lua_getfield(L, -1, "loop");
        }

        // local prepare = uv.new_prepare()
        lua_getfield(L, -1, "new_prepare");
        lua_call(L, 0, 1);                      // prepare => stack
        // uv.prepare_start(prepare, cb_prepare)
        lua_getfield(L, -2, "prepare_start");
        lua_pushvalue(L, -2);
        neo_pushcfunction(L, cb_prepare);
        lua_call(L, 2, 0);

        // local poll = uv.new_poll(XConnectionNumber(x->d))
        lua_getfield(L, -2, "new_poll");
        lua_pushinteger(L, XConnectionNumber(x->d));
        lua_call(L, 1, 1);                      // poll => stack
        // uv.poll_start(poll, "r", cb_poll)
        lua_getfield(L, -3, "poll_start");
        lua_pushvalue(L, -2);
        lua_pushliteral(L, "r");
        neo_pushcfunction(L, cb_poll);
        lua_call(L, 3, 0);

        // uv_share.poll = poll
        lua_setfield(L, uv_share, "poll");      // poll <= stack
        // uv_share.prepare = prepare
        lua_setfield(L, uv_share, "prepare");   // prepare <= stack
        // uv_share.uv = vim.uv or vim.loop
        lua_setfield(L, uv_share, "uv");        // uv or loop <= stack

        // force property change to get timestamp from X server
        XSelectInput(x->d, x->w, PropertyChangeMask);
        XChangeProperty(x->d, x->w, x->atom[timestamp], x->atom[timestamp], 32,
            PropModeAppend, NULL, 0);
    }

    lua_pushnil(L);
    return 1;
}


// destroy state
static int neo__gc(lua_State* L)
{
    // cannot checkudata anymore
    neo_X* x = lua_touserdata(L, 1);

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

    // clear data
    if (x != NULL) {
        for (int i = 0; i < sel_total; ++i)
            free(x->data[i]);
        XDestroyWindow(x->d, x->w);
        XCloseDisplay(x->d);
    }

    return 0;
}


// fetch new selection
void neo_fetch(lua_State* L, int ix, int sel)
{
    neo_X* x = neo_x(L);
    if (x != NULL) {
        // attempt to convert selection
        Window owner = XGetSelectionOwner(x->d, x->atom[sel]);
        if (owner == 0 || owner == x->w) {
            // no conversion needed
            neo_own(x, false, sel, NULL, owner ? SIZE_MAX : 0, 0);
        } else {
            // what TARGETS are supported?
            x->f_rdy[sel] = false;
            XConvertSelection(x->d, x->atom[sel], x->atom[targets], x->atom[neo_ready],
                x->w, time_diff(x->delta));

            lua_getfield(L, uv_share, "uv");    // uv or loop => stack

            // local timeout, now = uv.now() + 1000
            lua_Integer timeout, now;
            lua_getfield(L, -1, "now");
            lua_call(L, 0, 1);
            timeout = lua_tointeger(L, -1) + 1000;
            lua_pop(L, 1);

            // run nested loop
            do {
                // uv.run"once"
                lua_getfield(L, -1, "run");
                lua_pushliteral(L, "once");
                lua_call(L, 1, 0);

                // check selection ready
                if (x->f_rdy[sel])
                    break;

                // now = uv.now()
                lua_getfield(L, -1, "now");
                lua_call(L, 0, 1);
                now = lua_tointeger(L, -1);
                lua_pop(L, 1);
            } while (now < timeout);

            lua_pop(L, 1);                      // uv or loop <= stack
        }

        // split selection into t[ix]
        if (x->f_rdy[sel] && x->cb[sel] > 0)
            neo_split(L, ix, x->data[sel] + 1 + sizeof("utf-8"), x->cb[sel],
                x->data[sel][0]);
    }
}


// own new selection
// (cb == 0) => empty selection, (cb == SIZE_MAX) => keep selection
void neo_own(neo_X* x, bool offer, int sel, const void* ptr, size_t cb, int type)
{
    // set new data
    if (cb < SIZE_MAX) {
        // _VIMENC_TEXT: type 'encoding' NUL text
        cb = alloc_data(x, sel, cb);
        if (cb > 0) {
            x->data[sel][0] = type;
            memcpy(x->data[sel] + 1, "utf-8", sizeof("utf-8"));
            memcpy(x->data[sel] + 1 + sizeof("utf-8"), ptr, cb);
        }
        x->stamp[sel] = time_diff(x->delta);
    }

    if (offer) {
        // offer our selection
        XSetSelectionOwner(x->d, x->atom[sel], x->cb[sel] ? x->w : None,
            x->cb[sel] ? x->stamp[sel] : CurrentTime);
    } else {
        // signal data ready
        x->f_rdy[sel] = true;
    }
}


// uv_prepare_t callback
static int cb_prepare(lua_State* L)
{
    neo_X* x = neo_x(L);
    if (x != NULL) {
        XEvent xe;
        while (XPending(x->d) > 0) {
            XNextEvent(x->d, &xe);
            dispatch_event(x, &xe);
        }
    }

    return 0;
}


// uv_poll_t callback
static int cb_poll(lua_State* L)
{
    neo_X* x = neo_x(L);
    if (x != NULL && lua_isnil(L, 1) && *lua_tostring(L, 2) == 'r') {
        XEvent xe;
        XNextEvent(x->d, &xe);
        dispatch_event(x, &xe);
    }

    return 0;
}


// X event dispatcher
static void dispatch_event(neo_X* x, XEvent* xe)
{
    switch (xe->type) {
    case PropertyNotify:
        if (xe->xproperty.atom == x->atom[timestamp]) {
            x->delta = time_diff(xe->xproperty.time);
            XSelectInput(x->d, x->w, NoEventMask);
        }
    break;
    case SelectionNotify:
        on_sel_notify(x, &xe->xselection);
    break;
    case SelectionRequest:
        on_sel_request(x, &xe->xselectionrequest);
    break;
    }
}


// SelectionNotify event handler
static void on_sel_notify(neo_X* x, XSelectionEvent* xse)
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
                    // no UTF-8; then ask for UTF8_STRING
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
                neo_own(x, false, sel, buf, cb, 255);
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
                    neo_own(x, false, sel, list[0], strlen(list[0]), 255);
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
        // peer error
        neo_own(x, false, sel, NULL, 0, 0);
    }
}


// SelectionRequest event handler
static void on_sel_request(neo_X* x, XSelectionRequestEvent* xsre)
{
    // prepare SelectionNotify
    XSelectionEvent xse = {
        .type = SelectionNotify,
        .requestor = xsre->requestor,
        .selection = xsre->selection,
        .target = xsre->target,
        .property = xsre->property ? xsre->property : xsre->target,
        .time = xsre->time,
    };

    int sel = atom2sel(x, xse.selection);

    // TARGETS: DELETE, MULTIPLE, TIMESTAMP, _VIMENC_TEXT, _VIM_TEXT, UTF8_STRING,
    // COMPOUND_TEXT, STRING, TEXT
    if (xse.time != CurrentTime && xse.time < x->stamp[sel]) {
        // refuse request for non-matching timestamp
        xse.property = None;
    } else if (xse.target == x->atom[targets]) {
        xse.target = x->notify_targets;
        XChangeProperty(x->d, xse.requestor, xse.property, xse.target, 32,
            PropModeReplace, (unsigned char*)&x->atom[targets], total - targets);
    } else if (xse.target == x->atom[dele]) {
        // response type is NULL
        if (xse.target == x->atom[dele])
            alloc_data(x, sel, 0);
        xse.target = x->atom[null];
        XChangeProperty(x->d, xse.requestor, xse.property, xse.target, 32,
            PropModeReplace, NULL, 0);
    } else if (xse.target == x->atom[multi]) {
        // response type is ATOM_PAIR
        xse.target = x->atom[atom_pair];
        to_multiple(x, sel, &xse);
    } else if (xse.target == x->atom[timestamp]) {
        // response type is INTEGER
        xse.target = x->atom[integer];
        XChangeProperty(x->d, xse.requestor, xse.property, xse.target, 32,
            PropModeReplace, (unsigned char*)&x->stamp[sel], 1);
    } else if (best_target(x, &xse.target, 1) != None) {
        // attempt to convert
        to_property(x, sel, xse.requestor, xse.property, xse.target);
    } else {
        // unknown target
        xse.property = None;
    }

    // send SelectionNotify
    XSendEvent(x->d, xse.requestor, True, 0, (XEvent*)&xse);
}


// (re-)allocate data buffer for selection
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


// Atom => sel enum
static int atom2sel(neo_X* x, Atom atom)
{
    for (int i = 0; i < sel_total; ++i)
        if (atom == x->atom[i])
            return i;

    return sel_clip;
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
        xse->target, &(Atom){None}, &(int){0}, &ul_tgt, &(unsigned long){0},
        (unsigned char**)&tgt);
    int i_tgt = (long)ul_tgt;

    for (int i = 0; i < i_tgt; i += 2)
        if (best_target(x, &tgt[i], 1) != None && tgt[i + 1] != None)
            to_property(x, sel, xse->requestor, tgt[i + 1], tgt[i]);
        else
            tgt[i + 1] = None;

    if (i_tgt > 0) {
        XChangeProperty(x->d, xse->requestor, xse->property, xse->target, 32,
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


// get vim.g[var] as integer
static int vimg(lua_State* L, const char* var, int d)
{
    lua_getglobal(L, "vim");
    lua_getfield(L, -1, "g");
    lua_getfield(L, -1, var);
    int value = lua_isboolean(L, -1) ? lua_toboolean(L, -1) : luaL_optint(L, -1, d);
    lua_pop(L, 3);
    return value;
}
