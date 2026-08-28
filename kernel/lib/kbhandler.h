#pragma once

#include <stdint.h>

void keyboard_handle_scan(uint8_t scancode);
char get_char();
void clear_keybuffer();