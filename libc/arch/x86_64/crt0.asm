; asmsyntax=nasm
[BITS 64]
extern main
extern exit

section .text
global _start
_start:
	; Set up end of stack frame linked list
	mov rbp, 0
	push rbp ; rip=0
	push rbp ; rbp=0

	push rdi ; argc
	push rsi ; argv
	
	; Prepare signals, memory allocation, stdio and such.
	; call initialize_standard_library

	call main

	; mov eax, edi
	; call exit

	jmp $
