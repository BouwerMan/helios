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

static inline void list_insert(struct list_head* link, struct list_head* new_link)
{
	new_link->prev = link->prev;
	new_link->next = link;
	new_link->prev->next = new_link;
	new_link->next->prev = new_link;
}

static inline void list_append(struct list_head* list, struct list_head* new_link)
{
	list_insert((struct list_head*)list, new_link);
}

static inline void list_prepend(struct list_head* list, struct list_head* new_link)
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
 * @list: the entry to test
 * @head: the head of the list
 */
static inline int list_is_first(const struct list_head* list, const struct list_head* head)
{
	return list->prev == head;
}

/**
 * list_is_last - tests whether @list is the last entry in list @head
 * @list: the entry to test
 * @head: the head of the list
 */
static inline int list_is_last(const struct list_head* list, const struct list_head* head)
{
	return list->next == head;
}

/**
 * list_is_head - tests whether @list is the list @head
 * @list: the entry to test
 * @head: the head of the list
 */
static inline int list_is_head(const struct list_head* list, const struct list_head* head)
{
	return list == head;
}

#define list_entry_is_head(pos, head, member) list_is_head(&pos->member, (head))

#define list_entry(link, type, member) container_of(link, type, member)

#define list_first_entry(link, type, member) list_entry((link)->next, type, member)

#define list_last_entry(link, type, member) list_entry((link)->prev, type, member)

#define list_head(list, type, member) list_entry((list)->next, type, member)

#define list_tail(list, type, member) list_entry((list)->prev, type, member)

#define list_next(element) (element->next)

#define list_next_entry(pos, member) list_entry((pos)->member.next, typeof(*(pos)), member)

#define list_for_each(pos, head) for (pos = (head)->next; !list_is_head(pos, (head)); pos = pos->next)

#define list_for_each_entry(pos, head, member)                                                           \
	for (pos = list_first_entry(head, typeof(*pos), member); !list_entry_is_head(pos, head, member); \
	     pos = list_next_entry(pos, member))

#define HLIST_HEAD_INIT	     { .first = nullptr }
#define HLIST_HEAD(name)     struct hlist_head name = { .first = nullptr }
#define INIT_HLIST_HEAD(ptr) ((ptr)->first = nullptr)
static inline void INIT_HLIST_NODE(struct hlist_node* h)
{
	h->next = nullptr;
	h->pprev = nullptr;
}
