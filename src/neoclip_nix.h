/*
 * neoclip - Neovim clipboard provider
 * Last Change:  2024 Aug 11
 * License:      https://unlicense.org
 * URL:          https://github.com/matveyt/neoclip
 */


#ifndef NEOCLIP_NIX_H
#define NEOCLIP_NIX_H


#include <stdbool.h>
#include <stddef.h>


enum {
    sel_prim,
    sel_clip,
    sel_total
};


void* neo_create(bool first_run, bool targets_atom, const char** perr);
void neo_kill(void* X);
bool neo_lock(void* X, bool lock);
const void* neo_fetch(void* X, int sel, size_t* pcb, int* ptype);
void neo_own(void* X, bool offer, int sel, const void* ptr, size_t cb, int type);


#endif // NEOCLIP_NIX_H
