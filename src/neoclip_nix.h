/*
 * neoclip - Neovim clipboard provider
 * Last Change:  2024 Jun 25
 * License:      https://unlicense.org
 * URL:          https://github.com/matveyt/neoclip
 */


#ifndef NEOCLIP_NIX_H
#define NEOCLIP_NIX_H


#include <stddef.h>


// atoms
enum {
    prim,       // PRIMARY
    clip,       // CLIPBOARD
    atom,       // ATOM
    atom_pair,  // ATOM_PAIR
    clipman,    // CLIPBOARD_MANAGER
    incr,       // INCR
    integer,    // INTEGER
    null,       // NULL
    wm_proto,   // WM_PROTOCOLS
    wm_dele,    // WM_DELETE_WINDOW
    neo_ready,  // NEO_READY
    neo_offer,  // NEO_OFFER
    // valid targets
    targets,    // TARGETS
    dele,       // DELETE
    multi,      // MULTIPLE
    save,       // SAVE_TARGETS
    timestamp,  // TIMESTAMP
    // encodings from best to worst
    vimenc,     // _VIMENC_TEXT
    vimtext,    // _VIM_TEXT
    plain_utf8, // text/plain;charset=utf-8
    utf8,       // UTF8_STRING
    plain,      // text/plain
    compound,   // COMPOUND_TEXT
    string,     // STRING
    text,       // TEXT
    // total count
    total
};


void* neo_create(int first_run, int targets_atom, const char** perr);
void neo_kill(void* X);
int neo_lock(void* X, int lock);
const void* neo_fetch(void* X, int sel, size_t* pcb, int* ptype);
void neo_own(void* X, int offer, int sel, const void* ptr, size_t cb, int type);


#endif // NEOCLIP_NIX_H
