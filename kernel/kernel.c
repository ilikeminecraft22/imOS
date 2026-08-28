#include <stdint.h>
#include <stdbool.h>
#include "lib/vga.h"

void kmain(void) {
    vga_write_xy('A', 0, 0, 0x07);
    vga_move_cursor(1, 0);
    for(;;)
        asm volatile("hlt");
}