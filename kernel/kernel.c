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

    for(int i = 0x0; i < 0x0F; i++) {
        putc('#', i);
    }
    print("\n", 0x07);
    happylog("Happy log!");
    normallog("Normal log!");
    sadlog("Sad log!");
    angrylog("Angry log!");
    sleep(SEC(1));
    happylog("Booting into imOS!");
    sleep(MS(500));
    clear_screen(0x07);

    printv2("Hello from imOS!\n", 0x07);

    shell_poweron();

    for(;;)
        __asm__ volatile("hlt");
}