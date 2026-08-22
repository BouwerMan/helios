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

#include <stddef.h>

#include "kernel/container_of.h"
#include "kernel/rwonce.h"
#include "kernel/types.h"

static constexpr uptr LIST_POISON1 = 0x100;
static constexpr uptr LIST_POISON2 = 0x122;

// FIXME: I mix up the list and head orders a ton in this, standardize it or
// I'll fight you
// TODO: This will be the next big change, I'm going back to the linux way

#define LIST_HEAD_INIT(name) { &(name), &(name) }

#define LIST_HEAD(name) struct list_head name = LIST_HEAD_INIT(name)

/**
 * @brief Initializes a list_head structure.
 *
 * @param list list_head structure to initialize.
 *
 * Sets the list_head to point to itself. If it is a list header, the
 * result is an empty list.
 */
static inline void INIT_LIST_HEAD(struct list_head* list)
{
	WRITE_ONCE(list->next, list);
	WRITE_ONCE(list->prev, list);
}

static inline void list_init(struct list_head* list)
{
	WRITE_ONCE(list->next, list);
	WRITE_ONCE(list->prev, list);
}

static inline bool list_empty(const struct list_head* list)
{
	return list->next == list;
}

/**
 * @brief Checks whether an entry is the first in a list.
 *
 * @param head Head of the list.
 * @param list Entry to test.
 *
 * @return true if list is the first entry, false otherwise.
 */
static inline bool list_is_first(const struct list_head* head,
				 const struct list_head* list)
{
	return list->prev == head;
}

/**
 * @brief Checks whether an entry is the last in a list.
 *
 * @param head Head of the list.
 * @param list Entry to test.
 *
 * @return true if list is the last entry, false otherwise.
 */
static inline bool list_is_last(const struct list_head* head,
				const struct list_head* list)
{
	return head == list->next;
}

/**
 * @brief Checks whether an entry is the head of a list.
 *
 * @param head Head of the list.
 * @param list Entry to test.
 *
 * @return true if list is head, false otherwise.
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

	WRITE_ONCE(prev->next, new);
}

/**
 * @brief Adds a new entry after a list head.
 *
 * @param head List head to add the entry after.
 * @param new New entry to add.
 *
 * Useful for implementing stacks.
 */
static inline void list_add(struct list_head* head, struct list_head* new)
{
	__list_insert(new, head->next, head);
}

/**
 * @brief Adds a new entry before a list head.
 *
 * @param head List head to add the entry before.
 * @param new New entry to add.
 *
 * Useful for implementing queues.
 */
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
	WRITE_ONCE(prev->next, next);
}

/**
 * @brief Deletes an entry from a list.
 *
 * @param entry Element to delete from the list.
 */
static inline void list_del(struct list_head* entry)
{
	__list_del(entry->prev, entry->next);
	list_init(entry);
	// entry->next = (void*)LIST_POISON1;
	// entry->prev = (void*)LIST_POISON2;
}

static inline void __list_del_entry(struct list_head* entry)
{
	__list_del(entry->prev, entry->next);
	list_init(entry);
}

/**
 * @brief Moves an entry from one list to the head of another.
 *
 * @param list Entry to move.
 * @param head Head that will precede the entry.
 */
static inline void list_move(struct list_head* list, struct list_head* head)
{
	__list_del_entry(list);
	list_add(head, list);
}

/**
 * @brief Moves an entry from one list to the tail of another.
 *
 * @param list Entry to move.
 * @param head Head that will follow the entry.
 */
static inline void list_move_tail(struct list_head* list,
				  struct list_head* head)
{
	__list_del_entry(list);
	list_add_tail(head, list);
}

static inline void __list_splice(const struct list_head* list,
				 struct list_head* prev,
				 struct list_head* next)
{
	struct list_head* first = list->next;
	struct list_head* last = list->prev;

	first->prev = prev;
	prev->next = first;

	last->next = next;
	next->prev = last;
}

/**
 * @brief Joins two lists. Designed for stacks.
 *
 * @param list New list to add.
 * @param head Place to add it in the first list.
 */
static inline void list_splice(const struct list_head* list,
			       struct list_head* head)
{
	if (!list_empty(list)) __list_splice(list, head, head->next);
}

/**
 * @brief Joins two lists, each list being a queue.
 *
 * @param list New list to add.
 * @param head Place to add it in the first list.
 */
static inline void list_splice_tail(struct list_head* list,
				    struct list_head* head)
{
	if (!list_empty(list)) __list_splice(list, head->prev, head);
}

/**
 * @brief Joins two lists and reinitializes the emptied list.
 *
 * @param list New list to add. Reinitialized after the join.
 * @param head Place to add it in the first list.
 */
static inline void list_splice_init(struct list_head* list,
				    struct list_head* head)
{
	if (!list_empty(list)) {
		__list_splice(list, head, head->next);
		INIT_LIST_HEAD(list);
	}
}

#define list_entry_is_head(pos, head, member) \
	list_is_head((head), &(pos)->member)

/**
 * @brief Gets the struct that contains a list_head entry.
 *
 * @param ptr The list_head pointer.
 * @param type Type of the struct this is embedded in.
 * @param member Name of the list_head field within the struct.
 */
#define list_entry(ptr, type, member) container_of(ptr, type, member)

/**
 * @brief Gets the first element from a list.
 *
 * @param link List head to take the element from.
 * @param type Type of the struct this is embedded in.
 * @param member Name of the list_head field within the struct.
 *
 * @note The list must not be empty.
 */
#define list_first_entry(link, type, member) \
	list_entry((link)->next, type, member)

/**
 * @brief Gets the first element from a list.
 *
 * @param ptr List head to take the element from.
 * @param type Type of the struct this is embedded in.
 * @param member Name of the list_head field within the struct.
 *
 * @return The first element, or NULL if the list is empty.
 */
#define list_first_entry_or_null(ptr, type, member)                       \
	({                                                                \
		struct list_head* head__ = (ptr);                         \
		struct list_head* pos__ = head__->next;                   \
		pos__ != head__ ? list_entry(pos__, type, member) : NULL; \
	})

#define list_last_entry(link, type, member) \
	list_entry((link)->prev, type, member)

#define list_head(list, type, member) list_entry((list)->next, type, member)

#define list_tail(list, type, member) list_entry((list)->prev, type, member)

#define list_next(element) ((element)->next)

/**
 * @brief Gets the next element in a list.
 *
 * @param pos The type pointer to use as the cursor.
 * @param member Name of the list_head field within the struct.
 */
#define list_next_entry(pos, member) \
	list_entry((pos)->member.next, typeof(*(pos)), member)

#define list_for_each(pos, head)                               \
	for ((pos) = (head)->next; !list_is_head((head), pos); \
	     (pos) = (pos)->next)

static inline size_t list_count_nodes(struct list_head* head)
{
	struct list_head* pos;
	size_t count = 0;
	list_for_each (pos, head)
		count++;
	return count;
}

/**
 * @brief Iterates over a list of a given type.
 *
 * @param pos The type pointer to use as the loop cursor.
 * @param head Head of the list.
 * @param member Name of the list_head field within the struct.
 */
#define list_for_each_entry(pos, head, member)                       \
	for ((pos) = list_first_entry(head, typeof(*(pos)), member); \
	     !list_entry_is_head(pos, head, member);                 \
	     (pos) = list_next_entry(pos, member))

/**
 * @brief Iterates over a list of a given type, safe against entry removal.
 *
 * @param pos The type pointer to use as the loop cursor.
 * @param n Another type pointer to use as temporary storage.
 * @param head Head of the list.
 * @param member Name of the list_head field within the struct.
 */
#define list_for_each_entry_safe(pos, n, head, member)               \
	for ((pos) = list_first_entry(head, typeof(*(pos)), member), \
	    (n) = list_next_entry(pos, member);                      \
	     !list_entry_is_head(pos, head, member);                 \
	     (pos) = (n), (n) = list_next_entry(n, member))

/**
 * @brief Continues iteration over a list from the current position.
 *
 * @param pos The list_head to use as the loop cursor.
 * @param head Head of the list.
 */
#define list_for_each_continue(pos, head)                     \
	for ((pos) = (pos)->next; !list_is_head(pos, (head)); \
	     (pos) = (pos)->next)

/**
 * @brief Continues iteration over a list of a given type from the current
 * position.
 *
 * @param pos The type pointer to use as the loop cursor.
 * @param head Head of the list.
 * @param member Name of the list_head field within the struct.
 */
#define list_for_each_entry_continue(pos, head, member) \
	for ((pos) = list_next_entry(pos, member);      \
	     !list_entry_is_head(pos, head, member);    \
	     (pos) = list_next_entry(pos, member))

/**
 * @brief Iterates over a list of a given type from the current position.
 *
 * @param pos The type pointer to use as the loop cursor.
 * @param head Head of the list.
 * @param member Name of the list_head field within the struct.
 */
#define list_for_each_entry_from(pos, head, member)    \
	for (; !list_entry_is_head(pos, head, member); \
	     (pos) = list_next_entry(pos, member))

/**
 * @brief Gets the next element in a list, wrapping around at the end.
 *
 * @param pos The type pointer to use as the cursor.
 * @param head List head to take the element from.
 * @param member Name of the list_head field within the struct.
 *
 * @return The next element, or the first element if pos is the last.
 *
 * @note The list must not be empty.
 */
#define list_next_entry_circular(pos, head, member)               \
	(list_is_last(head, &(pos)->member) ?                     \
		 list_first_entry(head, typeof(*(pos)), member) : \
		 list_next_entry(pos, member))

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
 * @brief Checks whether a node was removed from its list and reinitialized.
 *
 * @param h Node to check.
 *
 * @return true if the node is unhashed, false otherwise.
 *
 * @note Not all removal functions leave a node in the unhashed state.
 */
static inline int hlist_unhashed(const struct hlist_node* h)
{
	return !h->pprev;
}

/**
 * @brief Checks whether an hlist_head structure is empty.
 *
 * @param h Structure to check.
 *
 * @return true if the hlist is empty, false otherwise.
 */
static inline int hlist_empty(const struct hlist_head* h)
{
	return !h->first;
}

static inline void __hlist_del(struct hlist_node* n)
{
	struct hlist_node* next = n->next;
	struct hlist_node** pprev = n->pprev;

	WRITE_ONCE(*pprev, next);
	if (next) WRITE_ONCE(next->pprev, pprev);
}

/**
 * @brief Deletes an hlist_node from its list.
 *
 * @param n Node to delete.
 *
 * @note Leaves the node in the hashed state. Use hlist_del_init() to
 * unhash n instead.
 */
static inline void hlist_del(struct hlist_node* n)
{
	__hlist_del(n);
	n->next = (struct hlist_node*)LIST_POISON1;
	n->pprev = (struct hlist_node**)LIST_POISON2;
}

/**
 * @brief Deletes an hlist_node from its list and reinitializes it.
 *
 * @param n Node to delete.
 *
 * @note Leaves the node in the unhashed state.
 */
static inline void hlist_del_init(struct hlist_node* n)
{
	if (!hlist_unhashed(n)) {
		__hlist_del(n);
		INIT_HLIST_NODE(n);
	}
}

/**
 * @brief Adds a new entry at the start of an hlist.
 *
 * @param h hlist head to add the entry after.
 * @param n New entry to add.
 *
 * Useful for implementing stacks.
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
 * @brief Adds a new entry before a specified node.
 *
 * @param n New entry to add.
 * @param next hlist node to add it before. Must not be NULL.
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
 * @brief Adds a new entry after a specified node.
 *
 * @param n New entry to add.
 * @param prev hlist node to add it after. Must not be NULL.
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
	for ((pos) = (head)->first; pos; (pos) = (pos)->next)

#define hlist_entry_safe(ptr, type, member)                             \
	({                                                              \
		typeof(ptr) ____ptr = (ptr);                            \
		____ptr ? hlist_entry(____ptr, type, member) : nullptr; \
	})

/**
 * @brief Iterates over an hlist of a given type.
 *
 * @param pos The type pointer to use as the loop cursor.
 * @param head Head of the hlist.
 * @param member Name of the hlist_node field within the struct.
 */
#define hlist_for_each_entry(pos, head, member)                               \
	for ((pos) = hlist_entry_safe((head)->first, typeof(*(pos)), member); \
	     pos;                                                             \
	     (pos) = hlist_entry_safe((pos)->member.next,                     \
				      typeof(*(pos)),                         \
				      member))

/**
 * @brief Iterates over an hlist of a given type, safe against entry removal.
 *
 * @param pos The type pointer to use as the loop cursor.
 * @param n An hlist_node to use as temporary storage.
 * @param head Head of the hlist.
 * @param member Name of the hlist_node field within the struct.
 */
#define hlist_for_each_entry_safe(pos, n, head, member)                       \
	for ((pos) = hlist_entry_safe((head)->first, typeof(*(pos)), member); \
	     (pos) && ({                                                      \
		     (n) = (pos)->member.next;                                \
		     1;                                                       \
	     });                                                              \
	     (pos) = hlist_entry_safe(n, typeof(*(pos)), member))

/**
 * @brief Counts the nodes in an hlist.
 *
 * @param head Head of the hlist.
 *
 * @return The number of nodes in the hlist.
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
