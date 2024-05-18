/* This file is derived from source code for the Nachos
   instructional operating system.  The Nachos copyright notice
   is reproduced in full below. */

/* Copyright (c) 1992-1996 The Regents of the University of California.
   All rights reserved.

   Permission to use, copy, modify, and distribute this software
   and its documentation for any purpose, without fee, and
   without written agreement is hereby granted, provided that the
   above copyright notice and the following two paragraphs appear
   in all copies of this software.

   IN NO EVENT SHALL THE UNIVERSITY OF CALIFORNIA BE LIABLE TO
   ANY PARTY FOR DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR
   CONSEQUENTIAL DAMAGES ARISING OUT OF THE USE OF THIS SOFTWARE
   AND ITS DOCUMENTATION, EVEN IF THE UNIVERSITY OF CALIFORNIA
   HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

   THE UNIVERSITY OF CALIFORNIA SPECIFICALLY DISCLAIMS ANY
   WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
   WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
   PURPOSE.  THE SOFTWARE PROVIDED HEREUNDER IS ON AN "AS IS"
   BASIS, AND THE UNIVERSITY OF CALIFORNIA HAS NO OBLIGATION TO
   PROVIDE MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR
   MODIFICATIONS.
   */

#include "threads/synch.h"
#include <stdio.h>
#include <string.h>
#include "threads/interrupt.h"
#include "threads/thread.h"

/* Initializes semaphore SEMA to VALUE.  A semaphore is a
   nonnegative integer along with two atomic operators for
   manipulating it:

   - down or "P": wait for the value to become positive, then
   decrement it.

   - up or "V": increment the value (and wake up one waiting
   thread, if any). */
void sema_init(struct semaphore *sema, unsigned value)
{
	ASSERT(sema != NULL);

	sema->value = value;
	list_init(&sema->waiters);
}

/* Down or "P" operation on a semaphore.  Waits for SEMA's value
   to become positive and then atomically decrements it.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but if it sleeps then the next scheduled
   thread will probably turn interrupts back on. This is
   sema_down function. */
void sema_down(struct semaphore *sema)
{
	enum intr_level old_level;

	ASSERT(sema != NULL);
	ASSERT(!intr_context());

	old_level = intr_disable();
	while (sema->value == 0)
	{
		list_insert_ordered(&sema->waiters, &thread_current()->elem, &compare_priority, NULL);
		thread_block();
	}
	sema->value--;
	intr_set_level(old_level);
}

/* Down or "P" operation on a semaphore, but only if the
   semaphore is not already 0.  Returns true if the semaphore is
   decremented, false otherwise.

   This function may be called from an interrupt handler. */
bool sema_try_down(struct semaphore *sema)
{
	enum intr_level old_level;
	bool success;

	ASSERT(sema != NULL);

	old_level = intr_disable();
	if (sema->value > 0)
	{
		sema->value--;
		success = true;
	}
	else
		success = false;
	intr_set_level(old_level);

	return success;
}

/* Up or "V" operation on a semaphore.  Increments SEMA's value
   and wakes up one thread of those waiting for SEMA, if any.

   This function may be called from an interrupt handler. */
void sema_up(struct semaphore *sema)
{
	enum intr_level old_level;

	ASSERT(sema != NULL);

	old_level = intr_disable();
	sema->value++; // unblock을 하는 과정에서 우선순위 높은 쓰레드에게 실행권한을 뺏기게 되므로 미리 value를 올림
	if (!list_empty(&sema->waiters))
	{
		list_sort(&sema->waiters, compare_priority, NULL);
		thread_unblock(list_entry(list_pop_front(&sema->waiters), struct thread, elem));
	}
	intr_set_level(old_level);
}

static void sema_test_helper(void *sema_);

/* Self-test for semaphores that makes control "ping-pong"
   between a pair of threads.  Insert calls to printf() to see
   what's going on. */
void sema_self_test(void)
{
	struct semaphore sema[2];
	int i;

	printf("Testing semaphores...");
	sema_init(&sema[0], 0);
	sema_init(&sema[1], 0);
	thread_create("sema-test", PRI_DEFAULT, sema_test_helper, &sema);
	for (i = 0; i < 10; i++)
	{
		sema_up(&sema[0]);
		sema_down(&sema[1]);
	}
	printf("done.\n");
}

/* Thread function used by sema_self_test(). */
static void
sema_test_helper(void *sema_)
{
	struct semaphore *sema = sema_;
	int i;

	for (i = 0; i < 10; i++)
	{
		sema_down(&sema[0]);
		sema_up(&sema[1]);
	}
}

/* Initializes LOCK.  A lock can be held by at most a single
   thread at any given time.  Our locks are not "recursive", that
   is, it is an error for the thread currently holding a lock to
   try to acquire that lock.

   A lock is a specialization of a semaphore with an initial
   value of 1.  The difference between a lock and such a
   semaphore is twofold.  First, a semaphore can have a value
   greater than 1, but a lock can only be owned by a single
   thread at a time.  Second, a semaphore does not have an owner,
   meaning that one thread can "down" the semaphore and then
   another one "up" it, but with a lock the same thread must both
   acquire and release it.  When these restrictions prove
   onerous, it's a good sign that a semaphore should be used,
   instead of a lock. */
void lock_init(struct lock *lock)
{
	ASSERT(lock != NULL);

	lock->holder = NULL;
	sema_init(&lock->semaphore, 1);
	// list_init(&lock->holder->donors);
}

/* Acquires LOCK, sleeping until it becomes available if
   necessary.  The lock must not already be held by the current
   thread.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but interrupts will be turned back on if
   we need to sleep. */
lock_acquire(struct lock *lock)
{
	struct thread *curr;
	ASSERT(lock != NULL);
	ASSERT(!intr_context());
	ASSERT(!lock_held_by_current_thread(lock));

	if (lock->holder != NULL)
	{										 // 락을 소유하는 holder 쓰레드가 있다면
		curr = thread_current();			 // curr은 현재 진행중인 쓰레드
		curr->wait_on_lock = lock;			 // curr의 wait_on_lock을 지금 lock으로 설정
		donate_priority(lock->holder, curr); // 지금 쓰레드의 우선순위가 높다면 우선순위를 holder에게 기부
	}
	sema_down(&lock->semaphore);
	lock->holder = thread_current();
}

/* Tries to acquires LOCK and returns true if successful or false
   on failure.  The lock must not already be held by the current
   thread.

   This function will not sleep, so it may be called within an
   interrupt handler. */
bool lock_try_acquire(struct lock *lock)
{
	bool success;

	ASSERT(lock != NULL);
	ASSERT(!lock_held_by_current_thread(lock));

	success = sema_try_down(&lock->semaphore);
	if (success)
		lock->holder = thread_current();
	return success;
}

/* Releases LOCK, which must be owned by the current thread.
   This is lock_release function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to release a lock within an interrupt
   handler. */
void lock_release(struct lock *lock)
{
	enum intr_level old_level;
	ASSERT(lock != NULL);
	ASSERT(lock_held_by_current_thread(lock));

	old_level = intr_disable();

	// 세마포어를 올려 대기 중인 쓰레드가 즉시 깨어나게 함으로써,
	// 우선순위가 높은 쓰레드가 바로 실행될 수 있도록 함
	sema_up(&lock->semaphore);

	// 해당 락을 도네이션 리스트에서 제거하고 우선순위를 갱신
	donate_remove(lock);

	// 잠금 소유자를 NULL로 설정하여 잠금이 반납되었음을 표시
	lock->holder = NULL;

	intr_set_level(old_level);
}

/* Returns true if the current thread holds LOCK, false
   otherwise.  (Note that testing whether some other thread holds
   a lock would be racy.) */
bool lock_held_by_current_thread(const struct lock *lock)
{
	ASSERT(lock != NULL);

	return lock->holder == thread_current();
}

/* One semaphore in a list. */
struct semaphore_elem
{
	struct list_elem elem;		/* List element. */
	struct semaphore semaphore; /* This semaphore. */
};

/* Initializes condition variable COND.  A condition variable
   allows one piece of code to signal a condition and cooperating
   code to receive the signal and act upon it. */
void cond_init(struct condition *cond)
{
	ASSERT(cond != NULL);

	list_init(&cond->waiters);
}

/* Atomically releases LOCK and waits for COND to be signaled by
   some other piece of code.  After COND is signaled, LOCK is
   reacquired before returning.  LOCK must be held before calling
   this function.

   The monitor implemented by this function is "Mesa" style, not
   "Hoare" style, that is, sending and receiving a signal are not
   an atomic operation.  Thus, typically the caller must recheck
   the condition after the wait completes and, if necessary, wait
   again.

   A given condition variable is associated with only a single
   lock, but one lock may be associated with any number of
   condition variables.  That is, there is a one-to-many mapping
   from locks to condition variables.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but interrupts will be turned back on if
   we need to sleep. */
//
void cond_wait(struct condition *cond, struct lock *lock)
{
	struct semaphore_elem waiter;

	ASSERT(cond != NULL);
	ASSERT(lock != NULL);
	ASSERT(!intr_context());
	ASSERT(lock_held_by_current_thread(lock));

	sema_init(&waiter.semaphore, 0);
	list_push_back(&cond->waiters, &waiter.elem);
	lock_release(lock);
	sema_down(&waiter.semaphore);
	lock_acquire(lock);
}

/* If any threads are waiting on COND (protected by LOCK), then
   this function signals one of them to wake up from its wait.
   LOCK must be held before calling this function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to signal a condition variable within an
   interrupt handler. */
// wait 상태에 있는 쓰레드들에게 시그널을 보내서 한개의 쓰레드를 wake_up함
void cond_signal(struct condition *cond, struct lock *lock UNUSED)
{
	ASSERT(cond != NULL);
	ASSERT(lock != NULL);
	ASSERT(!intr_context());
	ASSERT(lock_held_by_current_thread(lock));

	if (!list_empty(&cond->waiters))
	{
		// semaphore_elem으로 타고 들어가서 thread의 priority를 비교하는 함수로 비교
		list_sort(&cond->waiters, &check_cond_priority, 0);
		sema_up(&list_entry(list_pop_front(&cond->waiters), struct semaphore_elem, elem)->semaphore);
	}
}

/* Wakes up all threads, if any, waiting on COND (protected by
   LOCK).  LOCK must be held before calling this function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to signal a condition variable within an
   interrupt handler. */
void cond_broadcast(struct condition *cond, struct lock *lock)
{
	ASSERT(cond != NULL);
	ASSERT(lock != NULL);

	while (!list_empty(&cond->waiters))
		cond_signal(cond, lock);
}

bool check_cond_priority(struct list_elem *a, struct list_elem *b, void *aux)
{
	struct semaphore_elem *sema_elem_A = list_entry(a, struct semaphore_elem, elem);
	struct semaphore_elem *sema_elem_B = list_entry(b, struct semaphore_elem, elem);

	struct list *wait_list_A = &sema_elem_A->semaphore.waiters;
	struct list *wait_list_B = &sema_elem_B->semaphore.waiters;

	struct thread *threadA = list_entry(list_begin(wait_list_A), struct thread, elem);
	struct thread *threadB = list_entry(list_begin(wait_list_B), struct thread, elem);

	if (threadA->priority > threadB->priority)
	{
		return true;
	}
	else
	{
		return false;
	}
}

bool compare_priority_d_elem(struct list_elem *a, struct list_elem *b, void *aux)
{
	struct thread *threadA = list_entry(a, struct thread, d_elem);
	struct thread *threadB = list_entry(b, struct thread, d_elem);

	if (threadA->priority < threadB->priority)
	{
		return true;
	}
	else
	{
		return false;
	}
}
void donate_priority(struct thread *holder, struct thread *receiver)
{
	enum intr_level old_level;

	// holder의 우선순위가 receiver의 우선순위보다 낮으면 holder의 우선순위를 업데이트
	if (holder->priority < receiver->priority)
		holder->priority = receiver->priority;

	old_level = intr_disable();

	// receiver를 holder의 도네이션 리스트에 추가
	list_push_front(&holder->donors, &receiver->d_elem);
	donate_nested(holder);
	// 이전 인터럽트 레벨로 복원
	intr_set_level(old_level);
}

// 쓰레드의 우선순위를 도네이션 기반으로 갱신하는 함수
void donate_update(struct thread *t)
{
	// 도네이션 리스트에서 가장 높은 우선순위를 가진 쓰레드 찾기
	struct list_elem *max_elem = list_max(&t->donors, compare_priority_d_elem, NULL);
	struct thread *max_thread = list_entry(max_elem, struct thread, d_elem);

	// 도네이션 리스트가 비어있지 않으면
	if (!list_empty(&t->donors))
	{
		// 초기 우선순위와 도네이션 리스트에서 가장 높은 우선순위를 비교하여 업데이트
		if (t->init_priority < max_thread->priority)
		{
			t->priority = max_thread->priority;
		}
		else
		{
			t->priority = t->init_priority;
		}
	}
	else
	{
		// 도네이션 리스트가 비어있다면 초기 우선순위로 복원
		t->priority = t->init_priority;
	}

	// 우선순위 변경으로 인해 선점이 필요할 수 있음
	preemption();
}

// 도네이션을 재귀적으로 처리하는 함수
void donate_nested(struct thread *t)
{
	struct thread *holder, *curr = t; // 락을 소유하는 holder와 현재 탐색 중인 curr

	while (curr->wait_on_lock != NULL)
	{
		// 현재 탐색 중인 쓰레드가 기다리고 있는 락의 소유자를 holder로 설정
		holder = curr->wait_on_lock->holder;

		// 만약 holder의 우선순위가 t의 우선순위보다 높다면 중단
		if (holder->priority > t->priority)
		{
			break;
		}

		// holder의 우선순위를 t의 우선순위로 업데이트
		holder->priority = t->priority;
		// curr를 holder로 갱신
		curr = holder;
	}
	donate_update(curr);
}

// 도네이션 리스트에서 특정 락을 요청한 쓰레드를 제거하는 함수
void donate_remove(struct lock *lock)
{
	enum intr_level old_level;
	struct thread *holder, *curr_thread;
	struct list_elem *curr;

	// 현재 실행 중인 쓰레드가 락을 반납하는 쓰레드임을 확인
	holder = thread_current();
	ASSERT(holder == lock->holder);

	// 도네이션 리스트가 비어있으면 반환
	if (list_empty(&holder->donors))
		return;

	curr = list_front(&holder->donors);
	old_level = intr_disable();

	// 도네이션 리스트를 순회하면서 특정 락을 요청한 쓰레드를 제거
	while (curr != list_tail(&holder->donors))
	{
		curr_thread = list_entry(curr, struct thread, d_elem);
		if (curr_thread->wait_on_lock == lock)
		{
			// 락을 요청한 쓰레드를 도네이션 리스트에서 제거
			list_remove(curr);
		}
		curr = list_next(curr);
	}

	// 도네이션 리스트에서 제거 후 우선순위 갱신
	donate_update(holder);
	intr_set_level(old_level);
}
