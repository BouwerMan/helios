#include <arch/mmu/vmm.h>
#include <arch/regs.h>
#include <kernel/exec.h>
#include <kernel/panic.h>
#include <mm/mmap.h>
#include <mm/page.h>
#include <mm/page_alloc.h>
#include <mm/page_alloc_flags.h>
#include <string.h>

bool elf_validate(struct elf_file_header* header)
{
	kassert(header != NULL && "elf_validate: header is NULL");

	log_debug("Validating ELF header at %p", (void*)header);

	if (header->id[EI_MAG0] != ELFMAG0) {
		log_error("ELF Header EI_MAG0 incorrect.\n");
		return false;
	}
	if (header->id[EI_MAG1] != ELFMAG1) {
		log_error("ELF Header EI_MAG1 incorrect.\n");
		return false;
	}
	if (header->id[EI_MAG2] != ELFMAG2) {
		log_error("ELF Header EI_MAG2 incorrect.\n");
		return false;
	}
	if (header->id[EI_MAG3] != ELFMAG3) {
		log_error("ELF Header EI_MAG3 incorrect.\n");
		return false;
	}

	// TODO: Verify more like endian and such

	return true;
}

static int load_program_header(struct task* task, void* elf, struct elf_program_header* prog)
{
	u64* pml4 = (u64*)PHYS_TO_HHDM(task->cr3);
	log_debug("cr3: %lx, pml4: %p", task->cr3, (void*)pml4);
	size_t pages = CEIL_DIV(prog->size_in_memory, PAGE_SIZE);
	uptr paddr   = HHDM_TO_PHYS(get_free_pages(AF_KERNEL, pages));
	uptr vaddr   = (uptr)prog->virtual_address;

	// TODO: More flags???
	for (size_t i = 0; i < pages; i++) {
		int res = map_page(pml4, vaddr + i * PAGE_SIZE, paddr + i * PAGE_SIZE, PAGE_PRESENT | PAGE_WRITE);
		if (res) return -1;
		log_debug("Mapped vaddr: %lx, to paddr: %lx", vaddr + i * PAGE_SIZE, paddr + i * PAGE_SIZE);
	}

	// I am copying into kvaddr which is the same memory as vaddr in the other address space.
	void* kvaddr = (void*)PHYS_TO_HHDM(paddr);

	void* data = (void*)((uptr)elf + prog->offset);
	log_debug("Copying data at %p to vaddr %p, size: %zu", data, kvaddr, prog->size_in_file);
	memcpy(kvaddr, data, prog->size_in_file);

	return 0;
}

int execve(struct task* task, struct elf_file_header* header)
{
	kassert(header != NULL && "execve: header is NULL");

	if (!elf_validate(header)) {
		panic("execve: Invalid ELF header");
	}

	log_info("Loading ELF binary with entry point at 0x%lx", header->entry);

	if (header->type != ET_EXE) {
		log_error("Invalid elf type: %d", header->type);
		return -1;
	}

	log_debug("Valid type, reading program headers");

	struct elf_program_header* prog = (struct elf_program_header*)((uintptr_t)header + header->header_size);
	for (size_t i = 0; i < header->program_header_entry_count; i++) {
		log_debug(
			"ELF Program Header: type=0x%x, flags=0x%x, offset=0x%lx, virtual_address=0x%lx, size_in_file=0x%lx, size_in_memory=0x%lx, align=0x%lx",
			prog->type, prog->flags, prog->offset, prog->virtual_address, prog->size_in_file,
			prog->size_in_memory, prog->align);
		switch (prog->type) {
		case PT_LOAD:
			load_program_header(task, header, prog);
			break;
		}
		prog++;
	}

	task->regs->rip = header->entry; // Set the entry point
	task->entry	= (entry_func)header->entry;
	task->state	= READY;

	return 0;
}
