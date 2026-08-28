.set MAGIC,    0xe85250d6
.set ARCH,     0
.set LENGTH,   (multiboot_end - multiboot_start)
.set CHECKSUM, -(MAGIC + ARCH + LENGTH)

.section .multiboot
.balign 8
multiboot_start:
    .long MAGIC
    .long ARCH
    .long LENGTH
    .long CHECKSUM
    
    .short 0
    .short 0
    .long 8
multiboot_end:

.section .bss
.balign 16
stack_bottom:
    .skip 16384
stack_top:

.balign 4096
p4_table:
    .skip 4096
p3_table:
    .skip 4096
p2_table:
    .skip 4096

.section .data
.balign 8
gdt64:
    .quad 0
.set CODE_SEG, . - gdt64
    .quad (1<<43) | (1<<44) | (1<<47) | (1<<53)
gdt64_ptr:
    .short . - gdt64 - 1
    .quad gdt64

.section .text
.code32
.global _start
.type _start, @function

_start:
    cli
    mov $stack_top, %esp

    # 1. Link Page Tables together
    # Point P4 -> P3
    mov $p3_table, %eax
    or $0b11, %eax
    mov %eax, p4_table

    # Point P3 -> P2
    mov $p2_table, %eax
    or $0b11, %eax
    mov %eax, p3_table

    mov $0x00000000, %eax
    or $0b10000011, %eax
    mov %eax, p2_table

    mov $p4_table, %eax
    mov %eax, %cr3

    mov %cr4, %eax
    or $(1 << 5), %eax
    mov %eax, %cr4

    mov $0xC0000080, %ecx
    rdmsr
    or $(1 << 8), %eax
    wrmsr

    mov %cr0, %eax
    or $(1 << 31), %eax
    mov %eax, %cr0

    lgdt gdt64_ptr

    push $CODE_SEG
    push $_start64
    lret

.code64
_start64:
    xor %ax, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs
    mov %ax, %ss

    call kmain

.hlt_loop:
    hlt
    jmp .hlt_loop