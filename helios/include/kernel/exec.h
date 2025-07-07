#pragma once

#include <kernel/helios.h>
#include <kernel/tasks/scheduler.h>

#define ELF_FLAG_WRITABLE 1

struct elf_file_header {
	char id[16];
	u16 type;
	u16 machine_type;
	u32 version;
	u64 entry;
	u64 program_header_offset;
	u64 section_header_offset;
	u32 flags;
	u16 header_size;
	u16 program_header_entry_size;
	u16 program_header_entry_count;
	u16 section_header_entry_size;
	u16 section_header_entry_count;
	u16 section_name_string_table_index;
} __attribute__((packed));

struct elf_program_header {
	u32 type;
	u32 flags;
	u64 offset;
	u64 virtual_address;
	u64 rsvd;
	u64 size_in_file;
	u64 size_in_memory;
	u64 align;
} __attribute__((packed));

enum elf_id {
	EI_MAG0	      = 0, // 0x7F
	EI_MAG1	      = 1, // 'E'
	EI_MAG2	      = 2, // 'L'
	EI_MAG3	      = 3, // 'F'
	EI_CLASS      = 4, // Architecture (32/64)
	EI_DATA	      = 5, // Byte Order
	EI_VERSION    = 6, // ELF Version
	EI_OSABI      = 7, // OS Specific
	EI_ABIVERSION = 8, // OS Specific
	EI_PAD	      = 9  // Padding
};

enum ELF_TYPES {
	ET_REL	= 1,
	ET_EXE	= 2,
	ET_SHR	= 3,
	ET_CORE = 4,
};

enum PROGRAM_TYPES {
	PT_NULL = 0,
	PT_LOAD = 1,
	PT_DYN	= 2,
	PT_INT	= 3,
};

#define ELFMAG0 0x7F // e_ident[EI_MAG0]
#define ELFMAG1 'E'  // e_ident[EI_MAG1]
#define ELFMAG2 'L'  // e_ident[EI_MAG2]
#define ELFMAG3 'F'  // e_ident[EI_MAG3]

#define ELFDATA2LSB (1) // Little Endian
#define ELFCLASS32  (1) // 32-bit Architecture

bool elf_validate(struct elf_file_header* header);
int execve(struct task* task, struct elf_file_header* header);
