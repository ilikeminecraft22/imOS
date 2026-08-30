#include <stdint.h>
#include "io.h"

volatile uint8_t* vga_buffer = (volatile uint8_t*)0xB8000;

void vga_move_cursor(int x, int y) {
    if(x >= 80) {
        x = 0;
        y++;
    }
    if(y >= 25){
        y = 24;
    }
    uint16_t pos = y * 80 + x;

    outb(0x3D4, 0x0F);
    outb(0x3D5, (uint8_t) (pos & 0xFF));

    outb(0x3D4, 0x0E);
    outb(0x3D5, (uint8_t) ((pos >> 8) & 0xFF));
}

void vga_write_xy(char c, int x, int y, uint8_t colour) {
    if(x >= 80 || y >=25) return;

    uint32_t offset = (y * 80 + x) * 2;
    vga_buffer[offset] = (uint8_t)c;
    vga_buffer[offset + 1] = colour;
}

void vga_clear_screen(uint8_t colour) {
    for(int i = 0; i < 79; i++) {
        for(int j = 0; j < 24; j++) {
            vga_write_xy(' ', i, j, colour);
        }
    }
}