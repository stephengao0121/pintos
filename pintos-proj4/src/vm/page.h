#ifndef VM_PAGE_H
#define VM_PAGE_H

#include <stdio.h>
#include <list.h>

#define PAGE_ZERO 1
#define PAGE_KERNEL 2
#define PAGE_FILE 4

#define WRITABLE_TO_FILE 1
#define WRITABLE_TO_SWAP 2

struct file_entry{
	struct file *file;
	int offset;
};

struct sup_page_table_entry{
	void *kernelPage;
	void *userPage;
	int type, writable, *pd;
	struct list_elem elem;
	struct file_entry file_entry;
};

struct sup_page_table_entry *page_entry_create(void);
void page_entry_destroy(struct sup_page_table_entry *page_entry);
void page_entry_set_userPage(struct sup_page_table_entry *page_entry, const void *userPage);
void page_entry_set_type(struct sup_page_table_entry *page_entry, int type);
void page_entry_set_writable(struct sup_page_table_entry *page_entry, int writable);
void page_entry_set_pd(struct sup_page_table_entry *page_entry, int *pd);
void page_entry_set_file(struct sup_page_table_entry *page_entry, struct file *file, int offset);
void page_entry_set_kernelPage(struct sup_page_table_entry *page_entry, const void *kernelPage);

#endif