#include "vm/stack.h"
#include "vm/frame.h"
#include "vm/page.h"
#include "threads/thread.h"
#include "threads/pte.h"
#include "userprog/pagedir.h"

//set a limit on the stack size

static bool install_page (void *upage, void *kpage, bool writable);

bool on_the_stack(const void *vaddr){
	void *esp = thread_current()->user_esp;
	return (vaddr >= (PHYS_BASE -  MAX_STACK_SIZE + 0x4000) && ((esp - vaddr == 4) || (esp - vaddr == 32) || vaddr >= esp));
}

bool grow_stack(uint32_t *page, const void *vaddr){
	struct sup_page_table_entry *page_entry;
	struct frame_table_entry *frame_entry;
	bool success;
	void *userPage = pg_round_down(vaddr);
	if(pagedir_get_entry(page, userPage) == NULL && on_the_stack(vaddr)){
		//frametable_create_and_load_page(page, userPage, true);
		page_entry = page_entry_create();
		frame_entry = frametable_allocate(PAL_ZERO);
		if(page_entry != NULL && frame_entry != NULL)
			success = install_page(userPage, frame_entry->kernelPage, true);
		else return false;
		if(success){
			page_entry_set_kernelPage(page_entry, frame_entry->kernelPage);
			page_entry_set_userPage(page_entry, userPage);
			page_entry_set_pd(page_entry, page);
			page_entry_set_type(page_entry, PAGE_ZERO);
			page_entry_set_writable(page_entry, WRITABLE_TO_SWAP);
			pagedir_set_entry(page, userPage, page_entry);
			return true;
		}
		else return false;
	}
	return false;
}

static bool install_page (void *upage, void *kpage, bool writable)
{
  struct thread *t = thread_current ();

  /* Verify that there's not already a page at that virtual
     address, then map our page there. */
  return (pagedir_get_page (t->pagedir, upage) == NULL
          && pagedir_set_page (t->pagedir, upage, kpage, writable));
}
