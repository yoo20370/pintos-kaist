#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"

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
	// 가상메모리와 매핑된게 없거나 유저스택보다 같거나 크거나 NULL이면 종료
	if (pml4_get_page(thread_current()->pml4, addr) == NULL || addr >= USER_STACK || addr == NULL)
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
	// thread_current()->status = THREAD_DYING;    //thread_exit에서 수행됨
	struct thread *curr = thread_current();
	curr->exit_status = status;
	printf("%s: exit(%d)\n", curr->name, status);
	thread_exit();
}

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
	}
};

int exec(const char *file)
{
	printf("%s\n", file);
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
		printf("halt 함수 \n");
		halt();
		break;
	case 1:
		// printf("exit 함수 \n");
		exit(f->R.rdi);
		break;
	// case 2:
	// 	fork();
	// 	break;
	// case 3:
	// 	printf("exec 함수 \n");
	// 	exec();
	// 	break;
	// case 4:
	// 	wait();
	// 	break;
	// case 5:
	// 	create();
	// 	break;
	// case 6:
	// 	remove();
	// 	break;
	// case 7:
	// 	open();
	// 	break;
	// case 8:
	// 	filesize();
	// 	break;
	// case 9:
	// 	read();
	// 	break;
	case 10:
		// 받아온 인자 순서대로 삽입
		write(f->R.rdi, f->R.rsi, f->R.rdx);
		break;
		// case 11:
		// 	seek();
		// 	break;
		// case 12:
		// 	tell();
		// 	break;
		// case 13:
		// 	close();
		// 	break;
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
	}
	// 여기서 쓰레드 종료하면 한번만 실행되고 끝남
	// thread_exit();
}
