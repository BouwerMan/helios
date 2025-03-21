#include <stdint.h>
#include <string.h>

/**
 * @brief Copies a block of memory from a source to a destination.
 *
 * This function copies `count` bytes from the memory area pointed to by `src`
 * to the memory area pointed to by `dest`. The memory areas must not overlap;
 * if they do, the behavior is undefined. Use `memmove()` if overlapping areas
 * are possible.
 *
 * @param   dest    Pointer to the destination buffer where the content is to be copied.
 * @param   src     Pointer to the source of data to be copied.
 * @param   count   Number of bytes to copy.
 *
 * @return A pointer to the destination buffer dest.
 */
void* memcpy(void* dest, const void* src, size_t count)
{
    for (size_t i = 0; i < count; i++) {
        ((uint8_t*)dest)[i] = ((uint8_t*)src)[i];
    }
    return dest;
}
