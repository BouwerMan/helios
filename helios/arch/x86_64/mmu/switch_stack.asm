bits 64

global __switch_to_new_stack
default rel
section .text

; void switch_to_new_stack(void* new_stack_top, void (*entrypoint)(void));
; Arguments:
;   rdi = new_stack_top (stack pointer)
;   rsi = entrypoint function
__switch_to_new_stack:
	mov rsp, rdi
	jmp rsi
	

