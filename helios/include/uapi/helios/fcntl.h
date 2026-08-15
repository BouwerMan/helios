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

#ifndef _UAPI_HELIOS_FCNTL_H
#define _UAPI_HELIOS_FCNTL_H 1

#ifdef __cplusplus
extern "C" {
#endif

#define O_RDONLY  0x0000   ///< Open for reading only
#define O_WRONLY  0x0001   ///< Open for writing only
#define O_RDWR	  0x0002   ///< Open for reading and writing
#define O_ACCMODE 0x0003   ///< Mask for access mode (internal use)

#define O_APPEND 0x0004	   ///< Writes append to the end of file
#define O_CREAT	 0x0008	   ///< Create file if it does not exist
#define O_TRUNC	 0x0010	   ///< Truncate file to zero length if it exists
#define O_EXCL	 0x0020	   ///< Error if O_CREAT and file exists

#define O_DIRECTORY 0x0040 ///< Fail if the path is not a directory
#define O_NOFOLLOW  0x0080 ///< Do not follow symlinks (when you support them)

#define O_CLOEXEC 0x0100   ///< Set close-on-exec (if you do exec)

#ifdef __cplusplus
}
#endif

#endif /* _UAPI_HELIOS_FCNTL_H */
