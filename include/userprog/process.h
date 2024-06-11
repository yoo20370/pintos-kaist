#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"
typedef int32_t off_t;

tid_t process_create_initd (const char *file_name);
tid_t process_fork (const char *name, struct intr_frame *if_);
int process_exec (void *f_name);
int process_wait (tid_t);
void process_exit (void);
void process_activate (struct thread *next);
void process_exit_file(void);
int process_add_file_(struct file *f);
struct file *process_get_file(int fd);
void process_close_file(int fd);
void argument_stack(char *argv[], int argc, struct intr_frame *if_);
// void argument_stack(char **parse, int count, void **rsp);
struct thread* get_child_process (int pid);
void remove_child_process(struct thread *cp);
bool lazy_load_segment (struct page *page, void *aux);

#endif /* userprog/process.h */
