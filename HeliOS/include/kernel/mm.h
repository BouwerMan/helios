#pragma once

#include <kernel/multiboot.h>
#include <stdint.h>

#define MMAP_GET_NUM 0
#define MMAP_GET_ADDR 1
#define PAGE_SIZE 4096

void mmap_init(multiboot_info_t* mboot_hdr);
uint32_t mmap_read(uint32_t request, uint8_t mode);
uint32_t allocate_frame();
