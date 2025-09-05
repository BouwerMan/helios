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
	mov rbp, rsp

	; Prepare signals, memory allocation, stdio and such.
	; call initialize_standard_library
	
	; Get argc from stack (it's now 16 bytes down due to pushes)
	mov rdi, [rsp + 16]  ; argc from stack
	lea rsi, [rsp + 24]  ; argv = rsp + 24
	
	; Calculate envp: rsp + (argc + 1) * 8
	mov rdx, rdi            ; rdx = argc
	inc rdx                 ; rdx = argc + 1
	shl rdx, 3              ; rdx = (argc + 1) * 8  
	add rdx, rsi            ; rdx = argv + (argc + 1) * 8 = envp

	call main

	mov edi, eax
	call exit

	jmp $
