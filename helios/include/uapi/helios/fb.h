/**
 * Copyright (C) 2026 Dylan Parks
 *
 * This file is part of HeliOS
 *
 * HeliOS is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#ifndef _UAPI_HELIOS_FB_H
#define _UAPI_HELIOS_FB_H 1

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum fb_ioctl_request {
	FBIOGET_SCREENINFO = 0x4600,
	FBIOSET_MODE = 0x4601,
};

struct fb_screeninfo {
	uint32_t width;	   /* visible pixels */
	uint32_t height;
	uint32_t pitch;	   /* bytes per scanline */
	uint32_t bpp;	   /* bits per pixel */
	uint32_t format;   /* enum fb_format */
	uint32_t caps;	   /* enum fb_caps bitmask */
	uint64_t vram_len; /* total mappable bytes */
};

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

enum fb_mode {
	FB_MODE_TEXT = 0,
	FB_MODE_GRAPHICS = 1,
};

static inline const char* __get_fb_ioctl_request_name(unsigned long req)
{
	enum fb_ioctl_request _req = (enum fb_ioctl_request)req;
	switch (_req) {
	case FBIOGET_SCREENINFO: return "FBIOGET_SCREENINFO";
	case FBIOSET_MODE:	 return "FBIOSET_MODE";
	}
	return "UNKNOWN";
}

static inline const char* __get_fb_format_name(uint32_t fmt)
{
	enum fb_format _fmt = (enum fb_format)fmt;

	switch (_fmt) {
	case FB_FMT_XRGB8888: return "XRGB8888";
	}
	return "UNKNOWN";
}

static inline const char* __get_fb_mode_name(uint32_t mode)
{
	enum fb_mode _mode = (enum fb_mode)mode;

	switch (_mode) {
	case FB_MODE_TEXT:     return "FB_MODE_TEXT";
	case FB_MODE_GRAPHICS: return "FB_MODE_GRAPHICS";
	}
	return "UNKNOWN";
}

#ifdef __cplusplus
}
#endif

#endif /* _UAPI_HELIOS_FB_H */
