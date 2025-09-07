/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef _ERRNO_H
#define _ERRNO_H
#pragma once

#include <helios/errno.h>

extern int* __errno_location(void);
#define errno (*__errno_location())

#endif /* _ERRNO_H */
