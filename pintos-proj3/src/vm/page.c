#include "vm/page.h"

struct sup_page_table_entry *page_entry_create(void){
	struct sup_page_table_entry *page_entry;
	page_entry = calloc(1, sizeof *page_entry);
	return page_entry;
}

void page_entry_destroy(struct sup_page_table_entry *page_entry){
	free(page_entry);
}

void page_entry_set_userPage(struct sup_page_table_entry *page_entry, const void *userPage){
	page_entry->userPage = userPage;
}

void page_entry_set_type(struct sup_page_table_entry *page_entry, int type){
	page_entry->type = type;
}

void page_entry_set_writable(struct sup_page_table_entry *page_entry, int writable){
	page_entry->writable = writable;
}

void page_entry_set_pd(struct sup_page_table_entry *page_entry, int *pd){
	page_entry->pd = pd;
}

void page_entry_set_file(struct sup_page_table_entry *page_entry, struct file *file, int offset){
	page_entry->file_entry.file = file;
	page_entry->file_entry.offset = offset;
}

void page_entry_set_kernelPage(struct sup_page_table_entry *page_entry, const void *kernelPage){
	page_entry->kernelPage = kernelPage;
}
