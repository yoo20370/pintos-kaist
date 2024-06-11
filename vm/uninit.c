/* uninit.c: Implementation of uninitialized page.
 *
 * All of the pages are born as uninit page. When the first page fault occurs,
 * the handler chain calls uninit_initialize (page->operations.swap_in).
 * The uninit_initialize function transmutes the page into the specific page
 * object (anon, file, page_cache), by initializing the page object,and calls
 * initialization callback that passed from vm_alloc_page_with_initializer
 * function.
 * */

#include "vm/vm.h"
#include "vm/uninit.h"

static bool uninit_initialize (struct page *page, void *kva);
static void uninit_destroy (struct page *page);

/* DO NOT MODIFY this struct */
static const struct page_operations uninit_ops = {
	.swap_in = uninit_initialize,
	.swap_out = NULL,
	.destroy = uninit_destroy,
	.type = VM_UNINIT,
};

/* DO NOT MODIFY this function */
void
uninit_new (struct page *page, void *va, vm_initializer *init,
		enum vm_type type, void *aux,
		bool (*initializer)(struct page *, enum vm_type, void *)) {
	ASSERT (page != NULL);
	
	*page = (struct page) {
		.operations = &uninit_ops,
		.va = va,
		.frame = NULL, /* no frame for now */
		.uninit = (struct uninit_page) {
			.init = init,
			.type = type,
			.aux = aux,
			.page_initializer = initializer,
		}
	};
}

/* Initalize the page on first fault */
/*
	첫 번째 페이지 폴트가 발생했을 때, 페이지를 초기화
	-> 함수 포인터를 통해 해당하는 초기화 함수를 호출한다. 
*/
static bool
uninit_initialize (struct page *page, void *kva) {
	struct uninit_page *uninit = &page->uninit;

	/* Fetch first, page_initialize may overwrite the values */
	vm_initializer *init = uninit->init;
	void *aux = uninit->aux;

	/* TODO: You may need to fix this function. */
	// 페이지에 실제 타입 설정, 프레임 주소 설정 하고 lazy_load 함수 실행하여 실제 데이터를 가져와 프레임에 적재  // init에는 lazy_loading 함수의 주소를 가리키고 있음 
	return uninit->page_initializer (page, uninit->type, kva) &&
		(init ? init (page, aux) : true);
}

/* Free the resources hold by uninit_page. Although most of pages are transmuted
 * to other page objects, it is possible to have uninit pages when the process
 * exit, which are never referenced during the execution.
 * PAGE will be freed by the caller. */
/*
	초기화되지 않은 페이지가 보유하고 있는 자원을 해제합니다. 
	대부분의 페이지는 다른 페이지 객체로 변환되지만, 
	프로세스가 종료될 때 초기화되지 않은 페이지가 있을 수 있으며, 이는 실행 중에 참조되지 않습니다. 
	호출자가 페이지를 해제할 것입니다.
*/
static void
uninit_destroy (struct page *page) {
	// TODO: 
	if(page == NULL) return;
	struct uninit_page *uninit UNUSED = &page->uninit;
	/*
		// uninit 페이지이지만 실제 초기화될 타입은 다를 수 있음
	*/
	enum vm_type type = uninit->type;
	struct container *container = (struct container*)page->uninit.aux;

	// 처음에 생성할 때 타입으로 구분했으므로 해제할 때도 마찬가지 
	// VM_ANON일 때 file_close하면 왜 실패할까 ??
	// -> 스택과 힙을 위한 페이지이므로 에러 발생 (추측)
	switch(VM_TYPE(type)){
		case VM_ANON :

			break;
		case VM_FILE :
			file_close(container->file);
			break;
	}
	// 왜 aux를 메모리 할당 해제하면 안 되는가 
	// 이전에는 해제된 주소에 접근을 했기 때문에 제거가 안 된거임
	free(container);
}
