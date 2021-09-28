#ifndef VM_FRAME_H
#define VM_FRAME_H

#include <stdio.h>
#include <stdbool.h>
#include <list.h>
#include <hash.h>
#include <debug.h>
#include <string.h>
#include "threads/palloc.h"
#include "threads/synch.h"

struct frame_table_entry{
	struct list page_entry_list;
	struct list_elem list_elem;
	struct hash_elem hash_elem;
	struct condition IO_finished;
	bool IO;
	void *kernelPage;
	int lock;							/*prevent the frame from being evicted if larger than 0*/
};

void frametable_init(void);
void frametable_free(void *kernelPage);
struct frame_table_entry *frametable_allocate(enum palloc_flags flags);
bool frametable_create_and_load_page(uint32_t *page, const void *userPage, bool write);

#endif