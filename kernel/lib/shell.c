#include "stdcon.h"
#include "pit.h"
#include "shell.h"
#include "vga.h"
#include "../fs/fat32/fat32.h"
#include <stddef.h>

nostack char *argv[16];
nostack int argc;

nostack char cwd[256] = "/";

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

static void normalize_path(
    const char* input,
    char* output,
    size_t output_size
)
{
    if (!input || !output || output_size == 0)
        return;

    char temp[256];
    size_t length = 0;

    /*
     * Build an absolute path first.
     */
    if (input[0] == '/') {

        while (input[length] &&
               length < sizeof(temp) - 1) {

            temp[length] = input[length];
            length++;
        }

    } else {

        size_t cwd_length = strlen(cwd);

        for (size_t i = 0;
             i < cwd_length &&
             i < sizeof(temp) - 1;
             i++) {

            temp[i] = cwd[i];
        }

        length = cwd_length;

        if (length > 1 &&
            length < sizeof(temp) - 1) {

            temp[length++] = '/';
        }

        for (size_t i = 0;
             input[i] &&
             length < sizeof(temp) - 1;
             i++) {

            temp[length++] = input[i];
        }
    }

    temp[length] = '\0';

    /*
     * Parse path components.
     */
    char result[256];
    size_t result_length = 1;

    result[0] = '/';

    size_t i = 0;

    while (temp[i]) {

        while (temp[i] == '/')
            i++;

        if (!temp[i])
            break;

        char component[256];
        size_t component_length = 0;

        while (
            temp[i] &&
            temp[i] != '/' &&
            component_length < sizeof(component) - 1
        ) {
            component[component_length++] =
                temp[i++];

        }

        component[component_length] = '\0';

        /*
         * "." does nothing.
         */
        if (!strcmp(component, "."))
            continue;

        /*
         * ".." goes up one directory.
         */
        if (!strcmp(component, "..")) {

            if (result_length > 1) {

                /*
                 * Remove trailing slash.
                 */
                if (result_length > 1 &&
                    result[result_length - 1] == '/')
                    result_length--;

                while (
                    result_length > 1 &&
                    result[result_length - 1] != '/'
                ) {
                    result_length--;
                }
            }

            continue;
        }

        /*
         * Add slash between components.
         */
        if (result_length > 1 &&
            result[result_length - 1] != '/') {

            if (result_length >= sizeof(result) - 1)
                break;

            result[result_length++] = '/';
        }

        for (size_t j = 0;
             j < component_length &&
             result_length < sizeof(result) - 1;
             j++) {

            result[result_length++] =
                component[j];
        }
    }

    result[result_length] = '\0';

    /*
     * Copy to caller.
     */
    size_t j = 0;

    while (
        result[j] &&
        j < output_size - 1
    ) {
        output[j] = result[j];
        j++;
    }

    output[j] = '\0';
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

void CMDls(fat32_t* fs)
{
    char path[256];

    if (argc >= 2) {

        normalize_path(
            argv[1],
            path,
            sizeof(path)
        );

    } else {

        normalize_path(
            ".",
            path,
            sizeof(path)
        );
    }

    fat32_dir_t dir;

    fat32_result_t result =
        fat32_opendir(
            fs,
            path,
            &dir
        );

    if (result != FAT32_OK) {
        printv2(
            "ls: cannot open directory\n",
            0x07
        );
        return;
    }

    while (1) {

        char name[256];
        bool is_directory;
        uint32_t size;

        result =
            fat32_readdir(
                &dir,
                name,
                sizeof(name),
                &is_directory,
                &size
            );

        if (result == FAT32_END_OF_FILE)
            break;

        if (result != FAT32_OK) {
            printv2(
                "ls: error reading directory\n",
                0x07
            );
            return;
        }

        if (is_directory) {

            printv2(
                "[DIR]  *s\n",
                0x07,
                name
            );

        } else {

            printv2(
                "[FILE] *s\n",
                0x07,
                name
            );
        }
    }
}

void CMDrd(fat32_t* fs)
{
    if (argc < 2) {
        printv2("Usage: rd <file>\n", 0x07);
        return;
    }

    char path[256];

    normalize_path(
        argv[1],
        path,
        sizeof(path)
    );

    fat32_file_t file;

    fat32_result_t result =
        fat32_open(
            fs,
            path,
            &file
        );

    if (result != FAT32_OK) {
        printv2(
            "rd: file not found\n",
            0x07
        );
        return;
    }

    char buffer[512];

    uint32_t remaining = file.size;

    while (remaining > 0) {

        uint32_t chunk = remaining;

        if (chunk > sizeof(buffer))
            chunk = sizeof(buffer);

        uint32_t bytes_read = 0;

        result =
            fat32_read(
                &file,
                buffer,
                chunk,
                &bytes_read
            );

        if (result != FAT32_OK) {
            printv2(
                "\nrd: read error\n",
                0x07
            );
            return;
        }

        if (bytes_read == 0)
            break;

        for (uint32_t i = 0;
             i < bytes_read;
             i++) {

            putc(buffer[i], 0x07);
        }

        remaining -= bytes_read;
    }

    printv2("\n", 0x07);
}

void CMDcd(fat32_t* fs)
{
    if (argc < 2) {
        printv2(
            "Usage: cd <directory>\n",
            0x07
        );
        return;
    }

    char path[256];

    normalize_path(
        argv[1],
        path,
        sizeof(path)
    );

    fat32_dir_t dir;

    fat32_result_t result =
        fat32_opendir(
            fs,
            path,
            &dir
        );

    if (result != FAT32_OK) {
        printv2(
            "cd: directory not found\n",
            0x07
        );
        return;
    }

    /*
     * The directory exists.
     * Change the shell's cwd.
     */
    size_t i = 0;

    while (
        path[i] &&
        i < sizeof(cwd) - 1
    ) {
        cwd[i] = path[i];
        i++;
    }

    cwd[i] = '\0';
}

void CMDuptime() {
    if(argc < 2) {printv2("*i sec\n", 0x07, SECL(get_uptime())); return;}
    if(!strcmp(argv[1], "~s")) {
        printv2("*i sec\n", 0x07, SECL(get_uptime()));
    }
    else if(!strcmp(argv[1], "~ms")) {
        printv2("*i ms\n", 0x07, MSL(get_uptime()));
    }
    else if(!strcmp(argv[1], "~min")) {
        printv2("*i min\n", 0x07, MINL(get_uptime()));
    }
    else if(!strcmp(argv[1], "~h")) {
        printv2("Usage: uptime\n ~s - in seconds\n ~ms - in milliseconds\n ~min - in minutes\n ~h - show help\n\n default: in seconds\n", 0x07);
    }
    else {
        printv2("*i sec\n", 0x07, SECL(get_uptime()));
    }
}

void shell_poweron(fat32_t* fs)
{
    int should_run = 1;
    nostack char input[384];

    while(should_run) {

        printv2(
            "[imOS~*s] > ",
            0x0A,
            cwd
        );

        read(
            input,
            256,
            1,
            0x07
        );

        argc =
            parse_args(
                input,
                argv,
                16
            );

        if (argc == 0)
            continue;

        if(!strcmp(argv[0], "clear")) {
            CMDclear();
        }
        else if(!strcmp(argv[0], "echon")) {
            CMDechon();
        }
        else if(!strcmp(argv[0], "echo")) {
            CMDecho();
        }
        else if(!strcmp(argv[0], "uptime")) {
            CMDuptime();
        }
        else if(!strcmp(argv[0], "ls")) {
            CMDls(fs);
        }
        else if(!strcmp(argv[0], "rd")) {
            CMDrd(fs);
        }
        else if(!strcmp(argv[0], "cd")) {
            CMDcd(fs);
        }
        else if(!strcmp(argv[0], "exit")) {
            should_run = 0;
        }
        else if(!strcmp(argv[0], "help")) {
            printv2("Available commands:\n", 0x07);
            printv2("  clear - Clear the screen\n", 0x07);
            printv2("  echo - Print text to the screen\n", 0x07);
            printv2("  echon - Print text to the screen without a newline\n", 0x07);
            printv2("  uptime - Show system uptime\n", 0x07);
            printv2("  ls - List files and directories\n", 0x07);
            printv2("  exit - Exit the shell\n", 0x07);
            printv2("  help - Show this help message\n", 0x07);
        }
        else if(!strcmp(argv[0], "")) {}
        else {
            printv2(
                "Unknown command: *s\n",
                0x07,
                argv[0]
            );
        }
    }
}