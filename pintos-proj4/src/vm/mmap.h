#ifndef VM_MMAP_H
#define VM_MMAP_H

#include <stddef.h>
#include "threads/interrupt.h"
#define MAX_NUM_FILES 128

struct mmap{
	void *userPage;
	struct file *file;
	int numPages;				/*num of pages in this file*/
};

void mmap(int fd, void *addr, struct intr_frame *f);
void munmap(int mapid);

#endif
