/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef _FCNTL_H
#define _FCNTL_H
#pragma once

#include <features.h>
#include <helios/fcntl.h>

int open(const char* __path, int __oflag, ...) __nothrow;

#endif /* _FCNTL_H */
