#pragma once
#include <stdint.h>

// Inline assembly utilities
static inline void halt() { asm volatile("hlt"); }

static inline void outb(uint16_t port, uint8_t val)
{
    asm volatile("outb %0, %1" : : "a"(val), "Nd"(port) : "memory");
    /* There's an outb %al, $imm8 encoding, for compile-time constant port numbers that fit in 8b.
     * (N constraint). Wider immediate constants would be truncated at assemble-time (e.g. "i"
     * constraint). The  outb  %al, %dx  encoding is the only option for all other cases. %1 expands
     * to %dx because  port  is a uint16_t.  %w1 could be used if we had the port number a wider C
     * type */
}

static inline uint8_t inb(uint16_t port)
{
    uint8_t ret;
    asm volatile("inb %1, %0" : "=a"(ret) : "Nd"(port) : "memory");
    return ret;
}

/**
 * Outputs the word <val> to the I/O-Port <port>
 *
 * @param port the port
 * @param val the value
 */
static inline void outword(uint16_t port, uint16_t val) { __asm__ volatile("out	%%ax,%%dx" : : "a"(val), "d"(port)); }

/**
 * Outputs the dword <val> to the I/O-Port <port>
 *
 * @param port the port
 * @param val the value
 */
static inline void outdword(uint16_t port, uint32_t val)
{
    __asm__ volatile("out	%%eax,%%dx" : : "a"(val), "d"(port));
}

/**
 * Reads a word from the I/O-Port <port>
 *
 * @param port the port
 * @return the value
 */
static inline uint16_t inw(uint16_t port)
{
    uint16_t res;
    __asm__ volatile("in	%%dx,%%ax" : "=a"(res) : "d"(port));
    return res;
}

/**
 * Reads a dword from the I/O-Port <port>
 *
 * @param port the port
 * @return the value
 */
static inline uint32_t indword(uint16_t port)
{
    uint32_t res;
    __asm__ volatile("in	%%dx,%%eax" : "=a"(res) : "d"(port));
    return res;
}

static inline void io_wait(void) { __asm__ volatile("outb %%al, $0x80" ::"a"(0)); }
