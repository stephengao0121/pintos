#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H
#include "threads/interrupt.h"

typedef int pid_t;

bool check_if_valid(void *address);
int read_physical_memory(char *address);
void check_memory(void *address, int size);
void read_memory(void *frame, char *esp, int size);
bool write_memory(unsigned char *address, unsigned char byte);
void syscall_init (void);

/* syscall implementation.*/
void sys_halt(void);
void sys_exit(int exit_code, struct intr_frame *frame);
void sys_exec(const char *cmd_line, struct intr_frame *frame);
void sys_wait(pid_t pid, struct intr_frame *frame);
void sys_create(const char *file, unsigned initial_size, struct intr_frame *frame);
void sys_remove(const char *file, struct intr_frame *frame);
void sys_open(const char *file, struct intr_frame *frame);
void sys_filesize(int fd, struct intr_frame *frame);
void sys_read(int fd, void *buffer, unsigned size, struct intr_frame *frame);
void sys_write(int fd, const void *buffer, unsigned size, struct intr_frame *frame);
void sys_seek(int fd, unsigned position, struct intr_frame *frame);
void sys_tell(int fd, struct intr_frame *frame);
void sys_close(int fd, struct intr_frame *frame);
void sys_readdir(int fd, char *name, struct intr_frame *frame);
void sys_isdir(int fd, struct intr_frame *frame);
void sys_inumber(int fd, struct intr_frame *frame);

#endif /* userprog/syscall.h */
