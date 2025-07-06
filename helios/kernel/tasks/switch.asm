; asmsyntax=nasm
; @file kernel/tasks/switch.asm
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

extern interrupt_return

%define CR3_OFF 8

; __switch_to: Switches execution context to the next task.
; Parameters:
;   rdi - Pointer to the task structure of the next task.
global __switch_to
__switch_to:
	; Load new cr3 if needed
	mov rax, [rdi + CR3_OFF]
	mov rcx, cr3
	cmp rax, rcx
	je .no_cr3_switch
	mov cr3, rax

    .no_cr3_switch:
	mov rdi, [rdi]
	jmp interrupt_return
