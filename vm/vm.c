/* vm.c: Generic interface for virtual memory objects. */

#include "threads/malloc.h"
#include "vm/vm.h"
#include "vm/inspect.h"
#include "lib/kernel/hash.h"
#include "threads/mmu.h"
#include "string.h"

/* Initializes the virtual memory subsystem by invoking each subsystem's
 * intialize codes. */
void vm_init(void)
{
    vm_anon_init();
    vm_file_init();
#ifdef EFILESYS /* For project 4 */
    pagecache_init();
#endif
    register_inspect_intr();
    /* DO NOT MODIFY UPPER LINES. */
    /* TODO: Your code goes here. */
}

/* Get the type of the page. This function is useful if you want to know the
 * type of the page after it will be initialized.
 * This function is fully implemented now. */
enum vm_type
page_get_type(struct page *page)
{
    int ty = VM_TYPE(page->operations->type);
    switch (ty)
    {
    case VM_UNINIT:
        return VM_TYPE(page->uninit.type);
    default:
        return ty;
    }
}

/* Helpers */
static struct frame *vm_get_victim(void);
static bool vm_do_claim_page(struct page *page);
static struct frame *vm_evict_frame(void);

/* Create the pending page object with initializer. If you want to create a
 * page, do not create it directly and make it through this function or
 * `vm_alloc_page`. */
/*
    페이지 생명 주기
    초기화 -> ( page_fault -> lazy_load -> swap_in -> swap_out -> ... ) -> destory
*/
// 페이지 구조체를 생성하고, 이 구조체에 페이지 유형에 맞게 초기화 함수들을 담아줌, 즉 가상 메모리만 할당한다.
bool vm_alloc_page_with_initializer(enum vm_type type, void *upage, bool writable,
                                    vm_initializer *init, void *aux)
{
    // 타입에 따라 적절한 이니셜라이져를 가져와 uninit_new를 호출하는 함수
    ASSERT(VM_TYPE(type) != VM_UNINIT)

    struct supplemental_page_table *spt = &thread_current()->spt;

    /* Check wheter the upage is already occupied or not. */
    // NULL이면 spt에 없는 것이므로 초기화 구조체를 만들어서 spt에 삽입
    if (spt_find_page(spt, upage) == NULL)
    {
        /* Create the page, fetch the initialier according to the VM type,
         * and then create "uninit" page struct by calling uninit_new. You
         * should modify the field after calling the uninit_new. */
        // 페이지 구조체는 정보를 담는 그릇이므로 데이터를 담는 4kb 크기로 지정할 필요가 없음
        struct page *page = (struct page *)malloc(sizeof(struct page));

        // 할당받은 페이지에 페이지 타입에 맞게 필드 값을 설정해준다. (단 가상 주소만 할당함, 프레임은 할당하지 않음 - 지금 만들어지는 페이지는 uninitPage)
        switch (VM_TYPE(type))
        {
        case VM_ANON:
            uninit_new(page, pg_round_down(upage), init, type, aux, anon_initializer);
            break;
        case VM_FILE:
            uninit_new(page, pg_round_down(upage), init, type, aux, file_backed_initializer);
            break;
        }

        // uninit_new 함수 이후에 페이지 필드 값 설정
        // -> uninit_new 함수는 인자로 들어온 페이지에 대한 필드 값을 설정하기 때문에 이후에 값을 대입해야 함
        page->writable = writable;

        /* Insert the page into the spt. */
        // 초기화되지 않은 페이지를 생성 후, 이를 보조 페이지 테이블에 저장한다. (SPT는 페이지에 대한 정보를 저장하는 테이블이므로 저장해줘야 함 )
        return spt_insert_page(spt, page);
    }
err:
    return false;
}

/* Find VA from spt and return page. On error, return NULL. */
// spt에서 va에 해당하는 page를 찾아서 반환한다.
struct page *
spt_find_page(struct supplemental_page_table *spt UNUSED, void *va UNUSED)
{
    // spt_find_page: spt에서 va가 있는지를 찾는 함수, hash_find() 사용
    struct page *page = (struct page *)malloc(sizeof(struct page));
    // pg_round_down: 해당 va가 속해 있는 page의 시작 주소를 얻는 함수,
    // 실제 데이터가 들어올 때 페이지 중간 주소로 들어올 수 있기 때문에 해시 테이블의 시작주소를 만들어주기 위해 pg_round_down(va) 함수를 호출하여 페이지 시작 주소를 알아낸다.
    page->va = pg_round_down(va);

    // hash_find: Dummy page의 빈 hash_elem을 넣어주면, va에 맞는 hash_elem을 리턴해주는 함수 (hash_elem 갱신)
    struct hash_elem *e = hash_find(&spt->spt_hash, &page->hash_elem);
    free(page);

    return e == NULL ? NULL : hash_entry(e, struct page, hash_elem);
}

/* Insert PAGE into spt with validation. */
// spt에 page를 삽입
bool spt_insert_page(struct supplemental_page_table *spt UNUSED,
                     struct page *page UNUSED)
{
    return hash_insert(&spt->spt_hash, &page->hash_elem) == NULL ? true : false; // 존재하지 않을 경우에만 삽입
}

void spt_remove_page(struct supplemental_page_table *spt, struct page *page)
{
    vm_dealloc_page(page);
    return true;
}

/* Get the struct frame, that will be evicted. */
static struct frame *
vm_get_victim(void)
{
    struct frame *victim = NULL;
    /* TODO: The policy for eviction is up to you. */

    return victim;
}

/* Evict one page and return the corresponding frame.
 * Return NULL on error.*/
static struct frame *
vm_evict_frame(void)
{
    struct frame *victim UNUSED = vm_get_victim();
    /* TODO: swap out the victim and return the evicted frame. */

    return NULL;
}

/* palloc() and get frame. If there is no available page, evict the page
 * and return it. This always return valid address. That is, if the user pool
 * memory is full, this function evicts the frame to get the available memory
 * space.*/
// user pool에서 새로운 물리적 페이지를 가져와 새로운 프레임 구조체에 할당해서 반환
static struct frame *
vm_get_frame(void)
{
    struct frame *frame = (struct frame *)malloc(sizeof(struct frame));
    frame->kva = palloc_get_page(PAL_USER);

    if (frame->kva == NULL)
    {
        // 한 페이지를 추출하고 해당 프레임을 반환, 오류가 발생하면 NULL을 반환
        //frame = vm_evict_frame();
        frame->page = NULL;

        return frame;
    }
    frame->page = NULL;

    ASSERT(frame != NULL);
    ASSERT(frame->page == NULL);

    return frame;
}

/* Growing the stack. */
static void
vm_stack_growth(void *addr UNUSED)
{
    vm_alloc_page(VM_ANON | VM_MARKER_0, pg_round_down(addr), 1);
    vm_claim_page(pg_round_down(addr));
}

/* Handle the fault on write_protected page */
static bool
vm_handle_wp(struct page *page UNUSED)
{
}

/* Return true on success */
// 페이지 폴트 발생 시 제어권을 vm_try_handle_fault 함수로 넘긴다.
// 가짜 오류인 경우 일부 내용르 페이지에 로드하고 제어권을 사용자 프로그램으로 반환 // 가짜 오류 - 지연로드, 스왑 아웃 페이지, 쓰기 금지 페이지
// TODO: 해당 함수를 수정하여 스택 증가를 확인한다. 이후 stack growth를 호출하여 스택을 증가시킨다.
bool vm_try_handle_fault(struct intr_frame *f UNUSED, void *addr UNUSED,
                         bool user UNUSED, bool write UNUSED, bool not_present UNUSED)
{
    struct supplemental_page_table *spt UNUSED = &thread_current()->spt;
    struct page *page = NULL;
    bool success = false;

    if(is_kernel_vaddr(addr))
        return false;
    /* Validate the fault */
    // 가상 주소가 유효하지 않은 경우
    if (not_present) // 접근한 메모리의 physical page가 존재하지 않은 경우
	{
		/* TODO: Validate the fault */
		// todo: 페이지 폴트가 스택 확장에 대한 유효한 경우인지를 확인해야 합니다.
		void *rsp = f->rsp; // user access인 경우 rsp는 유저 stack을 가리킨다.
		if (!user)			// kernel access인 경우 thread에서 rsp를 가져와야 한다.
			rsp = thread_current()->rsp;

		// 스택 확장으로 처리할 수 있는 폴트인 경우, vm_stack_growth를 호출
		if(USER_STACK - (1 << 20) <= (rsp - 8) && (rsp - 8) <= addr && addr <= USER_STACK){
			vm_stack_growth(addr);
            return true;
        }

		page = spt_find_page(spt, addr);
		if (page == NULL)
			return false;
		if (write == 1 && page->writable == 0) // write 불가능한 페이지에 write 요청한 경우
			return false;
		return vm_do_claim_page(page);
	}
	return false;
}

// bool vm_try_handle_fault(struct intr_frame *f UNUSED, void *addr UNUSED,
//                          bool user UNUSED, bool write UNUSED, bool not_present UNUSED)
// {
//     struct supplemental_page_table *spt UNUSED = &thread_current()->spt;
//     struct page *page = NULL;
//     struct thread *current;
//     bool success = false;
//     void *rsp_page;

//     /* Validate the fault */
//     // 가상 주소가 유효하지 않은 경우
//     if (addr == NULL || is_kernel_vaddr(addr))
//         return false;

//     // 유저인지 커널인지 어떻게 판별 ??
//     // 커널 모드 -> thread_current() -> rsp
//     // 유저 모드 -> intr_frame->rsp

//     // 함수에 할당된 물리 프레임이 존재하지 않아서 발생한 예외인 경우 not_present 값이 true임
//     // false인 경우는 물리 프레임이 할당되어 있지만 page fault가 발생한 것 -> read_only 페이지에 write한 경우
//     if (not_present)
//     {   
//         current = thread_current();

//         if (user != true)
//         {
//             rsp_page = current->rsp;
//         }
//         else
//         {
//             rsp_page = f->rsp;
//         }
        
//         if ((rsp_page -8) <= USER_STACK && (rsp_page-8) >= (USER_STACK - (1 << 20)))
//         {
//             while (rsp_page > addr)
//             {
//                 vm_stack_growth(rsp_page);
//                 rsp_page -= PGSIZE;
//             }
//             return true;
//         }
//         page = spt_find_page(spt, addr);
        
//         // SPT에서 페이지를 찾지 못했다면 false 반환
//         if (page == NULL)
//             return false;
//         // 찾은 페이지가 쓰기 권한이 없는 경우 false
//         if (write == 1 && page->writable == 0)
//             return false;
//         success = vm_do_claim_page(page);
//         return success;
//     }
//     return success;
// }

/* Free the page.
 * DO NOT MODIFY THIS FUNCTION. */
void vm_dealloc_page(struct page *page)
{
    destroy(page);
    free(page);
}

/* Claim the page that allocate on VA. */
// spt에서 va에 해당하는 페이지를 가져와 frame과의 매핑을 요청
bool vm_claim_page(void *va UNUSED)
{
    // 프레임을 페이지에 할당하는 함수
    struct page *page = spt_find_page(&thread_current()->spt, va);
    if (page == NULL)
        false;

    return vm_do_claim_page(page);
}

/* Claim the PAGE and set up the mmu. */
// 새로운 프레임을 가져와, 페이지의 가상주소와 새로운 프레임(물리 메모리)을 페이지 테이블(pml4)에 매핑
static bool
vm_do_claim_page(struct page *page)
{
    struct frame *frame = vm_get_frame();
    if (frame == NULL)
        return false;

    /* Set links */
    frame->page = page;
    page->frame = frame;

    /* Insert page table entry to map page's VA to frame's PA. */
    struct thread *curr = thread_current();
    // va에 매핑된 것이 없고 && va와 새 프레임을 매핑한 경우 -> true
    bool success = (pml4_get_page(curr->pml4, page->va) == NULL && pml4_set_page(curr->pml4, page->va, frame->kva, page->writable));

    return success ? swap_in(page, frame->kva) : false;
}

/* Initialize new supplemental page table */
void supplemental_page_table_init(struct supplemental_page_table *spt UNUSED)
{
    hash_init(&spt->spt_hash, page_hash, page_less, NULL);
}

/* Copy supplemental page table from src to dst */
// 자식이 부모의 실행 context를 상속할 필요가 있을 때 사용 됨
// 즉 새로운 프로세스를 위한 spt를 생성해야한다는 의미
bool supplemental_page_table_copy(struct supplemental_page_table *dst UNUSED, struct supplemental_page_table *src UNUSED)
{
    // scr에서 dst로 SPT을 복사하는 함수
    struct hash_iterator iterator;
    hash_first(&iterator, &src->spt_hash);

    while (hash_next(&iterator))
    {
        // hash_cur: 현재 elem을 리턴하거나, table의 끝인 null 포인터를 반환하거나
        struct page *parent_page = hash_entry(hash_cur(&iterator), struct page, hash_elem);

        // 가상 주소는 같아야 함 -> 각 프로세스는 동일한 가상 주소를 사용함 0 ~ 특정 주소
        enum vm_type type = parent_page->operations->type;
        void *upage = parent_page->va;
        bool writable = parent_page->writable;

        // 1) type이 uninit인 경우
        if (VM_TYPE(type) == VM_UNINIT)
        {
            vm_initializer *init = parent_page->uninit.init;
            void *aux = parent_page->uninit.aux;

            struct container *container = (struct container *)malloc(sizeof(struct container));
            // memcopy를 해서 전송해야 함 -> uninit_destory()에서
            memcpy(container, aux, sizeof(struct container));

            // vm_alloc_page_with_initializer 안에서 spt_install을 해주므로 vm_alloc_page_with_initializer 사용
            vm_alloc_page_with_initializer(VM_ANON, upage, writable, init, container);
            continue;
        }

        if (type == VM_ANON) {
			if (!vm_alloc_page(type, upage, writable)) {
				return false;
			}
		}
        if(VM_TYPE(type) == VM_FILE){
            struct container * container = malloc(sizeof(struct container));
            container->file = parent_page->file.file;
            container->offset = parent_page->file.offset;
            container->read_bytes = parent_page->file.read_bytes;
            container->zero_bytes = parent_page->file.zero_bytes;

            if(!vm_alloc_page_with_initializer(type, upage, writable, NULL, container)) return false;
            continue;

        }

        // vm_claim_page으로 요청해서 매핑 & 페이지 타입에 맞게 초기화
        if (!vm_claim_page(upage))
        {
            return false;
        }

        // 매핑된 프레임에 내용 로딩
        struct page *dst_page = spt_find_page(dst, upage);
        memcpy(dst_page->frame->kva, parent_page->frame->kva, PGSIZE);
    }
    return true;
}

/* Free the resource hold by the supplemental page table */
// hash_clear - 해시를 비울 때 사용
// hash_destory - 해시 자체를 제거할 때 사용
// supplemental_page_table_kill은 모든 자원을 해제해야하는 것이지 spt 내부의 hash를 제거해야하는 게 아님
// 그러므로 hash_destory를 사용할 경우 spt 내부의 hash를 다시 초기화해줘야 함
void supplemental_page_table_kill(struct supplemental_page_table *spt UNUSED)
{
    /* TODO: Destroy all the supplemental_page_table hold by thread and
     * TODO: writeback all the modified contents to the storage. */
    hash_clear(&spt->spt_hash, hash_page_destory);
    // hahs_destory를 사용하면 spt 내부에 해시 자체가 제거되기 때문에 spt가 올바르게 동작하지 않음
    // hash_destroy(&spt->spt_hash, hash_page_destory);
    // hash_init(&spt->spt_hash, page_hash, page_less, NULL);
}
// =============================custom==========================

unsigned page_hash(struct hash_elem *elem, void *aux UNUSED)
{
    // hash_entry: 해당 hash_elem을 가지고 있는 page를 리턴하는 함수
    struct page *page = hash_entry(elem, struct page, hash_elem);
    // hash_bytes: 해당 page의 가상 주소를 hashed index로 변환하는 함수
    return hash_bytes(&page->va, sizeof(page->va));
}

bool page_less(struct hash_elem *elema, struct hash_elem *elemb, void *aux UNUSED)
{
    // page_less: 두 page의 주소값을 비교하여 왼쪽 값이 작으면 True 리턴하는 함수
    struct page *pagea = hash_entry(elema, struct page, hash_elem);
    struct page *pageb = hash_entry(elemb, struct page, hash_elem);
    return pagea->va < pageb->va;
}
void hash_page_destory(struct hash_elem *e, void *aux)
{
    struct page *page = hash_entry(e, struct page, hash_elem);
    destroy(page);
    free(page);
}