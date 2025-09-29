/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include "drivers/device.h"
#include "kernel/semaphores.h"

// Not sure what I actually support, but oh well
enum fb_format {
	FB_FMT_XRGB8888 = 0, // 32bpp, little-endian, X R G B
};

/* Capabilities bitmask for feature discovery. */
enum fb_caps {
	FB_CAP_MMAP = 1u << 0,	     // supports mmap of VRAM
	FB_CAP_PAN = 1u << 1,	     // y/x panning or buffer index
	FB_CAP_VBLANK_IRQ = 1u << 2, // can wait for vsync/poll
	FB_CAP_SET_MODE = 1u << 3,   // can change resolution/format
	FB_CAP_FLUSH_RECT = 1u << 4, // needs/accepts explicit flush
};

struct fb_device {
	u32 width; // visible pixels
	u32 height;
	u32 pitch; // bytes per scanline
	u32 bpp;   // bits per pixel
	enum fb_format format;

	/* memory region */
	paddr_t vram_paddr;
	size_t vram_len;

	u32 caps; // capabilities bitmask (FB_CAP_*)

	semaphore_t sem;
	struct chrdev cdev;
	struct file_ops* fops;
};

void fb_init();

ssize_t fb_write(struct vfs_file* file,
		 const char* buffer,
		 size_t count,
		 off_t* offset);
