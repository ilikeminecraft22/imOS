#include <stdint.h>
#include <stdbool.h>
#include "lib/vga.h"
#include "lib/stdcon.h"
#include "lib/idt.h"
#include "lib/pit.h"
#include "lib/shell.h"

void kmain(void) {
    uint16_t kernel_code_selector = 0x08;
    init_interrupts(kernel_code_selector);
    vga_clear_screen(0x07);

    printv2("Hello from imOS!\n", 0x07);

    shell_poweron();

    for(;;)
        __asm__ volatile("hlt");
}