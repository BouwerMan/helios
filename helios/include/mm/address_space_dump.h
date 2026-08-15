#pragma once
#include "lib/log.h"
#include "mm/address_space.h"

static inline void VAS_DUMP(struct address_space* vas)
{
	if (!vas) return;

	log_debug("VAS dump: PML4 phys=0x%016lx pml4=%p",
		  (unsigned long)vas->pml4_phys,
		  (void*)vas->pml4);

	log_debug(
		"Start              | End                | Prot  | Flags | Kind   | Share  | Details");
	log_debug(
		"--------------------------------------------------------------------------------------------------------------");

	struct memory_region* mr;
	list_for_each_entry (mr, &vas->mr_list, list) {
		const char* kind = (mr->kind == MR_FILE)   ? "FILE" :
				   (mr->kind == MR_ANON)   ? "ANON" :
				   (mr->kind == MR_DEVICE) ? "DEVICE" :
							     "UNKNOWN";
		const char* share = mr->is_private ? "priv" : "shared";

		switch (mr->kind) {
		case MR_FILE:
			log_debug(
				"0x%016lx | 0x%016lx | 0x%04lx | 0x%04lx | %-6s | %-6s | "
				"inode=%p off=[0x%lx..0x%lx) pgoff=%zu delta=%u",
				(unsigned long)mr->start,
				(unsigned long)mr->end,
				(unsigned long)mr->prot,
				(unsigned long)mr->flags,
				kind,
				share,
				(void*)mr->file.inode,
				(unsigned long)mr->file.file_lo,
				(unsigned long)mr->file.file_hi,
				(size_t)mr->file.pgoff,
				(unsigned)mr->file.delta);
			break;
		case MR_ANON:
			log_debug(
				"0x%016lx | 0x%016lx | 0x%04lx | 0x%04lx | %-6s | %-6s | tag=%u",
				(unsigned long)mr->start,
				(unsigned long)mr->end,
				(unsigned long)mr->prot,
				(unsigned long)mr->flags,
				kind,
				share,
				(unsigned)mr->anon.tag);
			break;
		case MR_DEVICE:
			log_debug(
				"0x%016lx | 0x%016lx | 0x%04lx | 0x%04lx | %-6s | %-6s | paddr=0x%016lx",
				(unsigned long)mr->start,
				(unsigned long)mr->end,
				(unsigned long)mr->prot,
				(unsigned long)mr->flags,
				kind,
				share,
				(unsigned long)mr->dev.paddr);
			break;
		default:
			log_debug(
				"0x%016lx | 0x%016lx | 0x%04lx | 0x%04lx | %-6s | %-6s | UNKNOWN",
				(unsigned long)mr->start,
				(unsigned long)mr->end,
				(unsigned long)mr->prot,
				(unsigned long)mr->flags,
				kind,
				share);
			break;
		}
	}
}
