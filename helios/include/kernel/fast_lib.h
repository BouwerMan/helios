/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

// This file contains fast standard library functions that
// rely on arch-specific optimizations.

#if defined(__x86_64__)
#include <arch/x86_64/fast_lib.h>
#endif
