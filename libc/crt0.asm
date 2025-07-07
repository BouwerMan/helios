[BITS 64]
global _start
extern main

; GDB BREAKPOINT
section .text
_start:
	; Intentional div by 0 to cause an exception
	xor rax, rax
	div rax
	hlt
	call main
	jmp $
