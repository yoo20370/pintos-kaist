#include "userprog/syscall.h"
#include <stdio.h>
#include "filesys/file.h"
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"
#include "userprog/process.h"
#include "filesys/filesys.h"
#include <devices/input.h>
#include "lib/kernel/stdio.h"
#include "threads/synch.h"
#include "threads/palloc.h"
#include "vm/vm.h"

// #define STDIN_FILENO 0
// #define STDOUT_FILENO 1

void syscall_entry (void);
void syscall_handler (struct intr_frame *);
void is_valid_addr(void *addr);
void halt (void); // 0 
void exit (int status);
bool create (const char *file, unsigned initial_size);
int open (const char *file);
bool remove(const char *file);
int filesize (int fd);
int read (int fd, void *buffer, unsigned size);
int write (int fd, const void *buffer, unsigned size);
void seek(int fd, unsigned position);
unsigned tell(int fd);
int exec(const char *cmd_line);
int wait(int pid);
int fork(const char *thread_name, struct intr_frame *f);
struct file* get_file(int fd);
void * mmap(void *addr, size_t length, int writable, int fd, off_t offset);
void munmap(void* addr);

#define EOF -1

struct lock filesys_lock;


/* System call.
 *
 * Previously system call services was handled by the interrupt handler
 * (e.g. int 0x80 in linux). However, in x86-64, the manufacturer supplies
 * efficient path for requesting the system call, the `syscall` instruction.
 *
 * The syscall instruction works by reading the values from the the Model
 * Specific Register (MSR). For the details, see the manual. */

#define MSR_STAR 0xc0000081         /* Segment selector msr */
#define MSR_LSTAR 0xc0000082        /* Long mode SYSCALL target */
#define MSR_SYSCALL_MASK 0xc0000084 /* Mask for the eflags */

void
syscall_init (void) {
	write_msr(MSR_STAR, ((uint64_t)SEL_UCSEG - 0x10) << 48  |
			((uint64_t)SEL_KCSEG) << 32);
	write_msr(MSR_LSTAR, (uint64_t) syscall_entry);
	/* The interrupt service rountine should not serve any interrupts
	 * until the syscall_entry swaps the userland stack to the kernel
	 * mode stack. Therefore, we masked the FLAG_FL. */
	write_msr(MSR_SYSCALL_MASK,
			FLAG_IF | FLAG_TF | FLAG_DF | FLAG_IOPL | FLAG_AC | FLAG_NT);
	lock_init(&filesys_lock);
}

/* The main system call interface */
void
syscall_handler (struct intr_frame *f UNUSED) {

	int syscall_n = f->R.rax; /* 시스템 콜 넘버 */

// 아마 Stack_growth를 위한 코드인듯 
#ifdef VM
	thread_current()->rsp = f->rsp;
#endif

	switch (syscall_n)
	{
	case SYS_HALT:
		halt();
		break;
	case SYS_EXIT:
		exit(f->R.rdi);
		break;
	case SYS_FORK:
		f->R.rax = fork(f->R.rdi, f);
		break;
	case SYS_EXEC:
	    f->R.rax = exec(f->R.rdi);
		break;
	case SYS_WAIT:
		f->R.rax = wait(f->R.rdi);
		break;
	case SYS_CREATE:
		f->R.rax = create(f->R.rdi, f->R.rsi);
		break;
	case SYS_REMOVE:
		f->R.rax = remove(f->R.rdi);
		break;
	case SYS_OPEN:
		f->R.rax = open(f->R.rdi);
		break;
	case SYS_FILESIZE:
		f->R.rax = filesize(f->R.rdi);
		break;
	case SYS_READ:
		f->R.rax = read(f->R.rdi, f->R.rsi, f->R.rdx);
		break;
	case SYS_WRITE:
		f->R.rax = write(f->R.rdi, f->R.rsi, f->R.rdx);
		break;
	case SYS_SEEK:
		seek(f->R.rdi, f->R.rsi);
		break;
	case SYS_TELL:
		f->R.rax = tell(f->R.rdi);
		break;
	case SYS_CLOSE:
		close(f->R.rdi);
		break;
	case SYS_MMAP :
		f->R.rax = mmap(f->R.rdi, f->R.rsi, f->R.rdx, f->R.rcx, f->R.r8);
		break;
	case SYS_MUNMAP :
		munmap(f->R.rdi);
		break;
	default:
        exit(-1);
        break;
	}
}

void is_valid_addr(void *addr) {
	if (addr == NULL || !is_user_vaddr(addr)) {
			exit(-1);
	}

}

void halt (void) {
	power_off();
}

void exit (int status) {
	struct thread *curr = thread_current();
	curr->exit_code = status;
	printf ("%s: exit(%d)\n", curr->name,status);
	thread_exit();
}

int
wait(int pid) {
	return process_wait(pid);
}

int
exec(const char *cmd_line) {
	//  cmd_line: 새로운 프로세스에 실행할 프로그램 명령어
	is_valid_addr(cmd_line);

	// int child_tid = process_create_initd(cmd_line);
	// if ( child_tid == -1) {
	// 	return -1;
	// }
	// struct thread* child = get_child_process(child_tid);

	// sema_down(&child->wait_sema);
	// /* 자식 프로세스 디스크립터 삭제*/
	// list_remove(&child->child_elem);
	// /* 자식 프로세스의 exit status 리턴*/
	// sema_up(&child->exit_sema);

	//NOT_REACHED();
	//return 0;
	struct thread * cur = thread_current();
	
	char *fn_copy = palloc_get_page (0); // 이거 임포트 제대로 안돼서 에러남.
	if (fn_copy == NULL) { 
		exit(-1); 
	}
	strlcpy (fn_copy, cmd_line, PGSIZE);

	if (process_exec(fn_copy) == -1) { 
		exit(-1);
	}
	NOT_REACHED();
}

bool create (const char *file, unsigned initial_size) {
	is_valid_addr(file);
	return filesys_create(file,initial_size);
}

bool remove(const char *file) {
	is_valid_addr(file);
	lock_acquire(&filesys_lock);
	bool success = filesys_remove(file);
	lock_release(&filesys_lock);
	return success;
}

int open (const char *file) {
	is_valid_addr(file);
	
	struct file* tmp;
	int fd = -1;
	lock_acquire(&filesys_lock);
	tmp = filesys_open(file);
	lock_release(&filesys_lock);
	if (tmp == NULL) {
		return -1;
	}

	if(strcmp(thread_name(), file) == 0) {
		file_deny_write(tmp); // 현재 읽고 있는 파일은 write를 할 수 없도록 설정합니다.
	}

	fd = process_add_file(tmp);
	if (fd == -1) {
		file_close(tmp);  // file.c 
	}

	return fd;
}


void seek(int fd, unsigned position) {
	if (fd < 2 || fd > 128) {
		return;
	}
	struct file *tmp = process_get_file(fd);
	if (tmp) {
		file_seek(tmp, position);
	}
}

unsigned tell(int fd) {
	if (fd < 2 || fd > 128)
		return;
	struct file *tmp = process_get_file(fd);
	if (tmp)
		return file_tell(tmp);
	return NULL;
}

void 
close (int fd) {
	struct file *open_file = process_get_file(fd);
	if (open_file == NULL)
		return;
	process_close_file(fd);
}

int filesize (int fd) {
	struct file *open_file = process_get_file(fd);
	if (open_file) 
		return file_length(open_file);
	return -1;
}
int
read (int fd, void *buffer, unsigned size) {		

	is_valid_addr(buffer);
	int bytes_read = 0;
	lock_acquire(&filesys_lock);
	// 표준 입력 처리: fd가 0인 경우 키보드에서 입력을 읽습니다.
    if (fd == 0) {
        unsigned i;
        for (i = 0; i < size; i++) {
			int c = input_getc();
			if (c == EOF) break;
            ((char*)buffer)[i] = (char)c;
			bytes_read++;
        }
	}
	else {
		struct file *file = process_get_file(fd);
    	bytes_read = file_read(file, buffer, size);
	}
	lock_release(&filesys_lock);
    return bytes_read;
}

int write (int fd, const void *buffer, unsigned size) {

	is_valid_addr(buffer);
	int writes = 0;
	lock_acquire(&filesys_lock);
	if (fd == 1) {
		putbuf(buffer,size);// void putbuf (const char *, size_t);
		lock_release(&filesys_lock);
        return size;
	}

	struct file *file  = process_get_file(fd);
	if (file == NULL) {
		lock_release(&filesys_lock);
		return -1;
	}
	writes = file_write(file, buffer, size);
	lock_release(&filesys_lock);
	return writes;
}

int fork(const char *thread_name, struct intr_frame *f) {
	is_valid_addr(thread_name);
	return process_fork(thread_name, f);
}

struct file* get_file(int fd){
	if (fd < 2 || fd > 128)
		return NULL; 
	return thread_current()->fd_table[fd];
}

/*
	addr - 매핑할 VM 시작주소
	length - 매핑할 파일의 길이
	writable - 매핑된 메모리 영역에 대한 쓰기 권한 여부 (0 : 읽기 전용, 1: 읽기/쓰기 가능)
	fd - 매핑할 파일의 파일 디스크립터 
	offset - 파일에서 매핑을 시작할 오프셋(바이트 단위)
	mmap은 annoymous와 동일하게 가숭 주소를 부여해주는 역할 수행 
	하지만 가상 메모리를 파일과 대응시켜야 함 
	따라서 파일 정보들을 넘겨줘야 함(file, offset 등 우리가 file_page에 담은 정보)
	파일 전체가 addr에서 시작하여 연속적인 가상 페이지에 매핑 됨 
	******** 즉 mmap은 단지 가상 주소 공간을 부여하는 것 !!!!
*/
void * mmap(void *addr, size_t length, int writable, int fd, off_t offset){

	struct file *file;
	
	// addr이 NULL이거나 페이지 경계에 정렬되어 있지 않은 경우  
	if(addr == NULL || !pg_round_down(addr)) return NULL;

	// addr이 커널 영역이거나, addr + length가 커널 영역인 경우, 즉 유저 영역이 아닌 경우 
	if(is_kernel_vaddr(addr) || is_kernel_vaddr(addr+length)) return NULL;

	// offset과 페이지 경계를 맞춰주기 위함
	// 메모리 매핑이 올바르게 이루어지지 않거나 예상치 못한 동작이 발생할 수 있음 
	if(offset != pg_round_down(offset)) return NULL;

	// 이미 매핑된 경우 체크 
	if(spt_find_page(&thread_current()->spt, addr)) return NULL;
	if(fd < 2) return NULL;

	file = get_file(fd);
	if(file == NULL) return NULL;
	
	// 파일 길이가 0인 경우 
	if(file_length(file) == 0 || (int)length <= 0) return NULL;

	return do_mmap(addr, length, writable, file, offset);

}

void munmap(void *addr){
	do_munmap(addr);
}