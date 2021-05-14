/*
 * neoclip - Neovim clipboard provider
 * Last Change:  2021 May 13
 * License:      https://unlicense.org
 * URL:          https://github.com/matveyt/neoclip
 */


#ifndef NEO_X_H
#define NEO_X_H


#include <stddef.h>


// X atoms
enum {
    prim,
    clip,
    atom,
    proto,
    dele,
    neo_update,
    neo_owned,
    targets,
    // followed by valid targets only
    vimenc,
    vimtext,
    utf8,
    compound,
    string,
    text,
    total
};


void* neo_X_start(void);
void neo_X_cleanup(void* X);

int neo_X_lock(void* X, int lock);
void neo_X_ready(void* X, int sel, const void* ptr, size_t cb, int type);
void neo_X_send(void* X, int message, int param);
const void* neo_X_update(void* X, int sel, size_t* pcb, int* ptype);


#endif // NEO_X_H
