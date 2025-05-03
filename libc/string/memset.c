#include <stdint.h>
#include <string.h>

/**
 * @brief Fills a block of memory with a specified byte value.
 *
 * Sets each of the `size` bytes in the memory area pointed to by `bufptr`
 * to the specified byte value `value` (converted to an unsigned char).
 *
 * This is commonly used to initialize or reset memory, such as clearing a
 * buffer or setting a memory region to zero.
 *
 * @param   bufptr  Pointer to the memory area to fill.
 * @param   value   Byte value to set, passed as an int but converted to unsigned char.
 * @param   size    Number of bytes to set.
 *
 * @return A pointer to the memory area `bufptr`.
 */
void* memset(void* s, int c, size_t n)
{
    uint8_t* p = (uint8_t*)s;

    for (size_t i = 0; i < n; i++) {
        p[i] = (uint8_t)c;
    }

    return s;
}
