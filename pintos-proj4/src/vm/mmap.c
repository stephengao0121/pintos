#include "vm/mmap.h"
#include "vm/stack.h"
#include "vm/page.h"
#include "filesys/file.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include <list.h>

static struct file *find_fd(int fd);
static int get_mapid(void *userPage, struct file *file, size_t numPages);

void mmap(int fd, void *vaddr, struct intr_frame *f){
	struct thread *cur = thread_current();
	cur->if_mmap = true;
	struct sup_page_table_entry *page_entry;
	struct file *file = find_fd(fd);
	int length, numPages, i, mapid, offset, read_bytes;
	void *userPage;

	if(vaddr == 0 || pg_ofs(vaddr) != 0 || fd == STDIN_FILENO || fd == STDOUT_FILENO || file == NULL){
		f->eax = -1;
		cur->if_mmap = false;
		return ;
	}
	length = file_length(file);
	if(length == 0){
		f->eax = -1;       
		cur->if_mmap = false;
		return ;
	}
	numPages = ((size_t) pg_round_up((const void *)length)) / PGSIZE;
	//numPages = length % PGSIZE > 0 ?  length / PGSIZE + 1 : length / PGSIZE;
	i = 0;
	for(userPage = vaddr; i < numPages; userPage -= PGSIZE, i++){
		if(pagedir_get_entry(cur->pagedir, userPage) != NULL || userPage >= PHYS_BASE - MAX_STACK_SIZE + 0x4000 || userPage <= 0x0804c000){
			f->eax = -1;
			cur->if_mmap = false;
			return ;
		}
	}
 	file = file_reopen(file);
	if(file == NULL){
		f->eax = -1;
		cur->if_mmap = false;
		return ;
	}
	mapid = get_mapid(vaddr, file, numPages);
	if(mapid == -1){
		file_close(file);
		cur->if_mmap = false;
		f->eax = -1;
		return ;
	}
	i = 0;
	offset = 0;
	for(userPage = vaddr; i < numPages; userPage = PGSIZE, i++){
		if(length > PGSIZE){
			read_bytes = file_read_at(file, vaddr - PGSIZE*i, PGSIZE, offset);
			ASSERT(read_bytes == PGSIZE);
			offset += PGSIZE;
			length -= PGSIZE;
		}
		else{
		read_bytes = file_read_at(file, vaddr - PGSIZE*i, length, offset);
		ASSERT(read_bytes == length);
	}
	}
	if(i < numPages) PANIC(" ");
	f->eax = mapid;
}

void munmap(int mapid){
	struct thread *cur = thread_current();
	struct mmap *mmap;
	void *userPage;
	int i = 0;
	struct sup_page_table_entry * spt;
	if(mapid >= 0 && mapid < MAX_NUM_FILES){
		mmap = &cur->mappedFiles[mapid];
		if(mmap->file != NULL){
			for(userPage = mmap->userPage; i < mmap->numPages; userPage -= PGSIZE, i++){
				file_write_at(mmap->file, userPage, PGSIZE, PGSIZE*i);
				spt = pagedir_get_entry(cur->pagedir, userPage);
				frametable_free(spt->kernelPage);
				page_entry_destroy(spt);
				pagedir_clear_page(cur->pagedir, userPage);
			}
			file_close(mmap->file);
			mmap->file = NULL;			
		}
	}
	cur->if_mmap = false;
}

static struct file *find_fd(int fd){
	struct thread *cur = thread_current();
	struct list_elem *e;
	struct filedescriptor *fD;
	for (e = list_begin(&cur->fd); e != list_end(&cur->fd); e = list_next(e)){
		fD = list_entry(e, struct filedescriptor, elem);
		if(fD->fd_num == fd) return fD->f;
	}
	return NULL;
}

static int get_mapid(void *userPage, struct file *file, size_t numPages){
	struct thread *cur = thread_current();
	int mapid = -1;
	for(mapid = 0; mapid < MAX_NUM_FILES; mapid++){
		if(cur->mappedFiles[mapid].file == NULL) break;
	}
	if(mapid < MAX_NUM_FILES){
		cur->mappedFiles[mapid].userPage = userPage;
		cur->mappedFiles[mapid].file = file;
		cur->mappedFiles[mapid].numPages = numPages;
	}
	return mapid;
}
