/**
 *   HeliOS is an open source hobby OS development project.
 *   Copyright (C) 2024  Dylan Parks
 *
 *   This program is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, either version 3 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *    along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

// MASTER TODO
// TODO: clean up inports, such as size_t coming from <stddef>;
// TODO: Pretty sure PMM will perform weird things when reaching max memory
// TODO: Project restructuring (drivers, kernel, lib, etc.)
// TODO: Standardize return values
#include "../arch/i386/vga.h"
#include <drivers/ata/controller.h>
#include <drivers/ata/device.h>
#include <drivers/fs/fat.h>
#include <drivers/fs/vfs.h>
#include <drivers/pci/pci.h>
#include <kernel/cpu.h>
#include <kernel/gdt.h>
#include <kernel/interrupts.h>
#include <kernel/keyboard.h>
#include <kernel/liballoc.h>
#include <kernel/memory.h>
#include <kernel/multiboot.h>
#include <kernel/sys.h>
#include <kernel/timer.h>
#include <kernel/tty.h>
#include <stdio.h>
#include <string.h>

#ifdef KERNEL_DEBUG
#define DEBUG_OUT(m) (puts(m))
#else
#define DEBUG_OUT(m) ((void)m)
#endif

extern uint32_t kernel_start_raw;
extern uint32_t kernel_end_raw;
static uint32_t kernel_start;
static uint32_t kernel_end;

static void test_passed_output(const char* message)
{
    tty_setcolor(VGA_COLOR_GREEN);
    printf("\t%s\n", message);
    tty_setcolor(VGA_COLOR_LIGHT_GREY);
}

static void test_failed_output(const char* message, uint32_t err_code)
{
    tty_setcolor(VGA_COLOR_RED);
    printf("\t%s0x%X\n", message, err_code);
    tty_setcolor(VGA_COLOR_LIGHT_GREY);
}

void kernel_early(multiboot_info_t* mbd, uint32_t magic)
{
    /* Initialize terminal interface */
    tty_initialize();

    /* Make sure the magic number matches for memory mapping*/
    if (magic != MULTIBOOT_BOOTLOADER_MAGIC) {
        panic("invalid magic number!");
    }

    /* Check bit 6 to see if we have a valid memory map */
    if (!(mbd->flags >> 6 & 0x1)) {
        panic("invalid memory map given by GRUB bootloader");
    }
#ifdef MEM_MAP_DUMP
    /* Loop through the memory map and display the values */
    int i;
    uint32_t length = 0;
    for (i = 0; i < mbd->mmap_length; i += sizeof(multiboot_memory_map_t)) {
        multiboot_memory_map_t* mmmt
            = (multiboot_memory_map_t*)(mbd->mmap_addr + i);

        printf("Start Addr: %x | Length: %x | Size: %x | Type: %d\n",
               mmmt->addr_low, mmmt->len_low, mmmt->size, mmmt->type);

        length += mmmt->len_low;
        if (mmmt->type == MULTIBOOT_MEMORY_AVAILABLE) {
            /*
             * Do something with this memory block!
             * BE WARNED that some of memory shown as availiable is actually
             * actively being used by the kernel! You'll need to take that
             * into account before writing to memory!
             */
        }
    }
    printf("Memory Length: %dMB\n", length / 1024 / 1024);
#endif

    tty_enable_cursor(0, 0);
    // tty_disable_cursor();

    puts("Initializing GDT");
    gdt_init();

    puts("Initializing IDT");
    idt_init();

    puts("Initializing ISRs");
    isr_init();

    puts("Initializing IRQs");
    irq_init();

    // Have to do some maintenance to make these sane numbers
    kernel_start = (uint32_t)(&kernel_start_raw);
    kernel_end = (uint32_t)(&kernel_end_raw) - KERNEL_OFFSET;

    uint32_t phys_alloc_start = (kernel_end + 0x1000) & 0xFFFFF000;
#ifdef KERNEL_DEBUG
    printf("KERNEL START: 0x%X, KERNEL END: 0x%X\n", kernel_start, kernel_end);
    printf("MEM LOW: 0x%X, MEM HIGH: 0x%X, PHYS START: 0x%X\n",
           mbd->mem_lower * 1024, mbd->mem_upper * 1024, phys_alloc_start);
#endif
    init_memory(mbd->mem_upper * 1024, phys_alloc_start);

    puts("Initializing Timer");
    timer_init();

    puts("Initializing Keyboard");
    keyboard_init();
}

void kernel_main(void)
{
    printf("Welcome to %s. Version: %s\n", KERNEL_NAME, KERNEL_VERSION);
    printf("Detected CPU: ");
    cpu_print_model();

    // Testing that interrupts are active by waiting for the timer to tick
    puts("Testing Interrupts:");
    timer_poll();
    test_passed_output("Interrupts Passed");

    // Physical memory manager testing
    uint8_t res = test_pmm();
    if (!res) {
        test_passed_output("PMM passed");
    } else {
        test_failed_output("PMM FAILED WITH ERROR CODE: ", res);
        panic("\tPMM FAILURE");
    }

#ifdef KMALLOC_TESTING
    puts("Testing liballoc:");
    int* test = (int*)kmalloc(sizeof(int));
    *test = 6435;
    printf("\tkmalloc returned address: 0x%X, set value to: %d\n", test, *test);
    kfree(test);
    int* test2 = (int*)kmalloc(sizeof(int));
    *test2 = 6435;
    printf("\tkmalloc returned address: 0x%X, set value to: %d\n", test2,
           *test2);
    int* test3 = (int*)kmalloc(sizeof(int));
    *test3 = 2421;
    printf("\tkmalloc returned address: 0x%X, set value to: %d\n", test3,
           *test3);
    kfree(test2);
    kfree(test3);
#endif

#define FS_TESTING
#ifdef FS_TESTING
    list_devices();
    ctrl_init();
    puts("VFS Testing");
    vfs_init(64);

    sATADevice* fat_device = ctrl_get_device(3);
    mount("/", fat_device, &fat_device->part_table[0], FAT16);

    puts("FAT and VFS testing");
    struct vfs_superblock* fat_sb = vfs_get_sb(0);

    struct vfs_dentry* dentry = vfs_resolve_path("/dir/test2.txt");
    if (dentry->inode) {
        dprintf("Found inode? 0x%X, Inode name: %s, File size: %d, Init "
                "cluster: %d\n",
                (void*)dentry, dentry->name, dentry->inode->f_size,
                ((struct fat_inode_info*)dentry->inode->fs_data)->init_cluster);
    } else {
        puts("Could not find inode :/");
    }
    struct vfs_file f = { 0 };
    int res2 = vfs_open("/dir/test2.txt", &f);
    if (res2 < 0) {
        puts("oh no");
    } else {
        printf("%s\n", f.read_ptr);
    }
    puts("open 2");
    struct vfs_file f2 = { 0 };
    res2 = vfs_open("/test2.txt", &f2);
    if (res2 < 0) {
        puts("oh no");
    } else {
        printf("f_size: %d\n", f2.file_size);
        // printf("%s\n", f2.read_ptr);
    }
    dprintf("test %d\n", 14);
#endif

#ifdef PRINTF_TESTING
    tty_writestring("Printf testing:\n");
    putchar('c');
    printf("test old\n");
    printf("test new\n");
    printf("String: %s\n", "test string");
    printf("Char: %c\n", 't');
    printf("Hex: 0x%x 0x%X\n", 0x14AF, 0x410BC);
    printf("pos dec: %d\n", 5611);
    printf("neg dec: %d\n", -468);
    printf("unsigned int: %d\n", 4184);
    printf("oct: %o\n", 4184);
#endif // PRINTF_TESTING

    // asm volatile("1: jmp 1b");
    puts("End breakpoint");

    // NOTE: I removed this for loop since that should reduce idle cpu usage
    // (boot.asm calls hlt)
    //
    // Stopping us from exiting kernel
    // for (;;)
    //     ;
}
