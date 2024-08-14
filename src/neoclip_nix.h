/*
 * neoclip - Neovim clipboard provider
 * Last Change:  2024 Aug 14
 * License:      https://unlicense.org
 * URL:          https://github.com/matveyt/neoclip
 */


#ifndef NEOCLIP_NIX_H
#define NEOCLIP_NIX_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>


// selection index
enum {
    sel_prim,
    sel_clip,
    sel_total
};

// driver state
typedef struct neo_X neo_X;

neo_X* neo_create(bool first_run, bool targets_atom, const char** perr);
void neo_kill(neo_X* x);
bool neo_lock(neo_X* x, bool lock);
const void* neo_fetch(neo_X* x, int sel, size_t* pcb, int* ptype);
void neo_own(neo_X* x, bool offer, int sel, const void* ptr, size_t cb, int type);


#endif // NEOCLIP_NIX_H
