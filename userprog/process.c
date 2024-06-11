#include "userprog/process.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "userprog/gdt.h"
#include "userprog/tss.h"
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/flags.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "threads/mmu.h"
#include "threads/vaddr.h"
#include "intrinsic.h"
#ifdef VM
#include "vm/vm.h"
#endif

/* (There is an unrelated limit of 128 bytes on command-line arguments that 
the pintos utility can pass to the kernel.)*/
#define MAX_ARGS 128

static void process_cleanup (void);
static bool load (const char *file_name, struct intr_frame *if_);
static void initd (void *f_name);
static void __do_fork (void *);
void argument_stack(char *argv[], int argc, struct intr_frame *if_);

void process_exit_file(void);
int process_add_file(struct file *f);
struct file *process_get_file(int fd);
void process_close_file(int fd);
void process_exit(void);

struct thread* get_child_process (int pid);
void remove_child_process(struct thread *cp);
/* General process initializer for initd and other process. */
static void
process_init (void) {
	struct thread *current = thread_current ();
}

/* Starts the first userland program, called "initd", loaded from FILE_NAME.
 * The new thread may be scheduled (and may even exit)
 * before process_create_initd() returns. Returns the initd's
 * thread id, or TID_ERROR if the thread cannot be created.
 * Notice that THIS SHOULD BE CALLED ONCE. */
tid_t
process_create_initd (const char *file_name) {
	/* command line에서 받은 args를 사용하여 파일 처리를 시작한다.*/

	/* 
	pintos -v -k -T 60 -m 20   --fs-disk=10 -p tests/userprog/args-single:args-single -- -q   -f run 'args-single onearg'
	run 뒷 부분부터 parsing이 되는 것입니다.
	argv[0] = run
	argv[1] = "args-single onearg"
	arg[1]을 file_name으로 받은 상태입니다.
	*/

	char *fn_copy;
	tid_t tid;

	/* Make a copy of FILE_NAME.
	 * Otherwise there's a race between the caller and load(). */
	fn_copy = palloc_get_page (0); // 페이지 할당 
	if (fn_copy == NULL) {
		return TID_ERROR;
	}
	strlcpy (fn_copy, file_name, PGSIZE); // page에 fn (file name) 저장.

	char *save_ptr, *token;
	token = strtok_r(file_name, " ", &save_ptr);
	
	/* Create a new thread to execute FILE_NAME. */
	tid = thread_create (token, PRI_DEFAULT, initd, fn_copy);
	if (tid == TID_ERROR)
		palloc_free_page (fn_copy);
	return tid;
}

/* A thread function that launches first user process. */
static void
initd (void *f_name) {
#ifdef VM
	supplemental_page_table_init (&thread_current ()->spt);
#endif

	process_init ();

	if (process_exec (f_name) < 0)
		PANIC("Fail to launch initd\n");
	NOT_REACHED ();
}

/* Clones the current process as `name`. Returns the new process's thread id, or
 * TID_ERROR if the thread cannot be created. */
tid_t
process_fork (const char *name, struct intr_frame *if_ UNUSED) {
	/* Clone current thread to new thread.*/

	/* intr frame 복사 */
	struct thread *cur = thread_current();
	memcpy(&cur->copied_if, if_, sizeof(struct intr_frame));
	
	tid_t tid = thread_create (name,
			PRI_DEFAULT, __do_fork, cur);
	if (tid == TID_ERROR) // Error handling
		return TID_ERROR;
 
	struct thread *child = get_child_process(tid);
	if (child == NULL) 
		return TID_ERROR;
	sema_down(&child->fork_sema);

	if (child->exit_code == -1) {
		
		return TID_ERROR;
	}
	return tid;
}

#ifndef VM
/* Duplicate the parent's address space by passing this function to the
 * pml4_for_each. This is only for the project 2. */
static bool
duplicate_pte (uint64_t *pte, void *va, void *aux) {
	struct thread *current = thread_current ();
	struct thread *parent = (struct thread *) aux;
	void *parent_page;
	void *newpage;
	bool writable;

	/* 1. TODO: If the parent_page is kernel page, then return immediately. */
	if (is_kernel_vaddr(va)) {
		return true;
	}

	/* 2. Resolve VA from the parent's page map level 4. */
	parent_page = pml4_get_page (parent->pml4, va);
	if (parent_page == NULL) { 
		return false;
	}
	/* 3. TODO: Allocate new PAL_USER page for the child and set result to
	 *    TODO: NEWPAGE. */
	newpage = palloc_get_page(PAL_USER); // create for User, and set it to 0
	if (newpage == NULL) {
		return false;
	}
	
	/* 4. TODO: Duplicate parent's page to the new page and
	 *    TODO: check whether parent's page is writable or not (set WRITABLE
	 *    TODO: according to the result). */
	memcpy(newpage, parent_page, PGSIZE);
	writable = is_writable(pte);

	/* 5. Add new page to child's page table at address VA with WRITABLE
	 *    permission. */
	if (!pml4_set_page (current->pml4, va, newpage, writable)) {
		/* 6. TODO: if fail to insert page, do error handling. */
		palloc_free_page(newpage);
		return false;
	}
	return true;
}
#endif

/* A thread function that copies parent's execution context.
 * Hint) parent->tf does not hold the userland context of the process.
 *       That is, you are required to pass second argument of process_fork to
 *       this function. */
static void
__do_fork (void *aux) {
	struct intr_frame if_; // local if_ for fork thread.
	struct thread *parent = (struct thread *) aux;
	struct thread *current = thread_current ();
	/* TODO: somehow pass the parent_if. (i.e. process_fork()'s if_) */
	struct intr_frame *parent_if;

	parent_if = &parent->copied_if; // parent 자체가 fork를 실행한 쓰레드, 즉 복사본을 위에서 fork 하기 전에 저장함.
	bool succ = true;
	/* 1. Read the cpu context to local stack. */
	memcpy (&if_, parent_if, sizeof (struct intr_frame));
	if_.R.rax = 0;

	/* 2. Duplicate PT */
	current->pml4 = pml4_create();
	if (current->pml4 == NULL) {
		goto error;
	}

	process_activate (current);
#ifdef VM
	supplemental_page_table_init (&current->spt);
	if (!supplemental_page_table_copy (&current->spt, &parent->spt))
		goto error;
#else

	if (!pml4_for_each (parent->pml4, duplicate_pte, parent)) {
		goto error;
	}
#endif

	/* TODO: Your code goes here.
	 * TODO: Hint) To duplicate the file object, use `file_duplicate`
	 * TODO:       in include/filesys/file.h. Note that parent should not return
	 * TODO:       from the fork() until this function successfully duplicates
	 * TODO:       the resources of parent.*/
	for (int i = 2; i < 128; i++) {
		struct file *file = parent->fd_table[i];
		if (file == NULL) 
			continue;
		file = file_duplicate(file);
		current->fd_table[i] = file;
	}
	current->cur_fd = parent->cur_fd;
	sema_up(&current->fork_sema);
	process_init ();

	/* Finally, switch to the newly created process. */
	if (succ)
		do_iret (&if_);
error:
	current->exit_code = TID_ERROR;
	sema_up(&current->fork_sema);
	exit(TID_ERROR); // thread_exit 대신 exit
}

/* Switch the current execution context to the f_name.
 * Returns -1 on fail. */
int
process_exec (void *f_name) {
	char *file_name = f_name;
	bool success;
	struct thread *cur = thread_current();
	/* We cannot use the intr_frame in the thread structure.
	 * This is because when current thread rescheduled,
	 * it stores the execution information to the member. */
	struct intr_frame _if;
	_if.ds = _if.es = _if.ss = SEL_UDSEG;
	_if.cs = SEL_UCSEG;
	_if.eflags = FLAG_IF | FLAG_MBS;

	/* We first kill the current context */
	process_cleanup ();

	char *token, *save_ptr;
    char *argv[128];
    int argc = 0;
	for (token = strtok_r(file_name, " ", &save_ptr); token != NULL; token = strtok_r(NULL, " ", &save_ptr)) {
		argv[argc++] = token;
	}
	/* And then load the binary */
	success = load (file_name, &_if);
	
	/* If load failed, quit. */
	if (!success)
		return -1;
	argument_stack(&argv, argc,&_if);
	//hex_dump(_if.rsp, _if.rsp, USER_STACK - _if.rsp, true);
	palloc_free_page (file_name);

	/* Start switched process. */
	do_iret (&_if);
	NOT_REACHED ();
}


/* Waits for thread TID to die and returns its exit status.  If
 * it was terminated by the kernel (i.e. killed due to an
 * exception), returns -1.  If TID is invalid or if it was not a
 * child of the calling process, or if process_wait() has already
 * been successfully called for the given TID, returns -1
 * immediately, without waiting.
 *
 * This function will be implemented in problem 2-2.  For now, it
 * does nothing. */
int
process_wait (tid_t child_tid UNUSED) {
	/* XXX: Hint) The pintos exit if process_wait (initd), we recommend you
	 * XXX:       to add infinite loop here before
	 * XXX:       implementing the process_wait. */
	struct thread* child = get_child_process(child_tid);
	
	/* 예외 처리 발생시-1 리턴*/
	if (child == NULL) {
		return -1;
	}
	/* 자식프로세스가 종료될 때까지 부모 프로세스 대기(세마포어 이용) */
	// for(int i=0; i< 1000000000; i++) {
	// }
	sema_down(&child->wait_sema);

	list_remove(&child->child_elem);

	sema_up(&child->exit_sema);

	int ret = child->exit_code;
	return ret;

}


/* Exit the process. This function is called by thread_exit (). */
void
process_exit (void) {
	struct thread *curr = thread_current ();
	/* TODO: Your code goes here.
	 * TODO: Implement process termination message (see
	 * TODO: project2/process_termination.html).
	 * TODO: We recommend you to implement process resource cleanup here. */
	process_exit_file();
	palloc_free_multiple(curr->fd_table,FDT_PAGES);
	process_cleanup ();
	sema_up(&curr->wait_sema);
	sema_down(&curr->exit_sema);
}

/* Free the current process's resources. */
static void
process_cleanup (void) {
	struct thread *curr = thread_current ();

#ifdef VM
	supplemental_page_table_kill (&curr->spt);
#endif

	uint64_t *pml4;
	/* Destroy the current process's page directory and switch back
	 * to the kernel-only page directory. */
	pml4 = curr->pml4;
	if (pml4 != NULL) {
		/* Correct ordering here is crucial.  We must set
		 * cur->pagedir to NULL before switching page directories,
		 * so that a timer interrupt can't switch back to the
		 * process page directory.  We must activate the base page
		 * directory before destroying the process's page
		 * directory, or our active page directory will be one
		 * that's been freed (and cleared). */
		curr->pml4 = NULL;
		pml4_activate (NULL);
		pml4_destroy (pml4);
	}
}

/* Sets up the CPU for running user code in the nest thread.
 * This function is called on every context switch. */
void
process_activate (struct thread *next) {
	/* Activate thread's page tables. */
	pml4_activate (next->pml4);

	/* Set thread's kernel stack for use in processing interrupts. */
	tss_update (next);
}

/* We load ELF binaries.  The following definitions are taken
 * from the ELF specification, [ELF1], more-or-less verbatim.  */

/* ELF types.  See [ELF1] 1-2. */
#define EI_NIDENT 16

#define PT_NULL    0            /* Ignore. */
#define PT_LOAD    1            /* Loadable segment. */
#define PT_DYNAMIC 2            /* Dynamic linking info. */
#define PT_INTERP  3            /* Name of dynamic loader. */
#define PT_NOTE    4            /* Auxiliary info. */
#define PT_SHLIB   5            /* Reserved. */
#define PT_PHDR    6            /* Program header table. */
#define PT_STACK   0x6474e551   /* Stack segment. */

#define PF_X 1          /* Executable. */
#define PF_W 2          /* Writable. */
#define PF_R 4          /* Readable. */

/* Executable header.  See [ELF1] 1-4 to 1-8.
 * This appears at the very beginning of an ELF binary. */
struct ELF64_hdr {
	unsigned char e_ident[EI_NIDENT];
	uint16_t e_type;
	uint16_t e_machine;
	uint32_t e_version;
	uint64_t e_entry;
	uint64_t e_phoff;
	uint64_t e_shoff;
	uint32_t e_flags;
	uint16_t e_ehsize;
	uint16_t e_phentsize;
	uint16_t e_phnum;
	uint16_t e_shentsize;
	uint16_t e_shnum;
	uint16_t e_shstrndx;
};

struct ELF64_PHDR {
	uint32_t p_type;
	uint32_t p_flags;
	uint64_t p_offset;
	uint64_t p_vaddr;
	uint64_t p_paddr;
	uint64_t p_filesz;
	uint64_t p_memsz;
	uint64_t p_align;
};

/* Abbreviations */
#define ELF ELF64_hdr
#define Phdr ELF64_PHDR

static bool setup_stack (struct intr_frame *if_);
static bool validate_segment (const struct Phdr *, struct file *);
static bool load_segment (struct file *file, off_t ofs, uint8_t *upage,
		uint32_t read_bytes, uint32_t zero_bytes,
		bool writable);

/* Loads an ELF executable from FILE_NAME into the current thread.
 * Stores the executable's entry point into *RIP
 * and its initial stack pointer into *RSP.
 * Returns true if successful, false otherwise. */
static bool
load (const char *file_name, struct intr_frame *if_) {
	struct thread *t = thread_current ();
	struct ELF ehdr;
	struct file *file = NULL;
	off_t file_ofs;
	bool success = false;
	int i;

	/* Allocate and activate page directory. */
	t->pml4 = pml4_create ();
	if (t->pml4 == NULL)
		goto done;
	process_activate (thread_current ());

	/* Open executable file. */
	file = filesys_open (file_name);
	if (file == NULL) {
		printf ("load: %s: open failed\n", file_name);
		goto done;
	}

	/* Read and verify executable header. */
	if (file_read (file, &ehdr, sizeof ehdr) != sizeof ehdr
			|| memcmp (ehdr.e_ident, "\177ELF\2\1\1", 7)
			|| ehdr.e_type != 2
			|| ehdr.e_machine != 0x3E // amd64
			|| ehdr.e_version != 1
			|| ehdr.e_phentsize != sizeof (struct Phdr)
			|| ehdr.e_phnum > 1024) {
		printf ("load: %s: error loading executable\n", file_name);
		goto done;
	}

	/* Read program headers. */
	file_ofs = ehdr.e_phoff;
	for (i = 0; i < ehdr.e_phnum; i++) {
		struct Phdr phdr;

		if (file_ofs < 0 || file_ofs > file_length (file))
			goto done;
		file_seek (file, file_ofs);

		if (file_read (file, &phdr, sizeof phdr) != sizeof phdr)
			goto done;
		file_ofs += sizeof phdr;
		switch (phdr.p_type) {
			case PT_NULL:
			case PT_NOTE:
			case PT_PHDR:
			case PT_STACK:
			default:
				/* Ignore this segment. */
				break;
			case PT_DYNAMIC:
			case PT_INTERP:
			case PT_SHLIB:
				goto done;
			case PT_LOAD:
				if (validate_segment (&phdr, file)) {
					bool writable = (phdr.p_flags & PF_W) != 0;
					uint64_t file_page = phdr.p_offset & ~PGMASK;
					uint64_t mem_page = phdr.p_vaddr & ~PGMASK;
					uint64_t page_offset = phdr.p_vaddr & PGMASK;
					uint32_t read_bytes, zero_bytes;
					if (phdr.p_filesz > 0) {
						/* Normal segment.
						 * Read initial part from disk and zero the rest. */
						read_bytes = page_offset + phdr.p_filesz;
						zero_bytes = (ROUND_UP (page_offset + phdr.p_memsz, PGSIZE)
								- read_bytes);
					} else {
						/* Entirely zero.
						 * Don't read anything from disk. */
						read_bytes = 0;
						zero_bytes = ROUND_UP (page_offset + phdr.p_memsz, PGSIZE);
					}
					if (!load_segment (file, file_page, (void *) mem_page,
								read_bytes, zero_bytes, writable))
						goto done;
				}
				else
					goto done;
				break;
		}
	}

	/* Set up stack. */
	if (!setup_stack (if_))
		goto done;

	/* Start address. */
	if_->rip = ehdr.e_entry;

	/* TODO: Your code goes here.
	 * TODO: Implement argument passing (see project2/argument_passing.html). */

	success = true;
done:
	/* We arrive here whether the load is successful or not. */
	// file_close (file);
	return success;
}

/* Checks whether PHDR describes a valid, loadable segment in
 * FILE and returns true if so, false otherwise. */
static bool
validate_segment (const struct Phdr *phdr, struct file *file) {
	/* p_offset and p_vaddr must have the same page offset. */
	if ((phdr->p_offset & PGMASK) != (phdr->p_vaddr & PGMASK))
		return false;

	/* p_offset must point within FILE. */
	if (phdr->p_offset > (uint64_t) file_length (file))
		return false;

	/* p_memsz must be at least as big as p_filesz. */
	if (phdr->p_memsz < phdr->p_filesz)
		return false;

	/* The segment must not be empty. */
	if (phdr->p_memsz == 0)
		return false;

	/* The virtual memory region must both start and end within the
	   user address space range. */
	if (!is_user_vaddr ((void *) phdr->p_vaddr))
		return false;
	if (!is_user_vaddr ((void *) (phdr->p_vaddr + phdr->p_memsz)))
		return false;

	/* The region cannot "wrap around" across the kernel virtual
	   address space. */
	if (phdr->p_vaddr + phdr->p_memsz < phdr->p_vaddr)
		return false;

	/* Disallow mapping page 0.
	   Not only is it a bad idea to map page 0, but if we allowed
	   it then user code that passed a null pointer to system calls
	   could quite likely panic the kernel by way of null pointer
	   assertions in memcpy(), etc. */
	if (phdr->p_vaddr < PGSIZE)
		return false;

	/* It's okay. */
	return true;
}

#ifndef VM
/* Codes of this block will be ONLY USED DURING project 2.
 * If you want to implement the function for whole project 2, implement it
 * outside of #ifndef macro. */

/* load() helpers. */
static bool install_page (void *upage, void *kpage, bool writable);

/* Loads a segment starting at offset OFS in FILE at address
 * UPAGE.  In total, READ_BYTES + ZERO_BYTES bytes of virtual
 * memory are initialized, as follows:
 *
 * - READ_BYTES bytes at UPAGE must be read from FILE
 * starting at offset OFS.
 *
 * - ZERO_BYTES bytes at UPAGE + READ_BYTES must be zeroed.
 *
 * The pages initialized by this function must be writable by the
 * user process if WRITABLE is true, read-only otherwise.
 *
 * Return true if successful, false if a memory allocation error
 * or disk read error occurs. */
static bool
load_segment (struct file *file, off_t ofs, uint8_t *upage,
		uint32_t read_bytes, uint32_t zero_bytes, bool writable) {
	ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
	ASSERT (pg_ofs (upage) == 0);
	ASSERT (ofs % PGSIZE == 0);

	file_seek (file, ofs);
	while (read_bytes > 0 || zero_bytes > 0) {
		/* Do calculate how to fill this page.
		 * We will read PAGE_READ_BYTES bytes from FILE
		 * and zero the final PAGE_ZERO_BYTES bytes. */
		size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
		size_t page_zero_bytes = PGSIZE - page_read_bytes;

		/* Get a page of memory. */
		uint8_t *kpage = palloc_get_page (PAL_USER);
		if (kpage == NULL)
			return false;

		/* Load this page. */
		if (file_read (file, kpage, page_read_bytes) != (int) page_read_bytes) {
			palloc_free_page (kpage);
			return false;
		}
		memset (kpage + page_read_bytes, 0, page_zero_bytes);

		/* Add the page to the process's address space. */
		if (!install_page (upage, kpage, writable)) {
			printf("fail\n");
			palloc_free_page (kpage);
			return false;
		}

		/* Advance. */
		read_bytes -= page_read_bytes;
		zero_bytes -= page_zero_bytes;
		upage += PGSIZE;
	}
	return true;
}

/* Create a minimal stack by mapping a zeroed page at the USER_STACK */
static bool
setup_stack (struct intr_frame *if_) {
	uint8_t *kpage;
	bool success = false;

	kpage = palloc_get_page (PAL_USER | PAL_ZERO);
	if (kpage != NULL) {
		success = install_page (((uint8_t *) USER_STACK) - PGSIZE, kpage, true);
		if (success)
			if_->rsp = USER_STACK;
		else
			palloc_free_page (kpage);
	}
	return success;
}

/* Adds a mapping from user virtual address UPAGE to kernel
 * virtual address KPAGE to the page table.
 * If WRITABLE is true, the user process may modify the page;
 * otherwise, it is read-only.
 * UPAGE must not already be mapped.
 * KPAGE should probably be a page obtained from the user pool
 * with palloc_get_page().
 * Returns true on success, false if UPAGE is already mapped or
 * if memory allocation fails. */
static bool
install_page (void *upage, void *kpage, bool writable) {
	struct thread *t = thread_current ();

	/* Verify that there's not already a page at that virtual
	 * address, then map our page there. */
	return (pml4_get_page (t->pml4, upage) == NULL
			&& pml4_set_page (t->pml4, upage, kpage, writable));
}
#else
/* From here, codes will be used after project 3.
 * If you want to implement the function for only project 2, implement it on the
 * upper block. */

// 실행 가능한 파일의 페이지들을 초기화하는 함수(?) 
bool
lazy_load_segment (struct page *page, void *aux) {
	/* TODO: Load the segment from the file */
	/* TODO: This called when the first page fault occurs on address VA. */
	/* TODO: VA is available when calling this function. */

	// aux에는 필요한 정보를 담아서 전달하고 있음, read_bytes, zero_bytes, offset, file 
	struct container *container = (struct container *)aux;
	
    struct file *file = container->file;
    off_t offsetof = container->offset;
    size_t read_bytes = container->read_bytes;
    size_t zero_bytes = PGSIZE - read_bytes;
    
    // Page load
    file_seek(file, offsetof); // file의 위치를 offsetof으로 변경
	// file에서 read_bytes만큼 읽어서 프레임(물리 메모리)에 저장
    if (file_read(file, page->frame->kva, read_bytes) != (int)read_bytes) {
        palloc_free_page(page->frame->kva);
        return false;
    }
	// 나머지 부분 0으로 채움 
    memset(page->frame->kva + read_bytes, 0, zero_bytes);

    return true;
}

/* Loads a segment starting at offset OFS in FILE at address
 * UPAGE.  In total, READ_BYTES + ZERO_BYTES bytes of virtual
 * memory are initialized, as follows:
 *
 * - READ_BYTES bytes at UPAGE must be read from FILE
 * starting at offset OFS.
 *
 * - ZERO_BYTES bytes at UPAGE + READ_BYTES must be zeroed.
 *
 * The pages initialized by this function must be writable by the
 * user process if WRITABLE is true, read-only otherwise.
 *
 * Return true if successful, false if a memory allocation error
 * or disk read error occurs. */

// load_segment는 딱 한 번만 호출 됨 -> 처음에 가상 주소만 할당할 때만 
// 파일의 크기가 페이지 크기보다 클 수 있으므로 해당 파일을 페이지 단위로 자르는 과정, 이 때 lazy_loading이므로 가상 주소만 담겨있는 페이지 구조체만 생성됨 
static bool
load_segment (struct file *file, off_t ofs, uint8_t *upage,
		uint32_t read_bytes, uint32_t zero_bytes, bool writable) {
	ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
	ASSERT (pg_ofs (upage) == 0);
	ASSERT (ofs % PGSIZE == 0);

	while (read_bytes > 0 || zero_bytes > 0) {
		/* Do calculate how to fill this page.
		 * We will read PAGE_READ_BYTES bytes from FILE
		 * and zero the final PAGE_ZERO_BYTES bytes. */	
		size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
		size_t page_zero_bytes = PGSIZE - page_read_bytes;
		
		/* TODO: Set up aux to pass information to the lazy_load_segment. */
		struct container *container = (struct container *)malloc(sizeof(struct container));
		// container에 담긴 데이터들은 나중에 lazy_load_segment 함수를 호출해서 실제 데이터를 가져올 때 사용 
		container->file = file;
		container->offset = ofs;
		container->read_bytes = page_read_bytes;
		container->zero_bytes = page_zero_bytes;

		// VM_ANON 타입인 이유 -> 게임 내에서의 데이터 값은 변경될 수 있지만 게임 자체의 데이터는 변경되면 안 되므로 VM_ANON 타입으로 선언해야 함
		// VM_ANON은 프레임에 데이터를 올리고 스왑 영역으로 데이터를 내릴 수 있지만 실제 프로그램에 대한 데이터를 변경할 수 없음 
		if (!vm_alloc_page_with_initializer (VM_ANON, upage,
					writable, lazy_load_segment, container))
			return false;

		/* Advance.*/
		// 실제 데이터를 읽어서 올리는 것이 아니라, lazy_load에서 필요한 위치와 크기를 계산하는데 사용
		// read_bytes - 읽어야 하는 남은 바이트 수, page_read_bytes 만큼 읽었으므로 빼준다.
		read_bytes -= page_read_bytes;
		// zero_byte - 0으로 채워야 할 바이트 수, page_zero_bytes 만큼 0을 채웠으니 빼준다.
		zero_bytes -= page_zero_bytes;
		upage += PGSIZE;
		// ofs - 파일에서 읽기 시작할 위치를 나타냄, page_read_bytes 만큼 읽었으므로 해당 크기 만큼 더한 위치를 다음 읽어야 하는 위치로 지정
		ofs += page_read_bytes;
	}
	return true;
}

/* Create a PAGE of stack at the USER_STACK. Return true on success. */
/*
	첫 스택 페이지는 지연적으로 할당될 필요가 없음 
	-> 페이지 폴트 발생을 기다릴 필요가 없음, 
*/
static bool
setup_stack (struct intr_frame *if_) {
	bool success = false;
    void *stack_bottom = (void *) (((uint8_t *) USER_STACK) - PGSIZE);

    struct thread *thread = thread_current();

    // vm_alloc_page: type, upage, writable
	// vm_alloc_page() == vm_alloc_with_initializer() 실행, 스택과 힙은 Anonymous Page이기 때문 
    if (vm_alloc_page(VM_ANON | VM_MARKER_0, stack_bottom, 1)) {
		// 프레임을 페이지에 할당 
        success = vm_claim_page(stack_bottom);

        if (success) {
            if_->rsp = USER_STACK;
        }
    }
    return success;
}
#endif /* VM */

void
argument_stack(char *argv[], int argc, struct intr_frame *if_) {
	/* Tokenize the args! */
	uintptr_t argv_addr[argc];

	// // USER_STACK;
	// USER_STACK = 0x47480000 따라서 여기서부터 빼주면 되겠네
	// Push the address of each string plus a null pointer sentinel, on the stack, in right-to-left order. 
	// 위의 제약조건 때문에 i를 0부터 하지 않고, 끝에서부터 (argc-1) 부터 시작.
	for(int i = argc-1; i >= 0; i--) {
     	size_t len = strlen(argv[i]) + 1;  // Null 종단자 포함해야함. 따라서 +1
		if_->rsp -= len;
		memcpy(if_->rsp, argv[i], len);
		argv_addr[i] = if_->rsp;
	}
	/* uint8_t 타입은 워드 정렬과 관련된 용도로 일반적으로 사용되는 타입입니다. 
	따라서 코드 가독성을 위해 uint8_t 타입을 사용하는 것이 좋습니다. */

	/* Word-aligned accesses are faster than unaligned accesses, 
	so for best performance round the stack pointer down to a multiple of 8 before the first push. */

	/* | Name       | Data |  Type 	    | */
	/* | word-align |   0  |  uint8_t[] | */ 
	while ( (if_->rsp % 8) != 0  ) { // 8배수 패딩
		if_->rsp--;
		memset(if_->rsp, 0, sizeof(uint8_t)); // 그렇다면, 패딩 부분도 모두 0으로 만들어야 푸쉬가 제대로 되는건가..?
	}

	for (int i=argc; i>=0; i--) {
		if_->rsp -= sizeof(uintptr_t);
		if (i == argc) {
			memset(if_->rsp,0, sizeof( uintptr_t) );
		}
		else {
			memcpy(if_->rsp, &argv_addr[i], sizeof( uintptr_t));
		}
	}
	/* 4 번 단계 */
	if_->R.rsi = if_->rsp;
	if_->R.rdi = argc;

	/* 5번 단계 */
	if_->rsp -= sizeof( uintptr_t);
	memset(if_->rsp, 0, sizeof( uintptr_t));
}

void 
process_exit_file(void) {
	struct thread *cur = thread_current();
    for (int i = 2; i <= 128; i++) {
		if (cur->fd_table[i] != NULL) {
			process_close_file(i);
		}
    }
}

int 
process_add_file(struct file *f) {

	struct thread *cur = thread_current();
 	int tmp_fd;

    for (tmp_fd = 2; tmp_fd < 128; tmp_fd++) {
		if (cur->fd_table[tmp_fd] == NULL) {
			cur->fd_table[tmp_fd] = f;
			cur->cur_fd = tmp_fd;
			return cur->cur_fd;
		}
    }
	return -1;
}

struct file 
*process_get_file(int fd) {
	if (fd < 2 || fd > 127)
		return NULL;
	return thread_current()->fd_table[fd];
}

void 
process_close_file(int fd) {
	if (fd < 2 || fd > 127 || fd == NULL)
		return NULL;

	struct thread *cur = thread_current();
	struct file *open_file = process_get_file(fd);
	if (open_file == NULL) return NULL;

	cur->fd_table[fd] = NULL;
	file_close(open_file);
}

struct thread*
get_child_process (int pid) {
	/* 자식 리스트에 접근하여 프로세스 디스크립터 검색*/
	struct thread* cur = thread_current();

	/* 해당 pid가 존재하면 프로세스 디스크립터 반환 */
	struct list_elem *e;

	for (e = list_begin(&cur->child_list); e!= list_end(&cur->child_list); e = list_next(e)) {
		struct thread* child = list_entry(e,struct thread, child_elem);
		if (child->tid == pid) {
			return child;
		}
	}
	/* 리스트에 존재하지 않으면 NULL 리턴*/
	return NULL;
}

void remove_child_process(struct thread *cp) {
	struct thread* cur = thread_current();
	/* 해당 pid가 존재하면 프로세스 디스크립터 반환 */
	struct list_elem *e;
	for (e = list_begin(&cur->child_list); e!= list_end(&cur->child_list); e = list_next(e)) {
		struct thread* child = list_entry(e,struct thread, child_elem);
		if (child == cp) {
			list_remove(child); 	/* 자식 리스트에서 제거*/
			free(child); 			/* 프로세스 디스크립터 메모리 해제*/
			break;
		}
	}
}
