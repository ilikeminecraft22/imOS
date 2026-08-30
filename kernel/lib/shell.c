#include "stdcon.h"
#include "pit.h"
#include "shell.h"
#include "vga.h"
#include <stddef.h>

nostack char *argv[16];
nostack int argc;

int parse_args(char *input, char **argv, size_t max_args)
{
    int argc = 0;
    char *p = input;

    while (*p && argc < max_args - 1)
    {
        while (*p == ' ' || *p == '\t')
            p++;

        if (!*p)
            break;

        argv[argc++] = p;

        while (*p && *p != ' ' && *p != '\t')
            p++;

        if (*p)
        {
            *p = '\0';
            p++;
        }
    }

    argv[argc] = NULL;
    return argc;
}

void CMDclear() {
    clear_screen(0x07);
}

void CMDecho()
{
    for (int i = 1; i < argc; i++)
    {
        printv2("*s", 0x07, argv[i]);

        if (i < argc - 1)
            printv2(" ", 0x07);
    }

}

void CMDechon()
{
    for (int i = 1; i < argc; i++)
    {
        printv2("*s", 0x07, argv[i]);

        if (i < argc - 1)
            printv2(" ", 0x07);
    }
    print("\n", 0x07);
}

void CMDimOSfetch() {
    print("imOS@nasa-pc\n", 0x09);
    print("|  RAM:  10 NIGGABYTES\n", 0x09);
    print("|  DISK: 1 NIGGERBYTES\n", 0x09);
    print("|  CPU:  inTELAVIV 64 Core CPU @ 8Nhz\n", 0x09);
    print("|  GPU:  RADEON 16834\n", 0x09);
}

void shell_poweron() {
    int should_run = 1;
    nostack char input[384];
    while(should_run) {
        printv2("[imOS] > ", 0x0A);
        read(input, 256, 1, 0x07);
        argc = parse_args(input, argv, 16);
        if(!strncmp(input, "clear", 5)) {
            CMDclear();
        }
        else if(!strncmp(input, "echo", 4)) {
            CMDecho();
        }
        else if(!strncmp(input, "echon", 5)) {
            CMDecho();
        }
        else if(!strncmp(input, "imosfetch", 9)) {
            CMDimOSfetch();
        }
    }
}