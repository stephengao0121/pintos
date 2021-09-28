#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"
#include "devices/shutdown.h"
#include "threads/synch.h"
#include "userprog/process.h"
#include "devices/block.h"
#include "filesys/free-map.h"
#include "filesys/directory.h"
#include "filesys/inode.h"
#include "filesys/filesys.h"
#include "filesys/file.h"
#include "threads/palloc.h"
#include "lib/kernel/list.h"
#include "devices/input.h"
#include "filesys/inode.h"

struct lock memory_lock;
static int fD_num = 1;

int read_physical_memory(char *address){
	int result;
	asm ("movl $1f, %0; movzbl %1, %0; 1:": "=&a" (result) : "m" (*address));
  return result;
}

void check_memory(void *address, int size){
    char *command = address;
    for(int i = 0; i < size; i ++){
        if(!check_if_valid((void *)(command + i)))
            sys_exit(-1, NULL);
    }
}

void read_memory(void *frame, char *esp, int size){
	for (int i = 0; i < size; i ++){
		if(check_if_valid(esp + i))
			*(char *)(frame + i) = read_physical_memory((esp + i)) & 0xff;
		else
			sys_exit(-1, NULL);
	}
}

bool write_memory(unsigned char *address, unsigned char byte)
{
    if(check_if_valid(address)){
        int error;
        asm ("movl $1f, %0; movb %b2, %1; 1:": "=&a" (error), "=m" (*address) : "q" (byte));
        return error != -1;
    }
    else
	 sys_exit(-1, NULL);
}

static void syscall_handler (struct intr_frame *frame){
	int syscall_num;
	void *esp = frame->esp;
	if(!check_if_valid(esp) && !check_if_valid(esp + 4) && !check_if_valid(esp + 8) && !check_if_valid(esp + 12))
		sys_exit(-1, NULL);
	read_memory(&syscall_num, esp, sizeof(syscall_num));
	switch(syscall_num){
		case SYS_HALT:
		{
			sys_halt();
			break;
		}
		case SYS_EXIT:
		{
			int exit_code;
			read_memory(&exit_code, esp + 4, sizeof(exit_code));
			sys_exit(exit_code, frame);
			break;
		}
		case SYS_EXEC:
		{
			void *command;
			read_memory(&command, esp + 4, sizeof(command));
			sys_exec(command, frame);
			break;
		}
		case SYS_WAIT:
		{
			int tid;
			read_memory(&tid, esp + 4, sizeof(tid));
			sys_wait(tid, frame);
			break;
		}
		case SYS_CREATE:
		{
			char *name;
			size_t size;
			read_memory(&name, esp + 4, sizeof(name));
			read_memory(&size, esp + 8, sizeof(size));
			sys_create(name, size, frame);
			break;
		}
		case SYS_OPEN:
		{
			char *name;
			read_memory(&name, esp + 4, sizeof(name));
			sys_open(name, frame);
			break;
		}
		case SYS_FILESIZE:
		{
			int fd;
			read_memory(&fd, esp + 4, sizeof(fd));
			sys_filesize(fd, frame);
			break;
		}
		case SYS_READ:
		{
			int fd, size;
			void *buffer;
			read_memory(&fd, esp + 4, sizeof(fd));
			read_memory(&buffer, esp + 8, sizeof(buffer));
			read_memory(&size, esp + 12, sizeof(size));
			sys_read(fd, buffer, size, frame);
			break;
		}
		case SYS_WRITE:
		{
			int fd, size;
			void *buffer;
			read_memory(&fd, esp + 4, sizeof(fd));
			read_memory(&buffer, esp + 8, sizeof(buffer));
			read_memory(&size, esp + 12, sizeof(size));
			sys_write(fd, buffer, size, frame);
			break;
		}
		case SYS_SEEK:
		{
			int fd, count;
			read_memory(&fd, esp + 4, sizeof(fd));
			read_memory(&count, esp + 8, sizeof(count));
			sys_seek(fd, count, frame);
			break;
		}
		case SYS_TELL:
		{
			int fd;
			read_memory(&fd, esp + 4, sizeof(fd));
			sys_tell(fd, frame);
			break;
		}
		case SYS_CLOSE:
		{
			int fd;
			read_memory(&fd, esp + 4, sizeof(fd));
			sys_close(fd, frame);
			break;
		}
        case SYS_REMOVE:
        {
            char *name;
            read_memory(&name, esp + 4, sizeof(name));
            sys_remove(name, frame);
            break;
        }
	    case SYS_CHDIR:
        {
            char *dir;
            read_memory(&dir, esp + 4, sizeof(dir));
            frame->eax = filesys_chdir(dir);
            break;
        }
	    case SYS_MKDIR:
        {
            char *dir;
            read_memory(&dir, esp + 4, sizeof(dir));
            frame->eax = filesys_mkdir(dir);
            break;
        }
	    case SYS_READDIR:
        {
            int fd;
            char *name;
            read_memory(&fd, esp + 4, sizeof(fd));
            read_memory(&name, esp + 8, sizeof(name));
            sys_readdir(fd, name, frame);
            break;
        }
	    case SYS_ISDIR:
        {
            int fd;
            read_memory(&fd, esp + 4, sizeof(fd));
            sys_isdir(fd, frame);
            break;
        }
	    case SYS_INUMBER:
        {
            int fd;
            read_memory(&fd, esp + 4, sizeof(fd));
            sys_inumber(fd, frame);
            break;
        }
		default:
		{
			int exit_code;
			read_memory(&exit_code, esp + 4, sizeof(exit_code));
			sys_exit(exit_code, NULL);
		}

	}
}

void
syscall_init (void)
{
    lock_init(&memory_lock);
    intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

bool check_if_valid(void *address){
	if((address != NULL) && ((unsigned int)address < (unsigned int) PHYS_BASE)){
		if((pagedir_get_page(thread_current()->pagedir, address)) != NULL)
			return true;
		else return false;
	}
	return false;
}

void sys_halt(void)
{
    shutdown_power_off();
}

void sys_exit(int exit_code, struct intr_frame *frame){
	struct TCB *TCB = thread_current()->TCB;
	printf("%s: exit(%d)\n", thread_current()->name, exit_code);
	if(TCB)
		TCB->exit_code = exit_code;
	thread_exit();
}

void sys_exec(const char *cmd_line, struct intr_frame *frame)
{
    lock_acquire(&memory_lock);
    frame->eax = process_execute(cmd_line);
    lock_release(&memory_lock);
}

void sys_wait(pid_t pid, struct intr_frame *frame)
{
    frame->eax = process_wait(pid);
}

void sys_create(const char *file, unsigned initial_size, struct intr_frame *frame)
{
    check_memory(file, sizeof(file));
    lock_acquire(&memory_lock);
    frame->eax = filesys_create(file, initial_size, false);
    lock_release(&memory_lock);
}

void sys_remove(const char *file, struct intr_frame *frame)
{
    lock_acquire(&memory_lock);
    frame->eax = filesys_remove(file);
    lock_release(&memory_lock);
}

void sys_open(const char *file, struct intr_frame *frame)
{
    check_memory(file, sizeof(file));
    struct file *f = NULL;
    struct fileDescriptor *fD = NULL;
    struct inode *inode;
    lock_acquire(&memory_lock);
    fD = palloc_get_page(0);
    if (fD == NULL){ /* check if palloc success. */
        palloc_free_page(fD);
        frame->eax = -1;
        lock_release(&memory_lock);
        return;
    }
    f = filesys_open(file);
    if (f == NULL){ /* check if file open success. */
        palloc_free_page(fD);
        frame->eax = -1;
        lock_release(&memory_lock);
        return;
    }
    /* initialize fd. */
    fD_num++;
    fD->file = f;
    fD->fD_id = fD_num;
    fD->t = thread_current();
    list_push_back(&(thread_current()->fD), &(fD->elem));
    inode = file_get_inode(f);
    if(inode != NULL && inode_is_dir(inode)) fD->dir = inode;
    else fD->dir = NULL;

    frame->eax = fD->fD_id;
    lock_release(&memory_lock);
}

void sys_filesize(int fd, struct intr_frame *frame)
{
    struct list_elem *e;
    struct fileDescriptor *fD;
    if(!list_empty(&thread_current()->fD)) {
        lock_acquire(&memory_lock);
        for (e = list_front(&thread_current()->fD);; e = list_next(e)) { /* find the corresponding fD. */
            fD = list_entry(e, struct fileDescriptor, elem);
            if (fD->fD_id == fd) break;
            if (e == list_tail(&thread_current()->fD) && (fD->fD_id != fd)) {
                frame->eax = -1;
                lock_release(&memory_lock);
                return;
            }
        }
        if(fD != NULL){
            frame->eax = file_length(fD->file);
            lock_release(&memory_lock);
            return;
        }
        else{
            frame->eax = -1;
            lock_release(&memory_lock);
            return;
        }
    }
}

void sys_read(int fd, void *buffer, unsigned size, struct intr_frame *frame)
{
    check_memory(buffer, sizeof(buffer));
    check_memory(buffer, sizeof(buffer) + size - 1);
    struct list_elem *e;
    struct fileDescriptor *fD;
    unsigned i;
    lock_acquire(&memory_lock);
    if(fd > fD_num){ /* check if invalid read. */
        frame->eax = -1;
        lock_release(&memory_lock);
        return;
    }
    if(fd == STDIN_FILENO){ /* check if input file. */
        for(i = 0; i < size; i++){
            write_memory((unsigned char*)(buffer + i), input_getc());
	}
            frame->eax = size;
            lock_release(&memory_lock);
            return;
    }
    if(fd == STDOUT_FILENO){ /* check if output file. */
        frame->eax = -1;
        lock_release(&memory_lock);
        return;
    }
    if(!list_empty(&thread_current()->fD)) {
        for (e = list_front(&thread_current()->fD);; e = list_next(e)) { /* find the corresponding fD. */
            fD = list_entry(e, struct fileDescriptor, elem);
            if (fD->fD_id == fd) break;
            if (e == list_tail(&thread_current()->fD) && (fD->fD_id != fd)) {
                frame->eax = -1;
                lock_release(&memory_lock);
                return;
            }
        }
    }
    frame->eax = file_read(fD->file, buffer, size);
    lock_release(&memory_lock);
    return;
    
}

void sys_write(int fd, const void *buffer, unsigned size, struct intr_frame *frame){
    struct inode *inode;
	if(buffer == NULL) sys_exit(-1, NULL);
	if(!check_if_valid(buffer)||!check_if_valid(buffer + size)) sys_exit(-1, NULL);

	lock_acquire(&memory_lock);
	/*write to the console*/
	if(fd == 1){
		putbuf(buffer, size);
		lock_release(&memory_lock);
		frame->eax = size;
	}
	/*doesn't have to write to the console*/
	else if(fd == 0){
		frame->eax = -1;
		lock_release(&memory_lock);
		return;
	}
	else{
		struct list_elem *temp;
		struct fileDescriptor *fD = NULL;
		if(!list_empty(&thread_current()->fD)){
			for(temp = list_front(&thread_current()->fD); ; temp = list_next(temp)){
				fD = list_entry(temp, struct fileDescriptor, elem);
				if(fD->fD_id == fd) break;
				if(temp == list_tail(&thread_current()->fD) && (fD->fD_id != fd)){
					lock_release(&memory_lock);
					frame->eax = -1;
					return;
				}
			}
		}
        inode  = file_get_inode(fD->file);
        if(inode_is_dir(inode)){
            lock_release(&memory_lock);
            frame->eax = -1;
            return;
        }
		if(fD != NULL){
			frame->eax = file_write(fD->file, buffer, size);
			lock_release(&memory_lock);
			return;
		}
		else{
			lock_release(&memory_lock);
			frame->eax = -1;
			return;
		}
	}
}

void sys_seek(int fd, unsigned position, struct intr_frame *frame)
{
    struct list_elem *e;
    struct fileDescriptor *fD;
    if(!list_empty(&thread_current()->fD)) {
        lock_acquire(&memory_lock);
        for (e = list_front(&thread_current()->fD);; e = list_next(e)) { /* find the corresponding fD. */
            fD = list_entry(e,
            struct fileDescriptor, elem);
            if (fD->fD_id == fd) break;
            if (e == list_tail(&thread_current()->fD) && (fD->fD_id != fd)) {
                lock_release(&memory_lock);
                return;
            }
        }
        if(fD != NULL){
            file_seek(fD->file, position);
            lock_release(&memory_lock);
            return;
        }
        else{
            lock_release(&memory_lock);
            return;
        }
    }
}

void sys_tell(int fd, struct intr_frame *frame)
{
    struct list_elem *e;
    struct fileDescriptor *fD;
    if(!list_empty(&thread_current()->fD)) {
        lock_acquire(&memory_lock);
        for (e = list_front(&thread_current()->fD);; e = list_next(e)) { /* find the corresponding fD. */
            fD = list_entry(e,
            struct fileDescriptor, elem);
            if (fD->fD_id == fd) break;
            if (e == list_tail(&thread_current()->fD) && (fD->fD_id != fd)) {
                frame->eax = -1;
                lock_release(&memory_lock);
                return;
            }
        }
        if(fD != NULL){
            frame->eax = file_tell(fD->file);
            lock_release(&memory_lock);
            return;
        }
        else{
            frame->eax = -1;
            lock_release(&memory_lock);
            return;
        }
    }
}

void sys_close(int fd, struct intr_frame *frame)
{
    struct list_elem *e;
    struct fileDescriptor *fD;
    if(!list_empty(&thread_current()->fD)){
        lock_acquire(&memory_lock);
        for (e = list_front(&thread_current()->fD); ; e = list_next(e)){ /* find the corresponding fD. */
            fD = list_entry(e, struct fileDescriptor, elem);
            if(fD->fD_id == fd) break;
            if(e == list_tail(&thread_current()->fD) && (fD->fD_id != fd)){
                lock_release(&memory_lock);
                return;
            }
        }
        if(thread_current()->tid == fD->t->tid){ /* check if current holds fD. */
            lock_release(&memory_lock);
            if (fD->dir != NULL) dir_close(fD->dir);
            file_close(fD->file);
            list_remove(&(fD->elem));
            palloc_free_page(fD);
            return;
        }
    }
}

void sys_readdir(int fd, char *name, struct intr_frame *frame)
{
    struct list_elem *e;
    struct fileDescriptor *fD;
    struct inode *inode;
    if(!list_empty(&thread_current()->fD)) {
        lock_acquire(&memory_lock);
        for (e = list_front(&thread_current()->fD);; e = list_next(e)) { /* find the corresponding fD. */
            fD = list_entry(e,
            struct fileDescriptor, elem);
            if (fD->fD_id == fd) break;
            if (e == list_tail(&thread_current()->fD) && (fD->fD_id != fd)) {
                lock_release(&memory_lock);
                frame->eax = false;
                return;
            }
        }
    }
    lock_release(&memory_lock);
    inode = file_get_inode(fD->file);
    ASSERT(fD->dir != NULL);
    if (inode == NULL || !inode_is_dir(inode)){
        frame->eax = false;
        return;
    }
    frame->eax = dir_readdir(fD->dir, name);
    return;
}

void sys_isdir(int fd, struct intr_frame *frame)
{
    struct list_elem *e;
    struct fileDescriptor *fD;
    struct inode *inode;
    if(!list_empty(&thread_current()->fD)) {
        lock_acquire(&memory_lock);
        for (e = list_front(&thread_current()->fD);; e = list_next(e)) { /* find the corresponding fD. */
            fD = list_entry(e,
            struct fileDescriptor, elem);
            if (fD->fD_id == fd) break;
            if (e == list_tail(&thread_current()->fD) && (fD->fD_id != fd)) {
                lock_release(&memory_lock);
                frame->eax = false;
                return;
            }
        }
    }
    lock_release(&memory_lock);
    inode = file_get_inode(fD->file);
    if (inode == NULL){
        frame->eax = false;
        return;
    }
    frame->eax = inode_is_dir(inode);
    return;
}

void sys_inumber(int fd, struct intr_frame *frame)
{
    struct list_elem *e;
    struct fileDescriptor *fD;
    struct inode *inode;
    if(!list_empty(&thread_current()->fD)) {
        lock_acquire(&memory_lock);
        for (e = list_front(&thread_current()->fD);; e = list_next(e)) { /* find the corresponding fD. */
            fD = list_entry(e,
            struct fileDescriptor, elem);
            if (fD->fD_id == fd) break;
            if (e == list_tail(&thread_current()->fD) && (fD->fD_id != fd)) {
                lock_release(&memory_lock);
                frame->eax = false;
                return;
            }
        }
    }
    lock_release(&memory_lock);
    inode = file_get_inode(fD->file);
    if (inode == NULL){
        frame->eax = false;
        return;
    }
    frame->eax = inode_get_id(inode);
    return;
}