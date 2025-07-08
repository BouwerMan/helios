; asmsyntax=nasm
[BITS 64]

section .text

global __syscall0
__syscall0:
	mov rax, rdi
	int 0x80
	ret

global __syscall1
__syscall1:
	mov rax, rdi
	mov rdi, rsi
	int 0x80
	ret

global __syscall2
__syscall2:
	mov rax, rdi
	mov rdi, rsi
	mov rsi, rdx
	int 0x80
	ret

global __syscall3
__syscall3:
	mov rax, rdi
	mov rdi, rsi
	mov rsi, rdx
	mov rdx, rcx
	int 0x80
	ret
