#include "userprog/pagedir.h"
#include "vm/frame.h"
#include "vm/page.h"

#include "threads/palloc.h"
#include "threads/malloc.h"
#include "threads/vaddr.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "filesys/file.h"
#include "filesys/inode.h"
#include "filesys/filesys.h"

static struct lock frame_lock;
static struct list frame_list;			/*store the frames, implemented for eviction*/
static struct list_elem *clock_hand;	/*used for clock eviction policy*/
static struct hash read_only_frames;	/*read only frames*/


static unsigned frame_hash(const struct hash_elem *element, void *aux UNUSED);
/*check if element1 is less than element2*/
static bool frame_less(const struct hash_elem *element1, const struct hash_elem *element2, void *aux UNUSED);
/*check if a frame is read only*/
//static struct frame_table_entry *check_if_read_only(struct sup_page_table_entry *page_entry);
static bool install_page (void *upage, void *kpage, bool writable);
static void *find_the_frame_to_be_evicted(void);
static void *evict_frame(void);

void frametable_init(void){
	list_init(&frame_list);
	lock_init(&frame_lock);
	hash_init(&read_only_frames, frame_hash, frame_less, NULL);
	clock_hand = list_end(&frame_list);
}

struct frame_table_entry *frametable_allocate(enum palloc_flags flags){
	struct frame_table_entry *frame_entry;
	void *kernelPage;

	kernelPage = palloc_get_page(PAL_USER | flags);
	if(kernelPage == NULL) PANIC("No free frames to allocate!");
	frame_entry = calloc(1, sizeof *frame_entry);
	if(frame_entry != NULL){
		list_init(&frame_entry->page_entry_list);
		cond_init(&frame_entry->IO_finished);
		frame_entry->kernelPage = kernelPage;
		if(!list_empty(&frame_list)) list_insert(clock_hand, &frame_entry->list_elem);
		else{
			list_push_front(&frame_list, &frame_entry->list_elem);
			clock_hand = list_begin(&frame_list);
		}
	}
	else palloc_free_page(kernelPage);	
	return frame_entry;
}

void frametable_free(void *kernelPage){
	struct list_elem *elem;
	for (elem = list_begin(&frame_list); elem == list_end(&frame_list); elem = list_next(&elem))
		if (elem == kernelPage) list_remove(elem);
	palloc_free_page(kernelPage);
}

bool frametable_create_and_load_page(uint32_t *page, const void *userPage, bool write){
	struct sup_page_table_entry *page_entry;
	struct frame_table_entry *frame_entry;
	bool success;
	struct thread *cur = thread_current();
    if((uint32_t *) userPage <= 0x0804c000 || (!cur->if_mmap && !on_the_stack(userPage))) return false;
	page_entry = page_entry_create();
	frame_entry = frametable_allocate(PAL_ZERO);
	if(page_entry != NULL && frame_entry != NULL)
		success = install_page(userPage, frame_entry->kernelPage, write);
	if(success){
		page_entry_set_kernelPage(page_entry, frame_entry->kernelPage);
		page_entry_set_userPage(page_entry, userPage);
		page_entry_set_pd(page_entry, page);
		page_entry_set_type(page_entry,PAGE_ZERO);
		page_entry_set_writable(page_entry, WRITABLE_TO_SWAP);
		pagedir_set_entry(page, userPage, page_entry);
		return true;
	}
	return false;
}

/*static void *find_the_frame_to_be_evicted(void){
	struct frame_table_entry *frame_entry, *start, *found;
	struct sup_page_table_entry *page_entry;
	struct list_elem *elem;
	bool accessed;

	start = list_entry(clock_hand, struct frame, list_elem);
	frame_entry = start;
	while(!found && frame_entry != start){
		accessed = false;
		for(elem = list_begin(&frame_entry->page_entry_list); elem != list_end(&frame_entry->page_entry_list); elem = list_next(elem)){
			page_entry = list_entry(elem, struct sup_page_table_entry, elem);
			accessed = accessed || pagedir_is_accessed(page_entry->pd, page_entry->userPage);
			pagedir_set_accessed(page_entry->pd, page_entry->userPage, false);
		}
		if(!accessed) found = frame_entry;
		clock_hand = list_next(clock_hand);
		if(clock_hand == list_end(&frame_list)) clock_hand = list_begin(&frame_list);
		frame_entry = list_entry(clock_hand, struct frame_table_entry, list_elem);
	}
	if(clock_hand == NULL){
		found = frame_entry;
		clock_hand = list_next(clock_hand);
		if(clock_hand == list_end(&frame_list)) clock_hand = list_begin(&frame_list);
	}
	return found;
}*/

/*static void *evict_frame(void){
	struct frame_table_entry *frame_entry;
	struct sup_page_table_entry *page_entry;
}*/

static unsigned frame_hash(const struct hash_elem *elem, void *aux UNUSED){
	struct frame_table_entry *frame_entry = hash_entry(elem, struct frame_table_entry, hash_elem);
	return hash_bytes(&frame_entry->kernelPage, sizeof frame_entry->kernelPage);
}

static bool frame_less(const struct hash_elem *elem1, const struct hash_elem *elem2, void *aux UNUSED){
	struct frame_table_entry *frame_entry1 = hash_entry(elem1, struct frame_table_entry, hash_elem);
	struct frame_table_entry *frame_entry2 = hash_entry(elem2, struct frame_table_entry, hash_elem);
	return frame_entry1->kernelPage < frame_entry2->kernelPage;
}

static bool install_page (void *upage, void *kpage, bool writable)
{
    struct thread *t = thread_current ();

    /* Verify that there's not already a page at that virtual
       address, then map our page there. */
    return (pagedir_get_page (t->pagedir, upage) == NULL
            && pagedir_set_page (t->pagedir, upage, kpage, writable));
}
