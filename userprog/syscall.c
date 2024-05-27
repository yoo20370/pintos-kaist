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
};

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

	if (fd != 0)
	{
		return -1;
	}
	else
	{
	}
};

int write(int fd, const void *buffer, unsigned length)
{
	// 주소 유효성 검사
	check_addr(buffer);

	if (fd != 1)
	{
		return -1;
	}
	else
	{
		putbuf(buffer, length);
		return length;
	}
};

int exec(const char *file) {
	// thread_current
};

bool create(const char *file, unsigned initial_size)
{
	check_addr(file);

	if (file == NULL || initial_size < 0)
	{
		return false;
	}

	return filesys_create(file, initial_size);
};

bool remove(const char *file)
{
	check_addr(file);

	return filesys_remove(file);
};

int open(const char *file)
{
	check_addr(file);

	struct file *curr_file = filesys_open(file);
	struct thread *curr = thread_current();
	curr->fdt[curr->fd++];
	if (curr_file == NULL)
	{
		return -1;
	}
	else
	{
		return 0;
	}
};

void close(int fd)
{
	struct thread *curr = thread_current();
	if (fd != NULL)
	{
		return;
	}
	else
	{
		curr->fdt[fd] = NULL;
	}
};

int filesize(int fd) {
	// struct thread *curr = thread_current();
	// int size = strlen(fd);
	// return size;
};

/* The main system call interface */
void syscall_handler(struct intr_frame *f UNUSED)
{
	/* 시스템 콜 넘버 */
	int sys_number = f->R.rax;

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
	// case 2:
	// 	fork();
	// 	break;
	case 3:
		exec(f->R.rdi);
		break;
	// case 4:
	// 	wait();
	// 	break;
	case 5:
		create(f->R.rdi, f->R.rsi);
		break;
	case 6:
		remove(f->R.rdi);
		break;
	case 7:
		open(f->R.rdi);
		break;
	case 8:
		filesize(f->R.rdi);
		break;
	case 9:
		read(f->R.rdi, f->R.rsi, f->R.rdx);
		break;
	case 10:
		write(f->R.rdi, f->R.rsi, f->R.rdx);
		break;
	// case 11:
	// 	seek();
	// 	break;
	// case 12:
	// 	tell();
	// 	break;
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
		thread_exit();
		break;
	}
}
