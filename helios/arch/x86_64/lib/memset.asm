; asmsyntax=nasm
[bits 64]

section .text

;
; __arch_memset - Set a block of memory to a byte value.
; @dst: Destination buffer.
; @c:   Byte value to store (low 8 bits used).
; @n:   Number of bytes to set.
; Return: @dst in %rax.
;
; No barriers implied. SysV x86-64 ABI: args in %rdi/%rsi/%rdx.
; Requires DF=0 on entry. Alignment is not required.
global __memset
global __arch_memset
__memset:
__arch_memset:
	mov r9, rdi
	mov al, sil
	mov rcx, rdx
	rep stosb
	mov rax, r9
	ret

;
;  __arch_memset16 - Fill 16-bit elements with a value.
;  @dst: Destination buffer (u16 *).
;  @v:   16-bit value to store.
;  @n:   Number of 16-bit elements to set.
;  Return: @dst in %rax.
;
;  Stores @n copies of @v to @dst. No barriers implied. SysV x86-64 ABI.
;  Requires DF=0 on entry. Alignment is not required.
;
global __memset16
global __arch_memset16
__memset16:
__arch_memset16:
	mov r9, rdi
	mov ax, si
	mov rcx, rdx
	rep stosw
	mov rax, r9
	ret

;
; __arch_memset32 - Fill 32-bit elements with a value.
; @dst: Destination buffer (u32 *).
; @v:   32-bit value to store.
; @n:   Number of 32-bit elements to set.
; Return: @dst in %rax.
;
; Stores @n copies of @v to @dst. No barriers implied. SysV x86-64 ABI.
; Requires DF=0 on entry. Alignment is not required.
;
global __memset32
global __arch_memset32
__memset32:
__arch_memset32:
	mov r9, rdi
	mov eax, esi
	mov rcx, rdx
	rep stosd
	mov rax, r9
	ret

;
;  __arch_memset64 - Fill 64-bit elements with a value.
;  @dst: Destination buffer (u64 *).
;  @v:   64-bit value to store.
;  @n:   Number of 64-bit elements to set.
;  Return: @dst in %rax.
;
;  Stores @n copies of @v to @dst. No barriers implied. SysV x86-64 ABI.
;  Requires DF=0 on entry. Alignment is not required.
;
global __memset64
global __arch_memset64
__memset64:
__arch_memset64:
	mov r9, rdi
	mov rax, rsi
	mov rcx, rdx
	rep stosq
	mov rax, r9
	ret
