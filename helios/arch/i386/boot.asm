; Declare constants for the multiboot header.
MBALIGN  equ  1 << 0            ; align loaded modules on page boundaries
MEMINFO  equ  1 << 1            ; provide memory map
MBFLAGS  equ  MBALIGN | MEMINFO ; this is the Multiboot 'flag' field
MAGIC    equ  0x1BADB002        ; 'magic number' lets bootloader find the header
CHECKSUM equ -(MAGIC + MBFLAGS)   ; checksum of above, to prove we are multiboot


; Declare a multiboot header that marks the program as a kernel. These are magic
; values that are documented in the multiboot standard. The bootloader will
; search for this signature in the first 8 KiB of the kernel file, aligned at a
; 32-bit boundary. The signature is in its own section so the header can be
; forced to be within the first 8 KiB of the kernel file.
section .multiboot
align 4
	dd MAGIC
	dd MBFLAGS
	dd CHECKSUM
    dd 0, 0, 0, 0, 0

    ; GFX things
    dd 0
    dd 800
    dd 600
    dd 32

 
; The multiboot standard does not define the value of the stack pointer register
; (esp) and it is up to the kernel to provide a stack. This allocates room for a
; small stack by creating a symbol at the bottom of it, then allocating 16384
; bytes for it, and finally creating a symbol at the top. The stack grows
; downwards on x86. The stack is in its own section so it can be marked nobits,
; which means the kernel file is smaller because it does not contain an
; uninitialized stack. The stack on x86 must be 16-byte aligned according to the
; System V ABI standard and de-facto extensions. The compiler will assume the
; stack is properly aligned and failure to align the stack will result in
; undefined behavior.
section .bss
align 16
stack_bottom:
resb 16384 ; 16 KiB
stack_top:

section .boot
global _start
_start:
    ; Bochs magic breakpoint
    ; xchg bx, bx

;     mov edi, (boot_page_table1 - KERNELOFF)
;     mov esi, 0
;     mov ecx, 1023
;
; .1:
;     cmp esi, 0x00100000
;     jl .2f
;     cmp esi, (kernel_end - KERNELOFF)
;
; .2:
;
;
;
    ; xchg bx, bx
    mov ecx, (initial_page_dir - 0xC0000000)
    mov cr3, ecx

    mov ecx, cr4
    or ecx, 0x10
    mov cr4, ecx
    
    mov ecx, cr0
    or ecx, 0x80000000
    mov cr0, ecx

    jmp higher_half

section .text

higher_half:
    ; mov DWORD [initial_page_dir], 0
    ; mov ecx, cr3
    ; mov cr3, ecx

    ; Set up stack
    mov esp, stack_top

    push eax ; push multiboot pointer to stack
    push ebx ; push multiboot magic to stack

    extern kernel_early
    call kernel_early ; Call initial i386 table setup stuff

	extern kernel_main
	call kernel_main

; halt the cpu if nothing else needs to be done
; or until next interrupt
halt:
    hlt
    jmp halt

; This will set up our new segment registers. We need to do
; something special in order to set CS. We do what is called a
; far jump. A jump that includes a segment as well as an offset.
; This is declared in C as 'extern void gdt_flush();'
global gdt_flush     ; Allows the C code to link to this
extern gp            ; Says that 'gp' is in another file
gdt_flush:
    lgdt [gp]        ; Load the GDT with our 'gp' which is a special pointer
    mov ax, 0x10      ; 0x10 is the offset in the GDT to our data segment
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    jmp 0x08:flush2   ; 0x08 is the offset to our code segment: Far jump!
flush2:
    ret               ; Returns back to the C code!

; Loads the IDT defined in 'idtp' into the processor.
; This is declared in C as 'extern void idt_load();'
global idt_load
extern idtp
idt_load:
    lidt [idtp]
    ret

section .data
align 4096
global initial_page_dir
initial_page_dir:
    dd 10000011b
    times 768-1 dd 0
    
    ; dd 10000011b
    dd (0 << 22) | 10000011b
    dd (1 << 22) | 10000011b
    dd (2 << 22) | 10000011b
    dd (3 << 22) | 10000011b
    times 256-1 dd 0
