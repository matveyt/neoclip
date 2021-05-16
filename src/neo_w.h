/*
 * neoclip - Neovim clipboard provider
 * Last Change:  2021 May 14
 * License:      https://unlicense.org
 * URL:          https://github.com/matveyt/neoclip
 */


#ifndef NEO_W_H
#define NEO_W_H


#include <stddef.h>


void* neo_W_start(void);
void neo_W_cleanup(void* W);

int neo_W_lock(void* W, int lock);
void neo_W_offer(void* W, const void* ptr, size_t cb, int type);
const void* neo_W_update(void* W, size_t* pcb, int* ptype);


#endif // NEO_W_H
