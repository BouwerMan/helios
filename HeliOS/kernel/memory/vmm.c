#include <kernel/memory/pmm.h>
#include <kernel/memory/vmm.h>
#include <kernel/sys.h>
#include <string.h>
#include <util/log.h>

/**
 *How does changing the paging structure mess with the pmm (check pmm.c and pmm.h)? Currently I translated the bitmap to
 */

uint64_t* pml4 = NULL;

void vmm_init()
{
	pml4 = PHYS_TO_VIRT(pmm_alloc_page());
	if (pml4 == NULL) {
		log_error("VMM Initialization failed. PMM didn't return a "
			  "valid page");
		panic("VMM Initialization failed.");
	}
	memset(pml4, 0, PAGE_SIZE); // Clear page tables

	// Recursive mapping
	pml4[510] = VIRT_TO_PHYS(pml4) | PAGE_PRESENT | PAGE_WRITE;

	// Identity map first 64 MiB
	for (uint64_t vaddr = 0; vaddr < 0x4000000; vaddr += PAGE_SIZE) {
		vmm_map((void*)vaddr, (void*)vaddr, PAGE_PRESENT | PAGE_WRITE);
	}
}

static inline void invalidate(void* vaddr)
{
	__asm__ volatile("invlpg (%0)" ::"r"(vaddr) : "memory");
}

#define VADDR_PML4_INDEX(vaddr) (((uint64_t)(vaddr) >> 39) & 0x1FF)
#define VADDR_PDPT_INDEX(vaddr) (((uint64_t)(vaddr) >> 30) & 0x1FF)
#define VADDR_PD_INDEX(vaddr)	(((uint64_t)(vaddr) >> 21) & 0x1FF)
#define VADDR_PT_INDEX(vaddr)	(((uint64_t)(vaddr) >> 12) & 0x1FF)

/**
 * @brief Maps a virtual address to a physical address in the page tables.
 *
 * This function ensures that all required page table levels (PML4, PDPT, PD, PT)
 * are present and properly initialized. If a level is missing, it allocates a new
 * page for it and initializes it to zero. Finally, it maps the given virtual address
 * to the specified physical address with the provided flags.
 *
 * @param virt_addr The virtual address to map.
 * @param phys_addr The physical address to map to.
 * @param flags The flags to set for the mapping (e.g., PAGE_PRESENT, PAGE_WRITE).
 */
void vmm_map(void* virt_addr, void* phys_addr, uint64_t flags)
{
	flags |= PAGE_PRESENT | PAGE_WRITE;
	uint64_t pml4_i = VADDR_PML4_INDEX(virt_addr);
	if ((pml4[pml4_i] & PAGE_PRESENT) == 0) {
		void* new_page = pmm_alloc_page();
		memset(new_page, 0, PAGE_SIZE);
		pml4[pml4_i] = VIRT_TO_PHYS((uint64_t)new_page) | flags;
	}

	// Extract the physical address and convert it to a usable virtual pointer
	uint64_t* pdpt = (uint64_t*)PHYS_TO_VIRT(pml4[pml4_i] &
						 ~FLAGS_MASK); // Mask off flags
	uint64_t pdpt_i = VADDR_PDPT_INDEX(virt_addr);
	if ((pdpt[pdpt_i] & PAGE_PRESENT) == 0) {
		void* new_page = pmm_alloc_page();
		memset(new_page, 0, PAGE_SIZE);
		pdpt[pdpt_i] = VIRT_TO_PHYS((uint64_t)new_page) | flags;
	}

	uint64_t* pd = (uint64_t*)PHYS_TO_VIRT(pdpt[pdpt_i] &
					       ~FLAGS_MASK); // Mask off flags
	uint64_t pd_i = VADDR_PD_INDEX(virt_addr);
	if ((pd[pd_i] & PAGE_PRESENT) == 0) {
		void* new_page = pmm_alloc_page();
		memset(new_page, 0, PAGE_SIZE);
		pd[pd_i] = VIRT_TO_PHYS((uint64_t)new_page) | flags;
	}

	// Mask off flags
	uint64_t* pt = (uint64_t*)PHYS_TO_VIRT(pd[pd_i] & ~FLAGS_MASK);
	uint64_t pt_i = VADDR_PT_INDEX(virt_addr);
	if ((pt[pt_i] & PAGE_PRESENT) == 0) {
		pt[pt_i] = (uint64_t)phys_addr | flags;
	} else {
		log_warn(
			"Tried to map existing entry, virt_addr: 0x%lx, phys_addr: 0x%lx",
			(uint64_t)virt_addr, (uint64_t)phys_addr);
		invalidate(virt_addr);
		pt[pt_i] = (uint64_t)phys_addr | flags;
	}
}

void* vmm_translate(void* virt_addr)
{
	uint64_t va = (uint64_t)virt_addr;

	uint64_t pml4_i = VADDR_PML4_INDEX(va);
	if (!(pml4[pml4_i] & PAGE_PRESENT)) return NULL;

	uint64_t* pdpt =
		(uint64_t*)PHYS_TO_VIRT(pml4[pml4_i] & PAGE_FRAME_MASK);
	uint64_t pdpt_i = VADDR_PDPT_INDEX(va);
	if (!(pdpt[pdpt_i] & PAGE_PRESENT)) return NULL;

	uint64_t* pd = (uint64_t*)PHYS_TO_VIRT(pdpt[pdpt_i] & PAGE_FRAME_MASK);
	uint64_t pd_i = VADDR_PD_INDEX(va);
	if (!(pd[pd_i] & PAGE_PRESENT)) return NULL;

	uint64_t* pt = (uint64_t*)PHYS_TO_VIRT(pd[pd_i] & PAGE_FRAME_MASK);
	uint64_t pt_i = VADDR_PT_INDEX(va);
	if (!(pt[pt_i] & PAGE_PRESENT)) return NULL;

	uint64_t frame = pt[pt_i] & PAGE_FRAME_MASK;
	uint64_t offset = va & 0xFFF;

	return (void*)(frame + offset);
}

void vmm_unmap(void* virt_addr, bool free_phys)
{
	// TODO: Detect and free empty tables
	uint64_t va = (uint64_t)virt_addr;

	uint64_t pml4_i = VADDR_PML4_INDEX(va);
	if (!(pml4[pml4_i] & PAGE_PRESENT)) goto not_present;

	uint64_t* pdpt =
		(uint64_t*)PHYS_TO_VIRT(pml4[pml4_i] & PAGE_FRAME_MASK);
	uint64_t pdpt_i = VADDR_PDPT_INDEX(va);
	if (!(pdpt[pdpt_i] & PAGE_PRESENT)) goto not_present;

	uint64_t* pd = (uint64_t*)PHYS_TO_VIRT(pdpt[pdpt_i] & PAGE_FRAME_MASK);
	uint64_t pd_i = VADDR_PD_INDEX(va);
	if (!(pd[pd_i] & PAGE_PRESENT)) goto not_present;

	uint64_t* pt = (uint64_t*)PHYS_TO_VIRT(pd[pd_i] & PAGE_FRAME_MASK);
	uint64_t pt_i = VADDR_PT_INDEX(va);
	if (!(pt[pt_i] & PAGE_PRESENT)) goto not_present;
	if (free_phys) {
		uint64_t phys_addr = pt[pt_i] & PAGE_FRAME_MASK;
		pmm_free_page((void*)phys_addr);
	}
	pt[pt_i] = 0;

	invalidate(virt_addr);
	return;

not_present:
	log_error(
		"Couldn't traverse tables for 0x%lx, something in the chain was already marked not present",
		(uint64_t)virt_addr);
}

// TODO: Actually use this
void vmm_test()
{
	// Step 1: Allocate a physical page
	void* test_phys = pmm_alloc_page();
	if (!test_phys) {
		log_error("Failed to allocate test physical page.");
		return;
	}

	// Step 2: Choose a test virtual address (must be 4 KiB aligned)
	void* test_virt = (void*)0xFFFFFFFFC0000000;

	// Step 3: Map the page
	vmm_map(test_virt, test_phys, PAGE_PRESENT | PAGE_WRITE);

	// Step 4: Write a pattern to the virtual address
	uint64_t* vptr = (uint64_t*)test_virt;
	*vptr = 0xCAFEBABEDEADBEEF;

	// Step 5: Translate the virtual address back to physical
	void* resolved_phys = vmm_translate(test_virt);
	if (resolved_phys != test_phys) {
		log_error("vmm_translate() failed! Expected %p but got %p\n",
			  test_phys, resolved_phys);
		return;
	}

	// Step 6: Verify the physical memory contents (via PHYS_TO_VIRT)
	uint64_t* pptr = (uint64_t*)PHYS_TO_VIRT((uintptr_t)test_phys);
	if (*pptr != 0xCAFEBABEDEADBEEF) {
		log_error("Memory content mismatch at physical address!\n");
		return;
	}

	log_info(
		"VMM test passed: mapping, translation, and memory match successful.\n");
}
