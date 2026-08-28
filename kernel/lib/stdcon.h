#pragma once

void putc(char c, uint8_t colour);
void print_int(int n, uint8_t colour);
void print(char *string, uint8_t colour);
void printv2(char* string, uint8_t colour, ...);
char getc(int echo, uint8_t colour);