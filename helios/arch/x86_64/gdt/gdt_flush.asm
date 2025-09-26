; asmsyntax=nasm
; @file arch/x86_64/gdt/gdt_flush.asm
;
; Copyright (C) 2025  Dylan Parks
;
; This file is part of HeliOS
;
; HeliOS is free software: you can redistribute it and/or modify
; it under the terms of the GNU General Public License as published by
; the Free Software Foundation, either version 3 of the License, or
; (at your option) any later version.
;
; This program is distributed in the hope that it will be useful,
; but WITHOUT ANY WARRANTY; without even the implied warranty of
; MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
; GNU General Public License for more details.
;
; You should have received a copy of the GNU General Public License
; along with this program.  If not, see <https://www.gnu.org/licenses/>.

[BITS 64]

global __gdt_flush
__gdt_flush:
	lgdt [rdi]        ; Load the new GDT pointer
	
	mov ax, 0x10      ; 0x10 is the offset in the GDT to our data segment
	mov ds, ax        ; Load all data segment selectors
	mov es, ax
	mov fs, ax
	mov gs, ax
	mov ss, ax
	
	pop rdi
	mov rax, 0x08
	push rax
	push rdi
	retfq

global __tss_flush
__tss_flush:
	ltr di
	ret
