#include "../arch/x86_64/ports.h"
#include <drivers/serial.h>

/**
 * @brief Initializes the serial port for communication.
 *
 * This function configures the serial port by setting the baud rate, enabling
 * interrupts, and configuring the data format. It also performs a loopback
 * test to verify the functionality of the serial port. If the test fails,
 * the function returns an error code.
 *
 * @return 0 if the serial port is initialized successfully, 1 if the loopback
 *         test fails.
 */
int init_serial(void)
{
    outb(PORT + 1, 0x00); // Disable all interrupts
    outb(PORT + 3, 0x80); // Enable DLAB (set baud rate divisor)
    outb(PORT + 0, 0x03); // Set divisor to 3 (lo byte) 38400 baud
    outb(PORT + 1, 0x00); //                  (hi byte)
    outb(PORT + 3, 0x03); // 8 bits, no parity, one stop bit
    outb(PORT + 2, 0xC7); // Enable FIFO, clear them, with 14-byte threshold
    outb(PORT + 4, 0x0B); // IRQs enabled, RTS/DSR set
    outb(PORT + 4, 0x1E); // Set in loopback mode, test the serial chip
    outb(PORT + 0, 0xAE); // Test serial chip (send byte 0xAE and check if serial returns same byte)

    // Check if serial is faulty (i.e: not same byte as sent)
    if (inb(PORT + 0) != 0xAE) {
        return 1;
    }

    // If serial is not faulty set it in normal operation mode
    // (not-loopback with IRQs enabled and OUT#1 and OUT#2 bits enabled)
    outb(PORT + 4, 0x0F);
    return 0;
}

static int is_transmit_empty(void) { return inb(PORT + 5) & 0x20; }

/**
 * @brief Writes a character to the serial port.
 *
 * This function waits until the serial port is ready to transmit data,
 * then sends the specified character.
 *
 * @param a The character to write to the serial port.
 */
void write_serial(char a)
{
    while (is_transmit_empty() == 0)
        ;
    outb(PORT, a);
}

/**
 * @brief Writes a null-terminated string to the serial port.
 *
 * This function iterates through each character in the provided string
 * and writes it to the serial port using `write_serial`.
 *
 * @param s The null-terminated string to write to the serial port.
 */
void write_serial_string(const char* s)
{
    while (*s)
        write_serial(*s++);
}
