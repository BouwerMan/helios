/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include <kernel/helios.h>
#include <kernel/tasks/scheduler.h>

static constexpr uptr DEFAULT_STACK_TOP = 0x7ffffffff000;

#define ELFMAG0 0x7F	   // e_ident[EI_MAG0]
#define ELFMAG1 'E'	   // e_ident[EI_MAG1]
#define ELFMAG2 'L'	   // e_ident[EI_MAG2]
#define ELFMAG3 'F'	   // e_ident[EI_MAG3]

#define ELFDATA2LSB (1)	   // Little Endian
#define ELFCLASS32  (1)	   // 32-bit Architecture

enum elf_id {
	EI_MAG0 = 0,	   // 0x7F
	EI_MAG1 = 1,	   // 'E'
	EI_MAG2 = 2,	   // 'L'
	EI_MAG3 = 3,	   // 'F'
	EI_CLASS = 4,	   // Architecture (32/64)
	EI_DATA = 5,	   // Byte Order
	EI_VERSION = 6,	   // ELF Version
	EI_OSABI = 7,	   // OS Specific
	EI_ABIVERSION = 8, // OS Specific
	EI_PAD = 9	   // Padding
};

enum ELF_TYPES {
	ET_REL = 1,
	ET_EXE = 2,
	ET_SHR = 3,
	ET_CORE = 4,
};

enum ELF_PROGRAM_TYPES {
	PT_NULL = 0,
	PT_LOAD = 1,
	PT_DYN = 2,
	PT_INT = 3,
};

enum ELF_PROGRAM_FLAGS {
	PF_EXEC = 1,
	PF_WRITE = 2,
	PF_READ = 4,
};

#define SHN_UNDEF (0x00)  // Undefined/Not Present
#define SHN_ABS	  0xFFF1  // Absolute symbol

enum ShT_Types {
	SHT_NULL = 0,	  // Null section
	SHT_PROGBITS = 1, // Program information
	SHT_SYMTAB = 2,	  // Symbol table
	SHT_STRTAB = 3,	  // String table
	SHT_RELA = 4,	  // Relocation (w/ addend)
	SHT_NOBITS = 8,	  // Not present in file
	SHT_REL = 9,	  // Relocation (no addend)
};

enum ShT_Attributes {
	SHF_WRITE = 0x01, // Writable section
	SHF_ALLOC = 0x02  // Exists in memory
};

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

struct exec_context {
	struct address_space* new_vas; // New address space
	void* entry_point;	       // Entry from ELF
	void* user_stack_top;	       // After argv/envp setup
	bool prepared;		       // Validation flag
	char name[MAX_TASK_NAME_LEN];
};

struct exec_context*
prepare_exec(const char* path, const char** argv, const char** envp);

int commit_exec(struct task* task, struct exec_context* ctx);

int __load_elf(struct exec_context* ctx, struct vfs_file* file);

void destroy_exec_context(struct exec_context* ctx);
