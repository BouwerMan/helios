/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef _SYS_IOCTL_H
#define _SYS_IOCTL_H 1
#pragma once

#include <features.h>

int ioctl(int fd, unsigned long request, void* arg);

#endif /* _SYS_IOCTL_H */
