#ifndef GUI_H
#define GUI_H
#include "riscv.h"
#define FRAMEBUFFER_VA 0x10000000UL
uint64 gui_framebuffer(pagetable_t pagetable);
void gui_unmap(pagetable_t pagetable);
void gui_clone(pagetable_t old, pagetable_t new);
#endif
