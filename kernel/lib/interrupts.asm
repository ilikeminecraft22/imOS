[bits 64]
extern isr_handler
global timer_stub
global keyboard_stub

%macro IRQ_STUB 2
%1:
    push qword 0       
    push qword %2      
    jmp irq_common
%endmacro

IRQ_STUB timer_stub, 32
IRQ_STUB keyboard_stub, 33

irq_common:
    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push rbp
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15

    mov rdi, rsp        
    cld                 
    call isr_handler

    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rbp
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax

    add rsp, 16
    iretq
