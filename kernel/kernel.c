#include <stdint.h>
#include <stdbool.h>
#include "lib/vga.h"
#include "lib/stdcon.h"
#include "lib/idt.h"

void kmain(void) {
    uint16_t kernel_code_selector = 0x08;
    init_interrupts(kernel_code_selector);

    printv2("Hello from imOS!", 0x07);
    nostack char text[128];
    read(text, 128, 1, 0x07);

    for(;;)
        __asm__ volatile("hlt");
}