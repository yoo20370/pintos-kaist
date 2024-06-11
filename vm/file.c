/* file.c: Implementation of memory backed file object (mmaped object). */

#include "vm/vm.h"
#include "threads/vaddr.h"
#include "userprog/process.h"

static bool file_backed_swap_in (struct page *page, void *kva);
static bool file_backed_swap_out (struct page *page);
static void file_backed_destroy (struct page *page);

/* DO NOT MODIFY this struct */
static const struct page_operations file_ops = {
	.swap_in = file_backed_swap_in,
	.swap_out = file_backed_swap_out,
	.destroy = file_backed_destroy,
	.type = VM_FILE,
};

/* The initializer of file vm */
void
vm_file_init (void) {
}

/* Initialize the file backed page */
/*
	수정사항을 파일에 다시 기록하기 위해서는 매핑 해제하는 시점에 해당 페이지에 매핑된 파일의 정보를 알 수 있어야 함 
	이를 위해 file_backed_page가 초기화될 때 file_backed_initializer에서 파일에 대한 정보를 page 구조체에 추가
*/
bool 
file_backed_initializer (struct page *page, enum vm_type type, void *kva) {
	/* Set up the handler */
	page->operations = &file_ops;

	struct file_page *file_page = &page->file;

    // page struct의 일부 정보(such as 메모리가 백업되는 파일과 관련된 정보)를 업데이트할 수도 있습니다.
    struct container *container = (struct container *)page->uninit.aux;
    file_page->file = container->file;
    file_page->offset = container->offset;
    file_page->read_bytes = container->read_bytes;
    return true;
}

/* Swap in the page by read contents from the file. */
static bool
file_backed_swap_in (struct page *page, void *kva) {
	struct file_page *file_page UNUSED = &page->file;
}

/* Swap out the page by writeback contents to the file. */
static bool
file_backed_swap_out (struct page *page) {
	struct file_page *file_page UNUSED = &page->file;
}

/* Destory the file backed page. PAGE will be freed by the caller. */
/*
	프로세스가 종료될 떄도 마찬가리로 매핑이 해제되어야 함
	수정사항을 파일에 반영하고 가상 페이지 목록에서 페이지를 제거 
*/
static void 
file_backed_destroy (struct page *page) {
    // page struct를 해제할 필요는 없습니다. (file_backed_destroy의 호출자가 해야 함)
	struct file_page *file_page UNUSED = &page->file;

    if (pml4_is_dirty(thread_current()->pml4, page->va)) {
        file_write_at(file_page->file, page->va, file_page->read_bytes, file_page->offset);
        pml4_set_dirty(thread_current()->pml4, page->va, 0);
    }
    pml4_clear_page(thread_current()->pml4, page->va);
}

/*
	fd로 열린 파일을 offset 바이트부터 프로세스의 가상 주소 공간의 addr에 length 만큼 매핑해야 함
	-> load_segment와 로직이 비슷 
*/
void *
do_mmap (void *addr, size_t length, int writable,
		struct file *file, off_t offset) {
	/* 
		mmap에서 열었던 file을 reopen 해야 하는 이유 2가지 
		1. 원본 파일은 그대로 유지되어야 함 
		-> 유저가 r/w할 때 정상적으로 진행되기 위해 원본 구조체의 offset이 유지되어야 함
		2. 유저가 해당 fd로 close 하더라도 munmap하지 않았따면 매핑이 유지되어야하기 때문
		-> reopen 하지 않고 원본 사용 시 유저에 의해 file이 close 되었을 떄 더 이상 매핑을 유지할 수 없음 
	*/
	struct file *rf = file_reopen(file);

	// 함수가 성공하면 파일이 매핑된 가상 주소(시작주소)를 반환해야 함
	void *start_addr = addr;
	
	// 이 매핑을 위해 사용한 총 페이지 수 
	int total_page_cnt = length & PGSIZE ? length / PGSIZE + 1 : length / PGSIZE;

	size_t read_bytes = file_length(rf) < length ? file_length(rf) : length;
	size_t zero_bytes = PGSIZE - read_bytes % PGSIZE;

	ASSERT((read_bytes + zero_bytes) % PGSIZE == 0);
	ASSERT(pg_ofs(addr) == 0);
	ASSERT(offset % PGSIZE == 0);

	while(read_bytes > 0 || zero_bytes > 0){
		if(spt_find_page(&thread_current()->spt, addr) != NULL) return NULL;
		size_t page_read_bytes = length < PGSIZE ? length : PGSIZE;
		size_t page_zero_bytes = PGSIZE - page_read_bytes;

		struct container * container = (struct container* )malloc(sizeof(struct container));
		if(container == NULL) return NULL;

		container->file = rf;
		container->offset = offset;
		container->read_bytes = length;
		container->zero_bytes = page_zero_bytes;

		if(!vm_alloc_page_with_initializer(VM_FILE, addr, writable, lazy_load_segment, container)){
			free(container);
			return NULL;
		}
		
		struct page* page = spt_find_page(&thread_current()->spt, addr);
		page->mapped_page_count = total_page_cnt;

		// 읽어야하는 byte 갱신
		read_bytes -= page_read_bytes;
		zero_bytes -= page_zero_bytes;
		// 다음 페이지 주소 
		addr += PGSIZE;
		// 읽은 만큼 파일의 offset 이동시킴 
		offset += page_read_bytes;
	}
	return start_addr;
}

/* Do the munmap */
// 지정된 주소 범위 addr에 대한 매핑을 해제 
// 이 주소는 동일한 프로세스에서 이전에 mmap 호출에 의해 반환된 가상 주소여야 함 , 아직 해제되지 않은 상태여야 함 
/*
	프로세스가 종료될 때 모든 매핑은 암묵적으로 해제 됨 
	매핑이 암묵적으로 또는 명시적으로 해제될 때, 프로세스에 의해 작성된 모든 페이지는 파일로 다시 쓰여짐
	작성되지 않은 페이지는 그렇지 않아야 함 
	그런 다음 페이즈들은 프로세스의 가상 페이지 목록에서 제거된다.
	파일을 닫아도 제거해도, 그 파일의 매핑이 해제되지 않음 
	매핑은 생성된 후 munmap이 호출되거나 프로세스가 종료될 때까지 유효

	**** 각 매핑에 대해 파일의 별도이고 독립적인 참조를 얻기 위해서 file_reopen 함수를 사용 
	두 개 이상의 프로세스가 동일한 파일을 매핑하는 경우, 일관된 데이터를 보는 것이 요구되지 않는다.
	유닉스는 두 매핑이 동일한 물리 페이지를 공유하도록 하며 
	mmap 시스템 호출은 페이지가 공유되는지 아니면 개인적인지를 지정할 수 있는 인자도 가지고 있음 
*/
void
do_munmap (void *addr) {
	struct supplemental_page_table *spt = &thread_current()->spt;
	struct page *page = spt_find_page(spt, addr);
	int count = page->mapped_page_count;
	for(int i = 0; i < count; i++){
		if(page) destroy(page);
		addr += PGSIZE;
		page = spt_find_page(spt,addr);
	}
}
