; asmsyntax=nasm
[BITS 64]

section .text
global writec
writec:
	mov rax, 1
	mov rsi, rdi
	mov rdx, 1
	mov rdi, 1
	int 0x80
	ret
