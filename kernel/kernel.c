#include <stdint.h>
#include <stdbool.h>
#include "lib/vga.h"
#include "lib/stdcon.h"

void kmain(void) {
    printv2("Hello imOS *i *s", 0x07, 123, "!");
    for(;;)
        asm volatile("hlt");
}