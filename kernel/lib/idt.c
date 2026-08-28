#include "idt.h"
#include "io.h"
#include "kbhandler.h"

#define PIC1_COMMAND 0x20
#define PIC1_DATA    0x21
#define PIC2_COMMAND 0xA0
#define PIC2_DATA    0xA1

extern void timer_stub(void);
extern void keyboard_stub(void);

static struct IDTEntry idt[256];
static struct IDTR idtr;

volatile uint64_t timer_ticks = 0;

static void pic_remap(void) {
    outb(PIC1_COMMAND, 0x11);
    outb(PIC2_COMMAND, 0x11);
    outb(PIC1_DATA, 0x20);
    outb(PIC2_DATA, 0x28);
    outb(PIC1_DATA, 0x04);
    outb(PIC2_DATA, 0x02);
    outb(PIC1_DATA, 0x01);
    outb(PIC2_DATA, 0x01);
    
    outb(PIC1_DATA, 0xFC);     
    outb(PIC2_DATA, 0xFF);     
}

static void idt_set_gate(int vector, uint64_t isr_address, uint16_t gdt_selector) {
    idt[vector].offset_low  = (uint16_t)(isr_address & 0xFFFF);
    idt[vector].selector    = gdt_selector;
    idt[vector].ist         = 0;
    idt[vector].type_attr   = 0x8E;
    idt[vector].offset_mid  = (uint16_t)((isr_address >> 16) & 0xFFFF);
    idt[vector].offset_high = (uint32_t)((isr_address >> 32) & 0xFFFFFFFF);
    idt[vector].zero        = 0;
}

void init_interrupts(uint16_t kernel_code_selector) {
    pic_remap();

    for(int i = 0; i < 256; i++) {
        idt_set_gate(i, 0, 0);
        idt[i].type_attr = 0; 
    }

    idt_set_gate(32, (uint64_t)timer_stub, kernel_code_selector);
    idt_set_gate(33, (uint64_t)keyboard_stub, kernel_code_selector);

    idtr.limit = (sizeof(struct IDTEntry) * 256) - 1;
    idtr.base  = (uint64_t)&idt;
    
    __asm__ volatile ("lidt %0" : : "m"(idtr));
    __asm__ volatile ("sti");
}

void isr_handler(struct Registers* regs) {
    if (regs->int_no == 32) {
        timer_ticks++;
    } 
    else if (regs->int_no == 33) {
        uint8_t scancode = inb(0x60);
        if (!(scancode & 0x80)) {
            keyboard_handle_scan(scancode);
        }
    }

    
    outb(PIC1_COMMAND, 0x20);
}
