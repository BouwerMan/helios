#include <arch/x86_64/asm.h>

FUNC(__switch_to_new_stack)
	mov %rdi, %rsp
	jmp *%rsi
