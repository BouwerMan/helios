/* Tell the linker that we want an x86_64 ELF64 output file */
OUTPUT_FORMAT(elf64-x86-64)

/* We want the symbol kmain to be our entry point */
ENTRY(__arch_entry)

/* Define the program headers we want so the bootloader gives us the right */
/* MMU permissions; this also allows us to exert more control over the linking */
/* process. */
PHDRS
{
	limine_requests PT_LOAD;
	text PT_LOAD;
	rodata PT_LOAD;
	data PT_LOAD;
}

SECTIONS
{
	/* We want to be placed in the topmost 2GiB of the address space, for optimisations */
	/* and because that is what the Limine spec mandates. */
	/* Any address in this region will do, but often 0xffffffff80000000 is chosen as */
	/* that is the beginning of the region. */
	. = 0xffffffff80000000;

	__kernel_start = .;     /* start of kernel image */

	/* Define a section to contain the Limine requests and assign it to its own PHDR */
	.limine_requests : {
		KEEP(*(.limine_requests_start))
		KEEP(*(.limine_requests))
		KEEP(*(.limine_requests_end))
	} :limine_requests

	/* --- .text family --------------------------------------------------- */

	/* Page-align before code */
	. = ALIGN(CONSTANT(MAXPAGESIZE));

	/* Hot / core text first for I-cache locality */
	.text : {
		__text_start = .;
		/* startup/hot go first */
		*(.text.startup .text.startup.*)
		*(.text.hot     .text.hot.*)

		/* plain .text */
		*(.text)

		__text_end = .;
	} :text

	/* Keep cold paths out-of-line; align to a page for clean separation */
	. = ALIGN(CONSTANT(MAXPAGESIZE));
	.text.cold : {
		__text_cold_start = .;
		/* compilers commonly use these names for unlikely paths */
		*(.text.unlikely .text.unlikely.*)
		*(.text.cold     .text.cold.*)
		__text_cold_end = .;
	} :text

	/* Any leftover per-function text (if you ever enable -ffunction-sections) */
	.text.rest : {
		__text_rest_start = .;
		*(.text.*)
		__text_rest_end = .;
	} :text

	/* Move to the next memory page for .rodata */
	. = ALIGN(CONSTANT(MAXPAGESIZE));

	.rodata : {
		*(.rodata .rodata.*)
	} :rodata

	/* Move to the next memory page for .data */
	. = ALIGN(CONSTANT(MAXPAGESIZE));

	.data : {
		*(.data .data.*)
	} :data

	/* NOTE: .bss needs to be the last thing mapped to :data, otherwise lots of */
	/* unnecessary zeros will be written to the binary. */
	/* If you need, for example, .init_array and .fini_array, those should be placed */
	/* above this. */
	.bss : {
		*(.bss .bss.*)
		*(COMMON)
	} :data

	/* Discard .note.* and .eh_frame* since they may cause issues on some hosts. */
	/DISCARD/ : {
		*(.eh_frame*)
		*(.note .note.*)
	}

	__kernel_end = .;       /* end of kernel image */
}

