#ifndef VM_STACK_H
#define VM_STACK_H

#include <stdint.h>
#include <stdbool.h>

#define MAX_STACK_SIZE 0x800000

bool on_the_stack(const void *vaddr);
bool grow_stack(uint32_t *page, const void *vaddr);

#endif
