#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"
#include "filesys/filesys.h"
#include "filesys/file.h"
#include "threads/synch.h"
#include "userprog/process.h"
#include "threads/palloc.h"

void syscall_entry(void);
void syscall_handler(struct intr_frame *);

/* System call.
 *
 * Previously system call services was handled by the interrupt handler
 * (e.g. int 0x80 in linux). However, in x86-64, the manufacturer supplies
 * efficient path for requesting the system call, the `syscall` instruction.
 *
 * The syscall instruction works by reading the values from the the Model
 * Specific Register (MSR). For the details, see the manual. */

#define MSR_STAR 0xc0000081			/* Segment selector msr */
#define MSR_LSTAR 0xc0000082		/* Long mode SYSCALL target */
#define MSR_SYSCALL_MASK 0xc0000084 /* Mask for the eflags */

void syscall_init(void)
{
	write_msr(MSR_STAR, ((uint64_t)SEL_UCSEG - 0x10) << 48 |
							((uint64_t)SEL_KCSEG) << 32);
	write_msr(MSR_LSTAR, (uint64_t)syscall_entry);

	/* The interrupt service rountine should not serve any interrupts
	 * until the syscall_entry swaps the userland stack to the kernel
	 * mode stack. Therefore, we masked the FLAG_FL. */
	write_msr(MSR_SYSCALL_MASK,
			  FLAG_IF | FLAG_TF | FLAG_DF | FLAG_IOPL | FLAG_AC | FLAG_NT);
}

void check_addr(void *addr)
{
	// 가상메모리와 매핑된게 없거나 유저스택보다 같거나 크고 NULL이면 종료
	if (pml4_get_page(thread_current()->pml4, addr) == NULL || !is_user_vaddr(addr) || addr == NULL)
	{
		exit(-1);
	}
}

void halt(void)
{
	power_off();
}

void exit(int status)
{
	struct thread *curr = thread_current();
	curr->exit_status = status;
	printf("%s: exit(%d)\n", curr->name, status);
	thread_exit();
}

int read(int fd, void *buffer, unsigned size)
{
	check_addr(buffer);

	struct thread *curr = thread_current();
	struct file *curr_file = curr->fdt[fd];

	if (fd < 0 || fd >= 64)
	{
		return -1;
	}

	return file_read(curr_file, buffer, size);
}

int write(int fd, const void *buffer, unsigned length)
{
	check_addr(buffer);

	if (fd < 1 || fd > 63)
	{
		return -1;
	}
	if (fd == 1)
	{
		putbuf(buffer, length);
	}

	return length;
}

tid_t fork(const char *file_name, struct intr_frame *f)
{
	return process_fork(file_name, f);
}

int exec(const char *cmd_line)
{
	check_addr(cmd_line);

	char *fn_copy;

	fn_copy = palloc_get_page(PAL_ZERO);
	if (fn_copy == NULL)
		exit(-1);
	strlcpy(fn_copy, cmd_line, PGSIZE);

	if (process_exec(fn_copy) < 0)
		return -1;

	NOT_REACHED();
	return 0;
}

int wait(tid_t tid)
{
	return process_wait(tid);
}

bool create(const char *file, unsigned initial_size)
{
	check_addr(file);

	if (initial_size < 0)
	{
		return false;
	}
	else
	{
		return filesys_create(file, initial_size);
	}
}

bool remove(const char *file)
{
	check_addr(file);

	return filesys_remove(file);
};

int open(const char *file)
{
	check_addr(file);

	struct file *curr_file;
	struct thread *curr = thread_current();
	int target;
	curr_file = filesys_open(file);

	if (curr_file == NULL)
	{
		return -1;
	}

	curr->fdt[curr->next_fd] = curr_file; // fd에 파일 주소 삽입
	target = curr->next_fd;
	for (int i = curr->next_fd + 1; i < 64; i++)
	{
		if (curr->fdt[i] == NULL)
		{
			curr->next_fd = i;
			return target;
		}
	}

	return -1;
};

struct file *process_get_file(int fd)
{
	if (fd < 2 || fd > 64)
	{
		return NULL;
	}
	return thread_current()->fdt[fd];
}

void seek(int fd, unsigned position)
{
	struct file *open_file = process_get_file(fd);

	check_addr(open_file);

	if (fd < 2 || fd > 64)
		return;
	if (open_file)
		file_seek(open_file, position);
}

unsigned
tell(int fd)
{
	struct file *open_file = process_get_file(fd);

	check_addr(open_file);

	if (fd < 2 || fd > 64)
		return;
	if (open_file)
		return file_tell(open_file);
	return NULL;
}

void close(int fd)
{
	struct thread *curr = thread_current();
	if (fd != NULL)
	{
		return;
	}
	else
	{
		file_close(curr->fdt[fd]);
		curr->fdt[fd] = NULL;
	}
}

int filesize(int fd)
{
	struct thread *curr = thread_current();
	if (fd < 2 || fd > 63)
	{
		return -1;
	}

	struct file *curr_file = curr->fdt[fd];

	if (curr_file == NULL)
	{
		return -1;
	}

	return file_length(curr_file);
};

/* The main system call interface */
void syscall_handler(struct intr_frame *f UNUSED)
{
	/* 시스템 콜 넘버 */
	int sys_number = f->R.rax;
	// printf("sys_number: %d\n", sys_number);
	/*
	1번째 인자: %rdi
	2번째 인자: %rsi
	3번째 인자: %rdx
	4번째 인자: %r10
	5번째 인자: %r8
	6번째 인자: %r9
	*/

	switch (sys_number)
	{
	case 0:
		halt();
		break;
	case 1:
		exit(f->R.rdi);
		break;
	case 2:
		f->R.rax = fork(f->R.rdi, f);
		break;
	case 3:
		f->R.rax = exec(f->R.rdi);
		break;
	case 4:
		f->R.rax = wait(f->R.rdi);
		break;
	case 5:
		f->R.rax = create(f->R.rdi, f->R.rsi);
		break;
	case 6:
		f->R.rax = remove(f->R.rdi);
		break;
	case 7:
		f->R.rax = open(f->R.rdi);
		break;
	case 8:
		f->R.rax = filesize(f->R.rdi);
		break;
	case 9:
		f->R.rax = read(f->R.rdi, f->R.rsi, f->R.rdx);
		break;
	case 10:
		f->R.rax = write(f->R.rdi, f->R.rsi, f->R.rdx);
		break;
	case 11:
		seek(f->R.rdi, f->R.rsi);
		break;
	case 12:
		f->R.rax = tell(f->R.rdi);
		break;
	case 13:
		close(f->R.rdi);
		break;
		// case 14:
		// 	dup2();
		// 	break;
		// case 15:
		// 	mmap();
		// 	break;
		// case 16:
		// 	munmap();
		// 	break;
		// case 17:
		// 	chdir();
		// 	break;
		// case 18:
		// 	mkdir();
		// 	break;
		// case 19:
		// 	readdir();
		// 	break;
		// case 20:
		// 	isdir();
		// 	break;
		// case 21:
		// 	isnumber();
		// 	break;
		// case 22:
		// 	symlink();
		// 	break;
		// case 23:
		// 	mount();
		// 	break;
		// case 24:
		// 	unmount();
		// 	break;
	default:
		// thread_exit();
		break;
	}
}
