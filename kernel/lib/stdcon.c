#include <stdint.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdbool.h>
#include "vga.h"
#include "kbhandler.h"
#include "stdcon.h"

static int cursor_X = 0;
static int cursor_Y = 0;

void validate_coords() {
    if(cursor_X < 0) cursor_X = 0;
    if(cursor_Y < 0) cursor_Y = 0;
    
    if(cursor_X > 79) {
        cursor_X = 0; 
        cursor_Y++;
    }
    
    if(cursor_Y > 24) {
        cursor_Y = 24;
        // scrolling soon
    }
}

void backspace(uint8_t colour) {
    if (cursor_X == 0 && cursor_Y == 0)
        return;

    if (cursor_X == 0) {
        cursor_X = 79;
        cursor_Y--;
    } else {
        cursor_X--;
    }

    // Erase the character
    vga_write_xy(' ', cursor_X, cursor_Y, colour);

    vga_move_cursor(cursor_X, cursor_Y);
}

void putc(char c, uint8_t colour) {
    if (c == '\n') {
        cursor_X = 0;
        cursor_Y++;
    }
    else {
        vga_write_xy(c, cursor_X, cursor_Y, colour);
        cursor_X++;
    }

    validate_coords();
    vga_move_cursor(cursor_X, cursor_Y);
}

void print_int(int n, uint8_t colour)
{
    unsigned int num;

    if (n < 0)
    {
        putc('-', colour);
        num = -(unsigned int)n;
    }
    else
        num = (unsigned int)n;

    if (num >= 10)
        print_int(num / 10, colour);

    putc('0' + (num % 10), colour);
}

void print(char *string, uint8_t colour) {
    while(*string) {
        putc(*string, colour);
        string++;
    }
}

void printv2(char* string, uint8_t colour, ...) {
    va_list lst;
    va_start(lst, colour);
    while(*string) {
        if(*string!='*'){
            putc(*string, colour);
            string++;
            continue;
        }
        string++;
        switch(*string) {
            case '*':
            string++;
            break;
            case 'i':
            print_int(va_arg(lst, int), colour);
            string++;
            break;
            case 's':
            print(va_arg(lst, char *), colour);
            string++;
            break;
            case 'c':
            putc(va_arg(lst, int), colour);
            string++;
            break;
            default:
            string++;
            break;
        }
    }
}

char getc(int echo, uint8_t colour) {
    char c = get_char();
    clear_keybuffer();
    if(echo && c && c != '\b') {
        putc(c, colour);
    }
    return c;
}

void copy(unknown *dst, const unknown *source, size_t length) {
    for(size_t i = 0; i < length; i++) {
        dst[i] = source[i];
    }
}

void read(char* dst, size_t length, int echo, uint8_t colour) {
    size_t amount = 0;

    if (length == 0)
        return;

    while (true) {
        __asm__ volatile("hlt");

        char c = getc(0, colour);

        if (!c)
            continue;

        if (c == '\n') {
            if (echo)
                putc('\n', colour);
            break;
        }

        if (c == '\b') {
            if (amount > 0) {
                amount--;

                dst[amount] = '\0';

                if (echo)
                    backspace(colour);
            }

            continue;
        }

        if (amount < length - 1) {
            dst[amount] = c;
            amount++;

            if (echo)
                putc(c, colour);
        }
    }

    dst[amount] = '\0';
}

int strcmp(const char *s1, const char *s2) {
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(const unsigned char *)s1 - *(const unsigned char *)s2;
}

int strncmp(const char *s1, const char *s2, size_t n) {
    if (n == 0) {
        return 0;
    }

    while (n > 1 && *s1 && (*s1 == *s2)) {
        s1++;
        s2++;
        n--;
    }

    return *(const unsigned char *)s1 - *(const unsigned char *)s2;
}

void clear_screen(uint8_t colour) {
    vga_clear_screen(colour);
    cursor_X = 0;
    cursor_Y = 0;
}