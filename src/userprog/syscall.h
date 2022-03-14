#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

#include <stdbool.h>
typedef int pid_t;
typedef int mapid_t;

// void valid_address(void *addr, void *esp);
// void check_string(char *str, unsigned size, void *esp);
// struct vm_entry *check_address(void *addr, void* esp);
// void check_valid_buffer (void *buffer, unsigned size, void *esp, bool to_write);

void get_argument(void *esp, int *arg, int count);

void syscall_init (void);
void halt (void);
void exit (int status);
pid_t exec (const char *cmd_lime);
int wait (pid_t pid);
bool create (const char *file, unsigned initial_size);
bool remove (const char *file);
int open (const char *file);
int filesize (int fd);
int read (int fd, void *buffer, unsigned size);
int write (int fd, const void *buffer, unsigned size);
void seek (int fd, unsigned position);
unsigned tell (int fd);
void close (int fd);
mapid_t mmap(int fd, void *addr);
void munmap(mapid_t mapid);


#endif /* userprog/syscall.h */
