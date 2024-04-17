/* The bootloader will look at this image and start execution at the symbol
   designated as the entry point. */
ENTRY(_start)

/* Tell where the various sections of the object files will be put in the final
   kernel image. */
SECTIONS
{
    kernel_start_raw = .;
    . = 0x00100000;

    .multiboot : {
        *(.multiboot)
    }

    .note.gnu.build-id : {
        *(.note.gnu.build-id)
    }

    .boot : {
        *(.boot)
    }

    . += 0xC0000000;

    .text ALIGN(4K): AT(ADDR(.text) - 0xC0000000){
        *(.text)
    }

    .rodata ALIGN(4K): AT(ADDR(.rodata) - 0xC0000000){
        *(.rodata)
    }

    .data ALIGN(4K): AT(ADDR(.data) - 0xC0000000){
        *(.data)
    }

    .bss ALIGN(4K): AT(ADDR(.bss) - 0xC0000000){
        *(COMMON)
        *(.bss)
    }

    kernel_end_raw = .;

}
