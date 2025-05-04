[BITS 64]

__switch_to:
	cli
	push r15
	push r14
	push r13
	push r12
	push r11
	push r10
	push r9
	push r8

	push rax
	push rcx
	push rdx
	push rbx
	push rsp
	push rbp
	push rsi
	push rdi
