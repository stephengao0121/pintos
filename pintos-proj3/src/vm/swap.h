#ifndef VM_SWAP_H
#define VM_SWAP_H

#include <stdbool.h>
#include <bitmap.h>
#include "threads/vaddr.h"
#include "vm/swap.h"
#include "devices/block.h"

void swap_init(void);
/*write the page to the swap disk, return the index of swap region where it is placed*/
block_sector_t swap_write (void *kernelPage);
/*read the content at a given swap index, store a PGSIZE data into the page*/
void swap_read(block_sector_t index, void *kernelPage);

void swap_release(block_sector_t index);

#endif