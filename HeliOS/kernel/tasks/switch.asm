[BITS 64]

%define RSP_OFF 176

global __switch_to
__switch_to:
	mov	rsp, rdi	; Load stack pointer from registers
	pop     rbx                      ; reload the original data segment descriptor
	mov     ds, bx
	mov     es, bx
	mov     fs, bx
	mov     gs, bx
	
	; movdqa  xmm15, [rsp + 15 * 16]
	; movdqa  xmm14, [rsp + 14 * 16]
	; movdqa  xmm13, [rsp + 13 * 16]
	; movdqa  xmm12, [rsp + 12 * 16]
	; movdqa  xmm12, [rsp + 11 * 16]
	; movdqa  xmm10, [rsp + 10 * 16]
	; movdqa  xmm9, [rsp + 9 * 16]
	; movdqa  xmm8, [rsp + 8 * 16]
	; movdqa  xmm7, [rsp + 7 * 16]
	; movdqa  xmm6, [rsp + 6 * 16]
	; movdqa  xmm5, [rsp + 5 * 16]
	; movdqa  xmm4, [rsp + 4 * 16]
	; movdqa  xmm3, [rsp + 3 * 16]
	; movdqa  xmm2, [rsp + 2 * 16]
	; movdqa  xmm1, [rsp + 1 * 16]
	; movdqa  xmm0, [rsp + 0 * 16]
	; add     rsp, 256
	
	pop     rdi
	pop     rsi
	pop     rbp
	add     rsp, 8 ; skip rsp
	pop     rbx
	pop     rdx
	pop     rcx
	pop     rax
	
	pop     r8
	pop     r9
	pop     r10
	pop     r11
	pop     r12
	pop     r13
	pop     r14
	pop     r15
	popfq

	iretq
