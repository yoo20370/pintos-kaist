#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"
#include "filesys/file.h"
#include "filesys/filesys.h"

void syscall_entry (void);
void syscall_handler (struct intr_frame *);
void halt (void) NO_RETURN;
void exit (int status) NO_RETURN;
//pid_t fork (const char *thread_name);
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
bool check_addr(void * addr);

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
}

void 
halt(void){
	// 단순히 종료만 시켜주면 됨 
	power_off();

}

void 
exit(int status){
	struct thread *curr_t = thread_current();
	curr_t->exit_status = status;
	printf("%s: exit(%d)\n", curr_t->name , status);
	thread_exit();

}

int exec (const char *file){
	return 0;
}

int wait (pid_t){
	return 0;
}

bool 
create (const char *file, unsigned initial_size){
	if(file == NULL || initial_size < 0) return -1;	
	return filesys_create(file, initial_size);
}


bool 
remove(const char *file){
	if(file == NULL) return false;
	// TODO: 열려있다면 반환 필요 
	return filesys_remove(file);
}

int find_table_empty(int fd, struct thread* curr_t){
	for(; fd < 64; fd++)
		if(curr_t->fdt[fd] == NULL || curr_t->fdt[fd] == 0) return fd;
	return -1;
}

// 파일 이름에 해당하는 파일 
int 
open(const char *file){	
	struct thread *curr_t = thread_current();
	struct file* open_file = filesys_open(file);
	int temp_fd = 0;
	int fd = 0;
	if(open_file == NULL || open_file == 0) return -1;

	// 파일을 연 스레드에 페이지 테이블 할당 필요 
	curr_t->fdt[curr_t->next_fd] = open_file;
	fd = curr_t->next_fd;

	// next_fd 다음 위치부터 탐색 시작
	temp_fd = find_table_empty(fd + 1, curr_t);
	if(temp_fd == -1) return -1; // 반환값 -1
	curr_t->next_fd = temp_fd;
	
	return fd;

}

int
filesize(int fd){
	struct thread* curr_t;

	if(fd < 2) exit(-1);
	curr_t = thread_current()->fdt[fd];

	if(curr_t == NULL) return 0;
	return file_length(curr_t);	

}

int read(int fd, void* buffer, unsigned length){
	if(fd < 0) return -1;
	struct file* curr_f = thread_current()->fdt[fd];
	if(curr_f == NULL || curr_f == 0 || length < 0) return -1;
	
	if(fd == 0){ 
		char * buf = (char*)buffer;
		for(unsigned i = 0 ; i < length; i++){
			buf[i] = input_getc();
		}
		return length;
	} else {

		if(length == 0){
			return 0;
		}		
		return file_read(curr_f, buffer, file_length(curr_f));
	}
}

int 
write (int fd, const void *buffer, unsigned length){

	struct thread* curr_t = thread_current();
	struct file * curr_f;

	if(fd < 0) return 0;

	if(fd == 1){
		putbuf(buffer, length);
		return length;

	} else {
		curr_f = curr_t->fdt[fd];
		if(curr_f == NULL || curr_f == 0 || length < 0) return 0;
		if(length == 0) return 0;

		return file_write(curr_f, buffer, length);

	}
}

unsigned tell (int fd){
	return 0;
}

void 
close(int fd){
	struct thread * curr_t = thread_current();
	struct file * curr_file = curr_t->fdt[fd];

	if(curr_file == NULL || curr_file == 0) return;
	// 파일 닫음 
	file_close(curr_file);

	curr_t->fdt[fd] = NULL;

	if(fd < curr_t->next_fd)
		curr_t->next_fd = fd;
}

bool 
check_addr(void * addr){

	// 주소 유효성 검사 - 리터럴은 값을 체크해줄 필요가 없음 
	// 접근하는 주소가 유저 영역 내에 있는지, 접근하는 주소가 커널 영역에 있는지, 접근하는 주소가 페이지 테이블에 매핑되어 있는지 체크해야 함 
	struct thread* curr =  thread_current();
	if (pml4_get_page(curr->pml4, addr) == NULL || !is_user_vaddr(addr) || addr == NULL)
	{
		exit(-1);
	}

}

/* The main system call interface */
void
syscall_handler (struct intr_frame *f UNUSED) {
	// TODO: Your implementation goes here.
	// 시스템 콜
	int number = f->R.rax;

	//printf("%d\n", number);
	switch(number) {
		case SYS_HALT :
			halt();
			break;

		case SYS_EXIT :
			exit(f->R.rdi);
			break;

		case SYS_FORK :
			break;

		case SYS_EXEC :
			break;

		case SYS_WAIT :
			break;

		case SYS_CREATE :
			check_addr(f->R.rdi);
			f->R.rax = create(f->R.rdi, f->R.rsi);
			break;
		case SYS_REMOVE :
			check_addr(f->R.rdi);
			f->R.rax = remove(f->R.rdi);
			break;

		case SYS_OPEN :
			check_addr(f->R.rdi);
			f->R.rax = open(f->R.rdi);
			break;

		case SYS_FILESIZE :
			f->R.rax = filesize(f->R.rdi);
			break;

		case SYS_READ :
			check_addr(f->R.rsi);
			f->R.rax = read(f->R.rdi, f->R.rsi, f->R.rdx);
			break;

		case SYS_WRITE :
			check_addr(f->R.rsi);
			f->R.rax = write(f->R.rdi, f->R.rsi, f->R.rdx);
			break;

		case SYS_SEEK :
			break;

		case SYS_TELL :
			break;

		case SYS_CLOSE :
			close(f->R.rdi);
			break;

	}

}