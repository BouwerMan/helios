/**
 * Copyright (C) 2025  Dylan Parks
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

#ifndef _UAPI_HELIOS_FS_H
#define _UAPI_HELIOS_FS_H 1

#ifdef __cplusplus
extern "C" {
#endif

#define F_OK 0 // File exists
#define R_OK 1 // Readable
#define W_OK 2 // Writable
#define X_OK 4 // Executable

#ifdef __cplusplus
}
#endif

#endif /* _UAPI_HELIOS_FS_H */
