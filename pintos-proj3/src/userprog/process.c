#include "userprog/process.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "userprog/gdt.h"
#include "userprog/pagedir.h"
#include "userprog/tss.h"
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/flags.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "lib/string.h"
#include "threads/vaddr.h"
#include "userprog/syscall.h"
#include "filesys/inode.h"
#include "vm/frame.h"
#include "vm/mmap.h"

static thread_func start_process NO_RETURN;
static bool load (const char *cmdline, void (**eip) (void), void **esp);

/* Starts a new thread running a user program loaded from
   FILENAME.  The new thread may be scheduled (and may even exit)
   before process_execute() returns.  Returns the new process's
   thread id, or TID_ERROR if the thread cannot be created. */
tid_t
process_execute (const char *file_name) 
{
  char *fn_copy=NULL, *argvs, *program=NULL;
  struct tcb *tcb=NULL;
  tid_t tid;

  //lock_acquire(&thread_current()->child_lock);
  fn_copy = palloc_get_page (0);
  program = palloc_get_page (0);
  tcb = palloc_get_page(0);
  //fn_copy = calloc(1,sizeof*fn_copy);
  //program = calloc(1,sizeof*program);
  //tcb = calloc(1, sizeof*tcb);

  if(tcb == NULL || fn_copy == NULL || program == NULL)
    PANIC("NULL NULL NULL!!");

  strlcpy (fn_copy, file_name, PGSIZE);
  strlcpy (program, file_name, PGSIZE); 
  program = strtok_r(program, " ",&argvs);

  sema_init(&tcb->sema,0);
  sema_init(&tcb->wait_sema,0);
  //tcb->tid = -2; //dont know yet
  //tcb->exit_code = -2; //don't know yet
  tcb->argv = argvs;
  tcb->prog = program;
  tcb->exit = false;
  tcb->wait = false;
  tcb->me = NULL;
  tcb->goa = false;
  tcb->parent = thread_current();
  
  tid = thread_create (program, PRI_DEFAULT, start_process, tcb);
  sema_down(&tcb->sema); //wait until load and push argv to stack
  
  if(tcb->tid == tid)
  {
    list_push_back(&thread_current()->child_tcb, &tcb->elem);
  }
  else
  {
    goto end;
  }
  
  palloc_free_page(fn_copy);
  palloc_free_page(program);
  
  return tid;
  
  end:    
  palloc_free_page(fn_copy);
  palloc_free_page(program);
  palloc_free_page(tcb);
  return -1;
}

/* A thread function that loads a user process and starts it
   running. */
static void
start_process (void *ptr)
{
  struct tcb *tcb = ptr;
  void **esp=NULL;
  char *file_name = tcb->prog;
  char *argv = tcb->argv;
  struct intr_frame if_;
  bool success;
  unsigned stack_size=0;
  unsigned char * argv_ptr;
  unsigned cnt=0;

  thread_current()->tcb = ptr;
  tcb->me = thread_current();
  
  /* Initialize interrupt frame and load executable. */
  memset (&if_, 0, sizeof if_);
  if_.gs = if_.fs = if_.es = if_.ds = if_.ss = SEL_UDSEG;
  if_.cs = SEL_UCSEG;
  if_.eflags = FLAG_IF | FLAG_MBS;
  success = load (file_name, &if_.eip, &if_.esp);
  if (success) // push argv to stack
  { 
    char *tmp;
    char *save;
    char **token = (char **)palloc_get_page(0);
    int i;
    tcb->tid = thread_current()->tid;       
    esp = &if_.esp;

    if(strlen(argv) != 0)
    {
      int argc=0;
      for(tmp = strtok_r(argv," ",&save); tmp != NULL; tmp = strtok_r(NULL, " ",&save))
      {
        if(tmp[0] != ' ')
        {
          token[argc++] = tmp;
        }
      }
      for(i=0; i<argc; i++)
      {
        *esp -= strlen(token[argc-1-i])+1;
        stack_size += strlen(token[argc-1-i])+1;
        memcpy(*esp, (void *)token[argc-1-i], strlen(token[argc-1-i])+1);
      }
      palloc_free_page(token);
    }
    *esp -= strlen(thread_current()->name)+1; 
    stack_size += strlen(thread_current()->name) +1;
    memcpy(*esp, thread_current()->name, strlen(thread_current()->name)+1);
    argv_ptr = PHYS_BASE-2;
    *esp -= 4 - stack_size % 4;
    *esp -= 4;
    //*(unsigned int*)*esp = (unsigned int)NULL;
    int argc=0;
    for(cnt = 0 ; cnt <= stack_size-1 ; cnt++) // make argv split, and push argv's addr
    {
      if((*(argv_ptr-cnt) == '\0' || *(argv_ptr-cnt)== ' ') &&
         (*(argv_ptr-cnt+1) != '\0' || *(argv_ptr-cnt+1)!= ' '))
      {
          argc++;
          *esp -= 4;
          *(unsigned int *)*esp = (unsigned int)(argv_ptr-cnt+1);
      }
    }
    *esp -=4;
    *(int *)*esp = (int)*esp+4; // addr of argv[0]'s addr
    *esp -=4;
    *(int *)*esp = (int)argc; // push argc
    *esp -=4;
    *(int *)*esp = 0;
  }
  sema_up(&(tcb->sema));

  /*If load failed, quit.*/
  if (!success) 
  {
    sys_exit(-1,NULL);
  }
  /* Start the user process by simulating a return from an
     interrupt, implemented by intr_exit (in
     threads/intr-stubs.S).  Because intr_exit takes all of its
     arguments on the stack in the form of a `struct intr_frame',
     we just point the stack pointer (%esp) to our stack frame
     and jump to it. */

  asm volatile ("movl %0, %%esp; jmp intr_exit" : : "g" (&if_) : "memory");
  NOT_REACHED ();
}

/* Waits for thread TID to die and returns its exit status.  If
   it was terminated by the kernel (i.e. killed due to an
   exception), returns -1.  If TID is == tidinvalid or if it was not a
   child of the calling process, or if process_wait() has already
   been successfully called for the gi== tidven TID, returns -1
   immediately, without waiting.== tid
== tid
   This function will be implemented in problem 2-2.  For now, it
   does nothing. */
int
process_wait (tid_t child_tid) 
{
  struct thread * curr = thread_current();
  struct tcb *tcb=NULL;
  struct list_elem *tmp=NULL;
  int exit_code;
  /* first, find child tcb*/
  if(!list_empty(&curr->child_tcb))
  {
    for(tmp = list_front(&curr->child_tcb); ; tmp = list_next(tmp))
    {
      tcb = list_entry(tmp, struct tcb, elem);
      if(tcb->tid == child_tid)
        break; //find.
      if(tmp == list_end(&curr->child_tcb))
      { 
        if(tcb->tid != child_tid)
        {
          printf("CANNOT find child process!\n");
          return -1;
        }
        else 
          break;
      }
    }
  }
  if(tmp==NULL)
  {
    return -1;
  }
  if(tcb->wait==true)
  {
    printf("ERROR! child process has been waiting!\n");
    return -1;
  }
  else
    tcb->wait = true;
  sema_down(&(tcb->wait_sema)); //wait parent process in process_wait
  /* now child process has been exited */
  list_remove(tmp);
  exit_code = tcb->exit_code;
  palloc_free_page(tcb);
  return exit_code;
}

/* Free the current process's resources. */
void
process_exit (void)
{
  struct thread *cur = thread_current ();
  struct tcb * tcb;
  struct filedescriptor *fd;
  uint32_t *pd;
  int mapid;
  
  if(cur->mappedFiles != NULL){
    for (mapid = 0; mapid < MAX_NUM_FILES; mapid++){
      if(cur->mappedFiles[mapid].file != NULL){
 	 munmap(mapid);
      }
    }
    free(cur->mappedFiles);
    cur->if_mmap = false;
  }  
  
  for(struct list *list = &cur->child_tcb;!list_empty(list);)
  {
    struct list_elem *elem = list_pop_front (list) ;
    tcb = list_entry(elem, struct tcb, elem);
    if(tcb->exit == true)
    {
      palloc_free_page(tcb);
    }
    else
    {
      tcb->goa=true;
      tcb->parent=NULL;
    } 
  }
  for(struct list *list_ = &cur->fd;!list_empty(list_);)
  {
    struct list_elem *elem = list_pop_front (list_);
    fd = list_entry(elem, struct filedescriptor, elem);
    file_close(fd->f);
    palloc_free_page(fd); 
  }
  if(cur->current_file)
  {
    file_allow_write(cur->current_file);
    file_close(cur->current_file);
  }
  /* wake up parent process */
  cur->tcb->exit = true;
  sema_up(&(cur->tcb->wait_sema));
  /*and remove itself if cur thread is orphan */
  if(cur->tcb->goa == true)
  {
    palloc_free_page(cur->tcb);
  }
  /* Destroy the current process's page directory and switch back
     to the kernel-only page directory. */
  pd = cur->pagedir;
  if (pd != NULL) 
  {
    /* Correct ordering here is crucial.  We must set
       cur->pagedir to NULL before switching page directories,
       so that a timer interrupt can't switch back to the
       process page directory.  We must activate the base page
       directory before destroying the process's page
       directory, or our active page directory will be one
       that's been freed (and cleared). */
    cur->pagedir = NULL;
    pagedir_activate (NULL);
    pagedir_destroy (pd);
  }
}

/* Sets up the CPU for running user code in the current
   thread.
   This function is called on every context switch. */
void
process_activate (void)
{
  struct thread *t = thread_current ();

  /* Activate thread's page tables. */
  pagedir_activate (t->pagedir);

  /* Set thread's kernel stack for use in processing
     interrupts. */
  tss_update ();
}

/* We load ELF binaries.  The following definitions are taken
   from the ELF specification, [ELF1], more-or-less verbatim.  */

/* ELF types.  See [ELF1] 1-2. */
typedef uint32_t Elf32_Word, Elf32_Addr, Elf32_Off;
typedef uint16_t Elf32_Half;

/* For use with ELF types in printf(). */
#define PE32Wx PRIx32   /* Print Elf32_Word in hexadecimal. */
#define PE32Ax PRIx32   /* Print Elf32_Addr in hexadecimal. */
#define PE32Ox PRIx32   /* Print Elf32_Off in hexadecimal. */
#define PE32Hx PRIx16   /* Print Elf32_Half in hexadecimal. */

/* Executable header.  See [ELF1] 1-4 to 1-8.
   This appears at the very beginning of an ELF binary. */
struct Elf32_Ehdr
  {
    unsigned char e_ident[16];
    Elf32_Half    e_type;
    Elf32_Half    e_machine;
    Elf32_Word    e_version;
    Elf32_Addr    e_entry;
    Elf32_Off     e_phoff;
    Elf32_Off     e_shoff;
    Elf32_Word    e_flags;
    Elf32_Half    e_ehsize;
    Elf32_Half    e_phentsize;
    Elf32_Half    e_phnum;
    Elf32_Half    e_shentsize;
    Elf32_Half    e_shnum;
    Elf32_Half    e_shstrndx;
  };

/* Program header.  See [ELF1] 2-2 to 2-4.
   There are e_phnum of these, starting at file offset e_phoff
   (see [ELF1] 1-6). */
struct Elf32_Phdr
  {
    Elf32_Word p_type;
    Elf32_Off  p_offset;
    Elf32_Addr p_vaddr;
    Elf32_Addr p_paddr;
    Elf32_Word p_filesz;
    Elf32_Word p_memsz;
    Elf32_Word p_flags;
    Elf32_Word p_align;
  };

/* Values for p_type.  See [ELF1] 2-3. */
#define PT_NULL    0            /* Ignore. */
#define PT_LOAD    1            /* Loadable segment. */
#define PT_DYNAMIC 2            /* Dynamic linking info. */
#define PT_INTERP  3            /* Name of dynamic loader. */
#define PT_NOTE    4            /* Auxiliary info. */
#define PT_SHLIB   5            /* Reserved. */
#define PT_PHDR    6            /* Program header table. */
#define PT_STACK   0x6474e551   /* Stack segment. */

/* Flags for p_flags.  See [ELF3] 2-3 and 2-4. */
#define PF_X 1          /* Executable. */
#define PF_W 2          /* Writable. */
#define PF_R 4          /* Readable. */

static bool setup_stack (void **esp);
static bool validate_segment (const struct Elf32_Phdr *, struct file *);
static bool load_segment (struct file *file, off_t ofs, uint8_t *upage,
                          uint32_t read_bytes, uint32_t zero_bytes,
                          bool writable);

/* Loads an ELF executable from FILE_NAME into the current thread.
   Stores the executable's entry point into *EIP
   and its initial stack pointer into *ESP.
   Returns true if successful, false otherwise. */
bool
load (const char *file_name, void (**eip) (void), void **esp) 
{
  struct thread *t = thread_current ();
  struct Elf32_Ehdr ehdr;
  struct file *file = NULL;
  off_t file_ofs;
  bool success = false;
  int i;

  /* Allocate and activate page directory. */
  t->pagedir = pagedir_create ();
  if (t->pagedir == NULL) 
  {
    //success=false;
    goto done;
  }
  process_activate ();
  t->mappedFiles = calloc(MAX_NUM_FILES, sizeof* t->mappedFiles);
  if(t->mappedFiles == NULL) goto done;

  /* Open executable file. */
  file = filesys_open (file_name);
  if (file == NULL) 
    {
      printf ("load: %s: open failed\n", file_name);
      //success=false;
      goto done; 
    }
  t->current_file = file;
  file_deny_write(file);
  /* Read and verify executable header. */
  if (file_read (file, &ehdr, sizeof ehdr) != sizeof ehdr
      || memcmp (ehdr.e_ident, "\177ELF\1\1\1", 7)
      || ehdr.e_type != 2
      || ehdr.e_machine != 3
      || ehdr.e_version != 1
      || ehdr.e_phentsize != sizeof (struct Elf32_Phdr)
      || ehdr.e_phnum > 1024) 
    {
      printf ("load: %s: error loading executable\n", file_name);
      goto done; 
    }

  /* Read program headers. */
  file_ofs = ehdr.e_phoff;
  for (i = 0; i < ehdr.e_phnum; i++) 
    {
      struct Elf32_Phdr phdr;

      if (file_ofs < 0 || file_ofs > file_length (file))
        goto done;
      file_seek (file, file_ofs);

      if (file_read (file, &phdr, sizeof phdr) != sizeof phdr)
        goto done;
      file_ofs += sizeof phdr;
      switch (phdr.p_type) 
        {
        case PT_NULL:
        case PT_NOTE:
        case PT_PHDR:
        case PT_STACK:
        default:
          /* Ignore this segment. */
          break;
        case PT_DYNAMIC:
        case PT_INTERP:
        case PT_SHLIB:
          goto done;
        case PT_LOAD:
          if (validate_segment (&phdr, file)) 
            {
              bool writable = (phdr.p_flags & PF_W) != 0;
              uint32_t file_page = phdr.p_offset & ~PGMASK;
              uint32_t mem_page = phdr.p_vaddr & ~PGMASK;
              uint32_t page_offset = phdr.p_vaddr & PGMASK;
              uint32_t read_bytes, zero_bytes;
              if (phdr.p_filesz > 0)
                {
                  /* Normal segment.
                     Read initial part from disk and zero the rest. */
                  read_bytes = page_offset + phdr.p_filesz;
                  zero_bytes = (ROUND_UP (page_offset + phdr.p_memsz, PGSIZE)
                                - read_bytes);
                }
              else 
                {
                  /* Entirely zero.
                     Don't read anything from disk. */
                  read_bytes = 0;
                  zero_bytes = ROUND_UP (page_offset + phdr.p_memsz, PGSIZE);
                }
              if (!load_segment (file, file_page, (void *) mem_page,
                                 read_bytes, zero_bytes, writable))
                goto done;
            }
          else
            goto done;
          break;
        }
    }

  /* Set up stack. */
  if (!setup_stack (esp))
    goto done;
  
  /* Start address. */
  *eip = (void (*) (void)) ehdr.e_entry;
  success = true;
 done:
  /* We arrive here whether the load is successful or not. */
  //file_close (file);
  return success;
}

/* load() helpers. */

static bool install_page (void *upage, void *kpage, bool writable);

/* Checks whether PHDR describes a valid, loadable segment in
   FILE and returns true if so, false otherwise. */
static bool
validate_segment (const struct Elf32_Phdr *phdr, struct file *file) 
{
  /* p_offset and p_vaddr must have the same page offset. */
  if ((phdr->p_offset & PGMASK) != (phdr->p_vaddr & PGMASK)) 
    return false; 

  /* p_offset must point within FILE. */
  if (phdr->p_offset > (Elf32_Off) file_length (file)) 
    return false;

  /* p_memsz must be at least as big as p_filesz. */
  if (phdr->p_memsz < phdr->p_filesz) 
    return false; 

  /* The segment must not be empty. */
  if (phdr->p_memsz == 0)
    return false;
  
  /* The virtual memory region must both start and end within the
     user address space range. */
  if (!is_user_vaddr ((void *) phdr->p_vaddr))
    return false;
  if (!is_user_vaddr ((void *) (phdr->p_vaddr + phdr->p_memsz)))
    return false;

  /* The region cannot "wrap around" across the kernel virtual
     address space. */
  if (phdr->p_vaddr + phdr->p_memsz < phdr->p_vaddr)
    return false;

  /* Disallow mapping page 0.
     Not only is it a bad idea to map page 0, but if we allowed
     it then user code that passed a null pointer to system calls
     could quite likely panic the kernel by way of null pointer
     assertions in memcpy(), etc. */
  if (phdr->p_vaddr < PGSIZE)
    return false;

  /* It's okay. */
  return true;
}

/* Loads a segment starting at offset OFS in FILE at address
   UPAGE.  In total, READ_BYTES + ZERO_BYTES bytes of virtual
   memory are initialized, as follows:

        - READ_BYTES bytes at UPAGE must be read from FILE
          starting at offset OFS.

        - ZERO_BYTES bytes at UPAGE + READ_BYTES must be zeroed.

   The pages initialized by this function must be writable by the
   user process if WRITABLE is true, read-only otherwise.

   Return true if successful, false if a memory allocation error
   or disk read error occurs. */
static bool
load_segment (struct file *file, off_t ofs, uint8_t *upage,
              uint32_t read_bytes, uint32_t zero_bytes, bool writable) 
{
  ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
  ASSERT (pg_ofs (upage) == 0);
  ASSERT (ofs % PGSIZE == 0);

  file_seek (file, ofs);
  while (read_bytes > 0 || zero_bytes > 0) 
    {
      /* Calculate how to fill this page.
         We will read PAGE_READ_BYTES bytes from FILE
         and zero the final PAGE_ZERO_BYTES bytes. */
      size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
      size_t page_zero_bytes = PGSIZE - page_read_bytes;

      /* Get a page of memory. */
      struct frame_table_entry *kpage = frametable_allocate (PAL_USER);
      if (kpage == NULL)
        return false;

      /* Load this page. */
      if (file_read (file, kpage->kernelPage, page_read_bytes) != (int) page_read_bytes)
        {
          frametable_free (kpage);
          return false; 
        }
      memset (kpage->kernelPage + page_read_bytes, 0, page_zero_bytes);

      /* Add the page to the process's address space. */
      if (!install_page (upage, kpage->kernelPage, writable)) 
        {
          frametable_free (kpage);
          return false; 
        }

      /* Advance. */
      read_bytes -= page_read_bytes;
      zero_bytes -= page_zero_bytes;
      upage += PGSIZE;
    }
  return true;
}

/* Create a minimal stack by mapping a zeroed page at the top of
   user virtual memory. */
static bool
setup_stack (void **esp) 
{
  struct frame_table_entry *kpage;
  bool success = false;

  kpage = frametable_allocate (PAL_ZERO);
  if (kpage != NULL) 
    {
      success = install_page (((uint8_t *) PHYS_BASE) - PGSIZE, kpage->kernelPage, true);
      if (success)
        *esp = PHYS_BASE;
      else
        frametable_free (kpage);
    }
  return success;
}

/* Adds a mapping from user virtual address UPAGE to kernel
   virtual address KPAGE to the page table.
   If WRITABLE is true, the user process may modify the page;
   otherwise, it is read-only.
   UPAGE must not already be mapped.
   KPAGE should probably be a page obtained from the user pool
   with palloc_get_page().
   Returns true on success, false if UPAGE is already mapped or
   if memory allocation fails. */
static bool
install_page (void *upage, void *kpage, bool writable)
{
  struct thread *t = thread_current ();

  /* Verify that there's not already a page at that virtual
     address, then map our page there. */
  return (pagedir_get_page (t->pagedir, upage) == NULL
          && pagedir_set_page (t->pagedir, upage, kpage, writable));
}