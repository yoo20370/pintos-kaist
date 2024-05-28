#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

void syscall_init(void);
void check_addr(void *addr);
void halt(void);
void exit(int status);
int read(int fd, void *buffer, unsigned size);
int write(int fd, const void *buffer, unsigned length);
int exec(const char *file);
int open(const char *file);
void seek(int fd, unsigned position);
unsigned tell(int fd);
void close(int fd);
int filesize(int fd);

#endif /* userprog/syscall.h */
