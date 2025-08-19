/**
 * list.h - My custom linked-list implementation
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2025 Dylan Parks
 *
 * This file is a derivative work based on the Linux kernel file:
 * include/linux/list.h
 *
 * The original file from the Linux kernel is licensed under GPL-2.0
 * (SPDX-License-Identifier: GPL-2.0) and is copyrighted by the
 * Linux kernel contributors.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 */
#pragma once

#include <kernel/container_of.h>
#include <kernel/types.h>
#include <stddef.h>

static constexpr uptr LIST_POISON1 = 0x100;
static constexpr uptr LIST_POISON2 = 0x122;

/**
 * __WRITE_ONCE - Ensures a value is written to a variable exactly once.
 * @x: The variable to write to.
 * @val: The value to write to the variable.
 *
 * This macro uses a volatile cast to prevent the compiler from optimizing
 * away the write operation, ensuring that the value is written exactly once.
 */
#define __WRITE_ONCE(x, val)                        \
	do {                                        \
		*(volatile typeof(x)*)&(x) = (val); \
	} while (0)

#define LIST_HEAD_INIT(name) { &(name), &(name) }

#define LIST_HEAD(name) struct list_head name = LIST_HEAD_INIT(name)

static inline void list_init(struct list_head* list)
{
	list->next = list;
	list->prev = list;
}

static inline int list_empty(struct list_head* list)
{
	return list->next == list;
}

static inline void list_insert(struct list_head* link,
			       struct list_head* new_link)
{
	new_link->prev = link->prev;
	new_link->next = link;
	new_link->prev->next = new_link;
	new_link->next->prev = new_link;
}

static inline void list_append(struct list_head* list,
			       struct list_head* new_link)
{
	list_insert((struct list_head*)list, new_link);
}

static inline void list_prepend(struct list_head* list,
				struct list_head* new_link)
{
	list_insert(list->next, new_link);
}

static inline void list_remove(struct list_head* link)
{
	link->prev->next = link->next;
	link->next->prev = link->prev;
}

static inline void list_move(struct list_head* link, struct list_head* new_link)
{
	list_remove(link);
	list_append(new_link, link);
}

/**
 * list_is_first -- tests whether @list is the first entry in list @head
 * @head: the head of the list
 * @list: the entry to test
 */
static inline int list_is_first(const struct list_head* head,
				const struct list_head* list)
{
	return list->prev == head;
}

/**
 * list_is_last - tests whether @list is the last entry in list @head
 * @head: the head of the list
 * @list: the entry to test
 */
static inline int list_is_last(const struct list_head* head,
			       const struct list_head* list)
{
	return list->next == head;
}

/**
 * list_is_head - tests whether @list is the list @head
 * @head: the head of the list
 * @list: the entry to test
 */
static inline bool list_is_head(const struct list_head* head,
				const struct list_head* list)
{
	return list == head;
}

/*
 * Insert a new entry between two known consecutive entries.
 *
 * This is only for internal list manipulation where we know
 * the prev/next entries already!
 */
static inline void __list_insert(struct list_head* new,
				 struct list_head* next,
				 struct list_head* prev)
{
	next->prev = new;

	new->next = next;
	new->prev = prev;

	__WRITE_ONCE(prev->next, new);
}

/**
 * list_add - add a new entry
 * @head: list head to add it after
 * @new: new entry to be added
 *
 * Insert a new entry after the specified head.
 * This is good for implementing stacks.
 */
static inline void list_add(struct list_head* head, struct list_head* new)
{
	__list_insert(new, head->next, head);
}

static inline void list_add_tail(struct list_head* head, struct list_head* new)
{
	__list_insert(new, head, head->prev);
}

/*
 * Delete a list entry by making the prev/next entries
 * point to each other.
 *
 * This is only for internal list manipulation where we know
 * the prev/next entries already!
 */
static inline void __list_del(struct list_head* prev, struct list_head* next)
{
	next->prev = prev;
	__WRITE_ONCE(prev->next, next);
}

/**
 * list_del - deletes entry from list.
 * @entry: the element to delete from the list.
 * Note: list_empty() on entry does not return true after this, the entry is
 * in an undefined state.
 */
static inline void list_del(struct list_head* entry)
{
	__list_del(entry->prev, entry->next);
	entry->next = (void*)LIST_POISON1;
	entry->prev = (void*)LIST_POISON2;
}

#define list_entry_is_head(pos, head, member) list_is_head((head), &pos->member)

#define list_entry(link, type, member) container_of(link, type, member)

/**
 * list_first_entry - get the first element from a list
 * @link: the list head to take the element from
 * @type: the type of the struct this is embedded in
 * @member: the name of the list_head within the struct
 *
 * Note, that list is expected to be not empty.
 */
#define list_first_entry(link, type, member) \
	list_entry((link)->next, type, member)

#define list_last_entry(link, type, member) \
	list_entry((link)->prev, type, member)

#define list_head(list, type, member) list_entry((list)->next, type, member)

#define list_tail(list, type, member) list_entry((list)->prev, type, member)

#define list_next(element) (element->next)

#define list_next_entry(pos, member) \
	list_entry((pos)->member.next, typeof(*(pos)), member)

#define list_for_each(pos, head) \
	for (pos = (head)->next; !list_is_head((head), pos); pos = pos->next)

/**
 * list_for_each_entry	-	iterate over list of given type
 * @pos:	the type * to use as a loop cursor.
 * @head:	the head for your list.
 * @member:	the name of the list_head within the struct.
 */
#define list_for_each_entry(pos, head, member)                   \
	for (pos = list_first_entry(head, typeof(*pos), member); \
	     !list_entry_is_head(pos, head, member);             \
	     pos = list_next_entry(pos, member))

/**
 * list_for_each_entry_safe - iterate over list of given type safe against removal of list entry
 * @pos:	the type * to use as a loop cursor.
 * @n:		another type * to use as temporary storage
 * @head:	the head for your list.
 * @member:	the name of the list_head within the struct.
 */
#define list_for_each_entry_safe(pos, n, head, member)           \
	for (pos = list_first_entry(head, typeof(*pos), member), \
	    n = list_next_entry(pos, member);                    \
	     !list_entry_is_head(pos, head, member);             \
	     pos = n, n = list_next_entry(n, member))

/*
 * Double linked lists with a single pointer list head.
 * Mostly useful for hash tables where the two pointer list head is
 * too wasteful.
 * You lose the ability to access the tail in O(1).
 */

#define HLIST_HEAD_INIT	     { .first = nullptr }
#define HLIST_HEAD(name)     struct hlist_head name = { .first = nullptr }
#define INIT_HLIST_HEAD(ptr) ((ptr)->first = nullptr)
static inline void INIT_HLIST_NODE(struct hlist_node* h)
{
	h->next = nullptr;
	h->pprev = nullptr;
}

/**
 * hlist_unhashed - Has node been removed from list and reinitialized?
 * @h: Node to be checked
 *
 * Not that not all removal functions will leave a node in unhashed
 * state.  For example, hlist_nulls_del_init_rcu() does leave the
 * node in unhashed state, but hlist_nulls_del() does not.
 */
static inline int hlist_unhashed(const struct hlist_node* h)
{
	return !h->pprev;
}

/**
 * hlist_empty - Is the specified hlist_head structure an empty hlist?
 * @h: Structure to check.
 */
static inline int hlist_empty(const struct hlist_head* h)
{
	return !h->first;
}

static inline void __hlist_del(struct hlist_node* n)
{
	struct hlist_node* next = n->next;
	struct hlist_node** pprev = n->pprev;

	__WRITE_ONCE(*pprev, next);
	if (next) __WRITE_ONCE(next->pprev, pprev);
}

/**
 * hlist_del - Delete the specified hlist_node from its list
 * @n: Node to delete.
 *
 * Note that this function leaves the node in hashed state.  Use
 * hlist_del_init() or similar instead to unhash @n.
 */
static inline void hlist_del(struct hlist_node* n)
{
	__hlist_del(n);
	n->next = (void*)LIST_POISON1;
	n->pprev = (void*)LIST_POISON2;
}

/**
 * hlist_del_init - Delete the specified hlist_node from its list and initialize
 * @n: Node to delete.
 *
 * Note that this function leaves the node in unhashed state.
 */
static inline void hlist_del_init(struct hlist_node* n)
{
	if (!hlist_unhashed(n)) {
		__hlist_del(n);
		INIT_HLIST_NODE(n);
	}
}

/**
 * hlist_add_head - add a new entry at the beginning of the hlist
 * @h: hlist head to add it after
 * @n: new entry to be added
 *
 * Insert a new entry after the specified head.
 * This is good for implementing stacks.
 */
static inline void hlist_add_head(struct hlist_head* h, struct hlist_node* n)
{
	struct hlist_node* first = h->first;
	n->next = first;

	if (first) {
		first->pprev = &n->next;
	}

	h->first = n;
	n->pprev = &h->first;
}

/**
 * hlist_add_before - add a new entry before the one specified
 * @n: new entry to be added
 * @next: hlist node to add it before, which must be non-NULL
 */
static inline void hlist_add_before(struct hlist_node* n,
				    struct hlist_node* next)
{
	n->pprev = next->pprev;
	n->next = next;
	*n->pprev = n;
	next->pprev = &n->next;
}

/**
 * hlist_add_behind - add a new entry after the one specified
 * @n: new entry to be added
 * @prev: hlist node to add it after, which must be non-NULL
 */
static inline void hlist_add_behind(struct hlist_node* n,
				    struct hlist_node* prev)
{
	n->next = prev->next;
	prev->next = n;
	n->pprev = &prev->next;

	if (n->next) {
		n->next->pprev = &n->next;
	}
}

#define hlist_entry(ptr, type, member) container_of(ptr, type, member)

#define hlist_for_each(pos, head) \
	for (pos = (head)->first; pos; pos = pos->next)

#define hlist_entry_safe(ptr, type, member)                             \
	({                                                              \
		typeof(ptr) ____ptr = (ptr);                            \
		____ptr ? hlist_entry(____ptr, type, member) : nullptr; \
	})

/**
 * hlist_for_each_entry	- iterate over list of given type
 * @pos:	the type * to use as a loop cursor.
 * @head:	the head for your list.
 * @member:	the name of the hlist_node within the struct.
 */
#define hlist_for_each_entry(pos, head, member)                             \
	for (pos = hlist_entry_safe((head)->first, typeof(*(pos)), member); \
	     pos;                                                           \
	     pos = hlist_entry_safe(                                        \
		     (pos)->member.next, typeof(*(pos)), member))

/**
 * hlist_count_nodes - count nodes in the hlist
 * @head:	the head for your hlist.
 */
static inline size_t hlist_count_nodes(struct hlist_head* head)
{
	struct hlist_node* pos;
	size_t count = 0;

	hlist_for_each (pos, head) {
		count++;
	}

	return count;
}
