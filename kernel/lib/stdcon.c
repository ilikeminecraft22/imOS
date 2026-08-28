#include <stdint.h>
#include <stdarg.h>
#include "vga.h"

static int cursor_X = 0;
static int cursor_Y = 0;

void validate_coords() {
    if(cursor_X < 0) cursor_X = 0;
    if(cursor_Y < 0) cursor_Y = 0;
    if(cursor_X > 79) {cursor_X = 0; cursor_Y++;}
    if(cursor_Y > 24) cursor_X = 24;
}

void putc(char c, uint8_t colour) {
    validate_coords();
    vga_write_xy(c, cursor_X, cursor_Y, colour);
    vga_move_cursor(cursor_X+1, cursor_Y);
    cursor_X++;
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
            default:
            string++;
            break;
        }
    }
}