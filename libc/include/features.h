/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef _FEATURES_H
#define _FEATURES_H 1
#pragma once

#if (__STDC_VERSION__ >= 202311L)
#define __USE_C23
#endif

/**
 * Means that the return value is solely a function of the arguments,
 * and if any of the arguments are pointers, then the pointers must not be dereferenced.
 */
#define __attribute_const __attribute__((const))

/**
* Means that the function has no side effects and the value returned
* depends on the arguments and the state of global variables.
*/
#define __pure __attribute__((pure))

#define __noreturn __attribute__((noreturn))

#define __no_throw __attribute__((nothrow))

#endif /* _FEATURES_H */
