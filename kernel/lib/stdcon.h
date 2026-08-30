#pragma once
#include <stdint.h>
#include <stddef.h>

#define nostack static

typedef uint8_t byte;
typedef uint8_t unknown;

void putc(char c, uint8_t colour);
void print_int(int n, uint8_t colour);
void print(char *string, uint8_t colour);
void printv2(char* string, uint8_t colour, ...);
char getc(int echo, uint8_t colour);
void copy(unknown *dst, const unknown *source, size_t length);
void read(char* dst, size_t length, int echo, uint8_t colour);