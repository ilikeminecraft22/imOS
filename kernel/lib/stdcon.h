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

int strncmp(const char *s1, const char *s2, size_t n);
int strcmp(const char *s1, const char *s2);
void clear_screen(uint8_t colour);