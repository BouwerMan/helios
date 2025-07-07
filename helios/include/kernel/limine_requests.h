/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include <limine.h>

#ifdef LIMINE_REQUESTS_DEFINE
#define LIMINE_EXTERN
#else
#define LIMINE_EXTERN extern
#endif

LIMINE_EXTERN volatile uint64_t limine_base_revision[3];

LIMINE_EXTERN volatile struct limine_framebuffer_request framebuffer_request;

LIMINE_EXTERN volatile struct limine_memmap_request memmap_request;

LIMINE_EXTERN volatile struct limine_hhdm_request hhdm_request;

LIMINE_EXTERN volatile struct limine_executable_address_request exe_addr_req;

LIMINE_EXTERN volatile struct limine_module_request mod_request;
