/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

// TODO: Use these

/** @brief An entry in the Page Global Directory (PGD), the top-level page table. */
typedef struct {
	unsigned long pgd;
} pgd_t;

/**
 * @brief An entry in the Page 4th-level Directory (P4D).
 * @note We do not support this since we only do 4 level paging.
 */
typedef struct {
	unsigned long p4d;
} p4d_t;

/** @brief An entry in the Page Upper Directory (PUD). */
typedef struct {
	unsigned long pud;
} pud_t;

/** @brief An entry in the Page Middle Directory (PMD). */
typedef struct {
	unsigned long pmd;
} pmd_t;

/** @brief A Page Table Entry (PTE) which points to a physical page frame. */
typedef struct {
	unsigned long pte;
} pte_t;
