/**
 * @file kernel/bootinfo.c
 *
 * Copyright (C) 2025  Dylan Parks
 *
 * This file is part of HeliOS
 *
 * HeliOS is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

/**
* The whole point of this system is to make sure we keep the limine responses after bootloader reclaimation.
*/

#undef LOG_LEVEL
#define LOG_LEVEL 1
#define FORCE_LOG_REDEF
#include <util/log.h>
#undef FORCE_LOG_REDEF

#include <kernel/bootinfo.h>
#include <kernel/helios.h>
#include <kernel/panic.h>
#include <limine.h>
#include <mm/bootmem.h>
#include <mm/page.h>

__attribute__((used, section(".limine_requests"))) static volatile struct limine_hhdm_request hhdm_request = {
	.id = LIMINE_HHDM_REQUEST,
	.revision = 0
};

__attribute__((used, section(".limine_requests"))) static volatile struct limine_executable_address_request
	exe_addr_req = { .id = LIMINE_EXECUTABLE_ADDRESS_REQUEST, .revision = 0 };

// Finally, define the start and end markers for the Limine requests.
// These can also be moved anywhere, to any .c file, as seen fit.

__attribute__((used, section(".limine_requests_start"))) static volatile LIMINE_REQUESTS_START_MARKER;

__attribute__((used, section(".limine_requests_end"))) static volatile LIMINE_REQUESTS_END_MARKER;

#define MAX_MEMMAP_ENTRIES_PER_PAGE (PAGE_SIZE / sizeof(struct bootinfo_memmap_entry))

void bootinfo_init(struct limine_memmap_response* mmap)
{
	struct limine_hhdm_response* hhdm = hhdm_request.response;
	struct limine_executable_address_response* exec_addr = exe_addr_req.response;
	struct bootinfo* bootinfo = &kernel.bootinfo;

	// TODO: Framebuffer,SMBIOS, and EFI system table support?
	if (!mmap || !hhdm || !exec_addr) {
		panic("Boot info missing a response");
	}

	kassert(mmap->entry_count <= MAX_MEMMAP_ENTRIES_PER_PAGE &&
		"Boot info memory map entry count exceeds maximum allowed entries per page");

	struct bootinfo_memmap_entry* mmap_entries = (void*)PHYS_TO_HHDM(bootmem_alloc_page());

	for (size_t i = 0; i < mmap->entry_count; i++) {
		struct limine_memmap_entry* entry = mmap->entries[i];
		log_debug("%zu. Start Addr: %lx | Length: %lx | Type: %lu", i, entry->base, entry->length, entry->type);
		mmap_entries[i].base = entry->base;
		mmap_entries[i].length = entry->length;
		mmap_entries[i].type = entry->type;
	}

	bootinfo->memmap = mmap_entries;
	bootinfo->memmap_entry_count = mmap->entry_count;

	bootinfo->hhdm_offset = hhdm->offset;

	bootinfo->executable.physical_base = exec_addr->physical_base;
	bootinfo->executable.virtual_base = exec_addr->virtual_base;

	bootinfo->valid = true;
}
