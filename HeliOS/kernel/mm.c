/**
 * Basic memory map reader and page allocator
 *
 * Based on https://anastas.io/osdev/memory/2016/08/08/page-frame-allocator.html
 */

#include <kernel/mm.h>
#include <kernel/multiboot.h>
#include <stddef.h>
#include <stdint.h>

multiboot_info_t* verified_mboot_hdr;
uint32_t mboot_reserved_start;
uint32_t mboot_reserved_end;
uint32_t next_free_frame;

void mmap_init(multiboot_info_t* mboot_hdr)
{
        verified_mboot_hdr = mboot_hdr;
        mboot_reserved_start = (uint32_t)mboot_hdr;
        mboot_reserved_end = (uint32_t)(mboot_hdr + sizeof(multiboot_info_t));
        next_free_frame = 1;
}

/**
 * A function to iterate through the multiboot memory map.
 * If `mode` is set to MMAP_GET_NUM, it will return the frame number for the
 * frame at address `request`.
 * If `mode` is set to MMAP_GET_ADDR, it will return the starting address for
 * the frame number `request`.
 */
uint32_t mmap_read(uint32_t request, uint8_t mode)
{
        // We're reserving frame number 0 for errors, so skip all requests for 0
        if (request == 0) return 0;

        // If the user specifies an invalid mode, also skip the request
        if (mode != MMAP_GET_NUM && mode != MMAP_GET_ADDR) return 0;

        // Increment through each entry in the multiboot memory map
        uintptr_t cur_mmap_addr = (uintptr_t)verified_mboot_hdr->mmap_addr;
        uintptr_t mmap_end_addr = cur_mmap_addr + verified_mboot_hdr->mmap_length;
        uint32_t cur_num = 0;
        while (cur_mmap_addr < mmap_end_addr) {
                // Get a pointer to the current entry
                multiboot_memory_map_t* current_entry = (multiboot_memory_map_t*)cur_mmap_addr;

                // Now let's split this entry into page sized chunks and increment our
                // internal frame number counter
                uint64_t i;
                uint64_t current_entry_end = current_entry->addr_low + current_entry->len_low;
                for (i = current_entry->addr_low; i + PAGE_SIZE < current_entry_end; i += PAGE_SIZE) {
                        if (mode == MMAP_GET_NUM && request >= i && request <= i + PAGE_SIZE) {
                                // If we're looking for a frame number from an address and we found it
                                // return the frame number
                                return cur_num + 1;
                        }

                        // If the requested chunk is in reserved space, skip it
                        if (current_entry->type == MULTIBOOT_MEMORY_RESERVED) {
                                if (mode == MMAP_GET_ADDR && cur_num == request) {
                                        // The address of a chunk in reserved space was requested
                                        // Increment the request until it is no longer reserved
                                        ++request;
                                }
                                // Skip to the next chunk until it's no longer reserved
                                ++cur_num;
                                continue;
                        } else if (mode == MMAP_GET_ADDR && cur_num == request) {
                                // If we're looking for a frame starting address and we found it
                                // return the starting address
                                return i;
                        }
                        ++cur_num;
                }

                // Increment by the size of the current entry to get to the next one
                cur_mmap_addr += current_entry->size + sizeof(uintptr_t);
        }
        // If no results are found, return 0
        return 0;
}

// TODO: Allow allocation of multiple frames (for liballoc)
/**
 * Allocate the next free frame and return it's frame number
 */
uint32_t allocate_frame()
{
        // Get the address for the next free frame
        uint32_t cur_addr = mmap_read(next_free_frame, MMAP_GET_ADDR);

        // Verify that the frame is not in the multiboot reserved area
        // If it is, increment the next free frame number and recursively call back.
        if (cur_addr >= mboot_reserved_start && cur_addr <= mboot_reserved_end) {
                ++next_free_frame;
                return allocate_frame();
        }

        // Call mmap_read again to get the frame number for our address
        uint32_t cur_num = mmap_read(cur_addr, MMAP_GET_NUM);

        // Update next_free_frame to the next unallocated frame number
        next_free_frame = cur_num + 1;

        // Finally, return the newly allocated frame num
        return cur_num;
}

void free_frame(uint32_t mmap_addr)
{
        // Get a pointer to the current entry
        multiboot_memory_map_t* entry = (multiboot_memory_map_t*)mmap_addr;
        entry->type = MULTIBOOT_MEMORY_AVAILABLE;
}

/**
 * @brief Allocates multiple page frames
 * @param frames Number of consequtive frames to allocate.
 * @return Address for beginning of frames.
 */
uint32_t allocate_frames(size_t frames)
{
        uint32_t cur_addr = mmap_read(next_free_frame, MMAP_GET_ADDR);
        return 0;
}

/**
 * Wrapper for liballoc allocation.
 * @param pages Number of pages to alloc
 * @return Pointer to the allocated pages
 */
void* liballoc_alloc(size_t pages)
{
        allocate_frames(pages);
        return NULL;
}
