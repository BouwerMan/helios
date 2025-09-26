#include "mm/page.h"
#include "arch/atomic.h"
#include "kernel/assert.h"

bool trylock_page(struct page* page)
{
	return try_set_flag_mask(&page->flags, PG_LOCKED);
}

void lock_page(struct page* page)
{
	while (!trylock_page(page)) {
		waitqueue_prepare_wait(&page->wq);
		if (trylock_page(page)) {
			waitqueue_cancel_wait(&page->wq);
			break;
		}
		waitqueue_commit_sleep(&page->wq);
	}

	kassert(flags_test_acquire(&page->flags, PG_LOCKED),
		"Failed to acquire page lock");
}

bool tryunlock_page(struct page* page)
{
	return try_clear_flag_mask(&page->flags, PG_LOCKED);
}

void unlock_page(struct page* page)
{
	clear_flag_mask(&page->flags, PG_LOCKED);

	if (waitqueue_has_waiters(&page->wq)) {
		waitqueue_wake_one(&page->wq);
	}
}

void wait_on_page_locked(struct page* page)
{
	while (flags_test_acquire(&page->flags, PG_LOCKED)) {
		waitqueue_sleep(&page->wq);
	}
}
