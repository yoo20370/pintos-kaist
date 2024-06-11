#ifndef __LIB_USER_SYSCALL_H
#define __LIB_USER_SYSCALL_H

#include <userprog/syscall.h>
#include <stdbool.h>
#include "lib/kernel/console.h"
#include <stdbool.h>
#include <debug.h>
#include <stddef.h>
#include "threads/synch.h"
/* Process identifier. */
typedef int pid_t;
#define PID_ERROR ((pid_t) -1)

// Implements
void halt (void);
void exit (int status);
int fork (const char *thread_name);
int exec (const char *cmd_line);
int wait (int pid);
bool create (const char *file, unsigned initial_size);
bool remove (const char *file);
int open (const char *file);
int filesize (int fd);
int read (int fd, void *buffer, unsigned size);
int write (int fd, const void *buffer, unsigned size);
void seek (int fd, unsigned position);
unsigned tell (int fd);
void close (int fd);

/* Map region identifier. */
typedef int off_t;
#define MAP_FAILED ((void *) NULL)

/* Maximum characters in a filename written by readdir(). */
#define READDIR_MAX_LEN 14

/* Typical return values from main() and arguments to exit(). */
#define EXIT_SUCCESS 0          /* Successful execution. */
#define EXIT_FAILURE 1          /* Unsuccessful execution. */

/* Projects 2 and later. */
void halt (void) NO_RETURN;
void exit (int status) NO_RETURN;
pid_t fork (const char *thread_name);
int exec (const char *file);
int wait (pid_t);
bool create (const char *file, unsigned initial_size);
bool remove (const char *file);
int open (const char *file);
int filesize (int fd);
int read (int fd, void *buffer, unsigned length);
int write (int fd, const void *buffer, unsigned length);
void seek (int fd, unsigned position);
unsigned tell (int fd);
void close (int fd);
void syscall_init(void);

int dup2(int oldfd, int newfd);

/* Project 3 and optionally project 4. */
void *mmap (void *addr, size_t length, int writable, int fd, off_t offset);
void munmap (void *addr);

/* Project 4 only. */
bool chdir (const char *dir);
bool mkdir (const char *dir);
bool readdir (int fd, char name[READDIR_MAX_LEN + 1]);
bool isdir (int fd);
int inumber (int fd);
int symlink (const char* target, const char* linkpath);

static inline void* get_phys_addr (void *user_addr) {
	void* pa;
	asm volatile ("movq %0, %%rax" ::"r"(user_addr));
	asm volatile ("int $0x42");
	asm volatile ("\t movq %%rax, %0": "=r" (pa));
	return pa;
}

static inline long long
get_fs_disk_read_cnt (void) {
	long long read_cnt;
	asm volatile ("movq $0, %rdx");
	asm volatile ("movq $1, %rcx");
	asm volatile ("int $0x43");
	asm volatile ("\t movq %%rax, %0": "=r" (read_cnt));
	return read_cnt;
}

static inline long long
get_fs_disk_write_cnt (void) {
	long long write_cnt;
	asm volatile ("movq $0, %rdx");
	asm volatile ("movq $1, %rcx");
	asm volatile ("int $0x44");
	asm volatile ("\t movq %%rax, %0": "=r" (write_cnt));
	return write_cnt;
}

#endif /* lib/user/syscall.h */
