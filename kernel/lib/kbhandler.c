#include "kbhandler.h"
#include "stdcon.h"
#include <stdbool.h>

#define SCAN_LSHIFT    0x2A
#define SCAN_RSHIFT    0x36
#define SCAN_CAPSLOCK  0x3A

static bool shift_pressed = false;
static bool caps_lock_active = false;

static const char kbd_us_normal[128] = {
    0,  27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
  '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
    0,  'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',   0,
 '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/',   0,   '*',   0,
  ' ',   0
};

static const char kbd_us_shift[128] = {
    0,  27, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b',
  '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n',
    0,  'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~',   0,
  '|', 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?',   0,   '*',   0,
  ' ',   0
};

extern void kernel_print_char(char c);

// static volatile char keybuffer[8];
volatile char keybuffer = 0;

// void keybuffer_push(char key) {
//     for(int i = 7; i = 1; --i)
//     {
//         keybuffer[i] = keybuffer[i-1];
//     }
//     keybuffer[0] = key;
// }

// char get_char(int id) {
//     if(id < 0) return 0;
//     else if(id > 7) return 0;
//     return keybuffer[id];
// }
char get_char() {return keybuffer;}
void clear_keybuffer() {keybuffer = 0;}

void keyboard_handle_scan(uint8_t scancode) {
    if (scancode & 0x80) {
        uint8_t released_code = scancode & 0x7F;

        if (released_code == SCAN_LSHIFT || released_code == SCAN_RSHIFT) {
            shift_pressed = false;
        }
        return;
    }

    switch (scancode) {
        case SCAN_LSHIFT:
        case SCAN_RSHIFT:
            shift_pressed = true;
            return;
            
        case SCAN_CAPSLOCK:
            caps_lock_active = !caps_lock_active;
            return;
            
        default:
            break;
    }

    if (scancode >= 128) return;

    char ascii = shift_pressed ? kbd_us_shift[scancode] : kbd_us_normal[scancode];

    if (ascii >= 'a' && ascii <= 'z') {
        if (shift_pressed ^ caps_lock_active) {
            ascii -= 32;
        }
    } else if (ascii >= 'A' && ascii <= 'Z') {
        if (shift_pressed ^ caps_lock_active) {
        } else {
            ascii += 32;
        }
    }

    if (ascii != 0) {
        keybuffer = ascii;
    }
}
