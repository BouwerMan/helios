; asmsyntax=nasm
[BITS 64]
extern main

section .text
global _start
_start:
	call main
	jmp $
