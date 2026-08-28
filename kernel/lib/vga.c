#include <stdint.h>

volatile uint8_t* vga_buffer = (volatile uint8_t*)0xB8000;

void vga_write_xy(char c, int x, int y, uint8_t colour) {
    if(x >= 80 || y >=25) return;

    uint32_t offset = (y * 80 + x) * 2;
    vga_buffer[offset] = (uint8_t)c;
    vga_buffer[offset + 1] = colour;
}