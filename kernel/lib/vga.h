#pragma once
#include <stdint.h>

void vga_write_xy(char c, int x, int y, uint8_t colour);
void vga_move_cursor(int x, int y);