#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"
#include "threads/init.h"
#include "devices/input.h"
#include "filesys/filesys.h"
#include "filesys/file.h"
#include "stdbool.h"

void syscall_entry(void);
void syscall_handler(struct intr_frame *);
static bool is_valid_access(struct intr_frame *f);

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

/* The main system call interface */
void syscall_handler(struct intr_frame *f UNUSED)
{
	// TODO: Your implementation goes here.
	switch (f->R.rax)
	{
	case SYS_HALT: /* Halt the operating system. */
		halt();
		break;
	case SYS_EXIT: /* Terminate this process. */
		exit(f->R.rdi);
		break;
	case SYS_FORK: /* Clone current process. */
		break;
	case SYS_EXEC: /* Switch current process. */
		break;
	case SYS_WAIT: /* Wait for a child process to die. */
		break;
	case SYS_CREATE: /* Create a file. */
	{
		if (!is_valid_access(f))
			exit(-1);

		const char *file = (const char *)f->R.rdi;
		unsigned initial_size = (unsigned)f->R.rsi;

		bool success = create(file, initial_size);
		f->R.rax = success;
	}
	break;

	case SYS_REMOVE: /* Delete a file. */
		break;
	case SYS_OPEN: /* Open a file. */
		break;
	case SYS_FILESIZE: /* Obtain a file's size. */
		break;
	case SYS_READ: /* Read from a file. */
		break;
	case SYS_WRITE: /* Write to a file. */
		f->R.rax = write(f->R.rdi, f->R.rsi, f->R.rdx);
		break;
	case SYS_SEEK: /* Change position in a file. */
		break;
	case SYS_TELL: /* Report current position in a file. */
		break;
	case SYS_CLOSE: /* Close a file. */
		break;
	default:
		break;
	}
}

static bool is_valid_access(struct intr_frame *f)
{
	struct thread *t = thread_current();

	if (!pml4_get_page(t->pml4, f->R.rdi) || !is_user_vaddr(f->R.rdi))
		return false;

	return true;
}

void halt(void)
{
	power_off();
}

void exit(int status)
{
	struct thread *cur = thread_current();
	cur->exit_status = status;
	thread_exit();
}

// pid_t fork(const char *thread_name)
// {
// 	return (pid_t)syscall1(SYS_FORK, thread_name);
// }

// int exec(const char *file)
// {
// 	return (pid_t)syscall1(SYS_EXEC, file);
// }

// int wait(pid_t pid)
// {
// 	return syscall1(SYS_WAIT, pid);
// }

bool create(const char *file, unsigned initial_size)
{
	if (!file || initial_size < 0)
		return false;

	/* The validation right below is actually redundant :( */
	struct file *existing_file = filesys_open(file);
	if (existing_file)
	{
		file_close(existing_file);
		return false;
	}

	return filesys_create(file, initial_size);
}

// bool remove(const char *file)
// {
// 	return syscall1(SYS_REMOVE, file);
// }

// int open(const char *file)
// {
// 	return syscall1(SYS_OPEN, file);
// }

// int filesize(int fd)
// {
// 	return syscall1(SYS_FILESIZE, fd);
// }

// int read(int fd, void *buffer, unsigned size)
// {
// 	input_getc();
// }

int write(int fd, const void *buffer, unsigned size)
{
	if (fd < 0 || !buffer || size <= 0)
		return -1;

	putbuf(buffer, size); // char*, size_t
	return 1;
}

// void seek(int fd, unsigned position)
// {
// 	syscall2(SYS_SEEK, fd, position);
// }

// unsigned
// tell(int fd)
// {
// 	return syscall1(SYS_TELL, fd);
// }

// void close(int fd)
// {
// 	syscall1(SYS_CLOSE, fd);
// }