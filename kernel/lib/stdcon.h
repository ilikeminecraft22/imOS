#pragma once
#include <stdint.h>
#include <stddef.h>

#define nostack static

typedef uint8_t byte;
typedef uint8_t unknown;

void putc(char c, uint8_t colour);
void print_int(int n, uint8_t colour);
void print(char *string, uint8_t colour);
void printv2(const char* string, uint8_t colour, ...);
void print_title_screen();
char getc(int echo, uint8_t colour);
void copy(unknown *dst, const unknown *source, size_t length);
void read(char* dst, size_t length, int echo, uint8_t colour);

int strncmp(const char *s1, const char *s2, size_t n);
int strcmp(const char *s1, const char *s2);
size_t strlen(const char* str);

void clear_screen(uint8_t colour);

void happylog(char* str);
void sadlog(char* str);
void angrylog(char* str);
void normallog(char* str);

typedef enum {
    VGA_BLACK         = 0x0,
    VGA_BLUE          = 0x1,
    VGA_GREEN         = 0x2,
    VGA_CYAN          = 0x3,
    VGA_RED           = 0x4,
    VGA_MAGENTA       = 0x5,
    VGA_BROWN         = 0x6,
    VGA_LIGHT_GRAY    = 0x7,
    VGA_DARK_GRAY     = 0x8,
    VGA_LIGHT_BLUE    = 0x9,
    VGA_LIGHT_GREEN   = 0xA,
    VGA_LIGHT_CYAN    = 0xB,
    VGA_LIGHT_RED     = 0xC,
    VGA_LIGHT_MAGENTA = 0xD,
    VGA_YELLOW        = 0xE,
    VGA_WHITE         = 0xF
} vga_color_t;

uint8_t vga_color(
    vga_color_t foreground,
    vga_color_t background
);