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

void CMDmf(fat32_t* fs)
{
    if (argc < 2) {
        printv2(
            "Usage: mf <file>\n",
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

    fat32_file_t file;

    fat32_result_t result =
        fat32_create_file(
            fs,
            path,
            &file
        );

    if (result != FAT32_OK) {
        printv2(
            "mf: could not create file\n",
            0x07
        );
        return;
    }

    printv2(
        "Created: *s\n",
        0x07,
        path
    );
}

void CMDwt(fat32_t* fs)
{
    if (argc < 3) {
        printv2(
            "Usage: wt <file> <text>\n",
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

    fat32_file_t file;

    fat32_result_t result =
        fat32_open(
            fs,
            path,
            &file
        );

    /*
     * If the file doesn't exist,
     * create it first.
     */
    if (result == FAT32_NOT_FOUND) {

        result =
            fat32_create_file(
                fs,
                path,
                &file
            );

        if (result != FAT32_OK) {
            printv2(
                "wt: could not create file\n",
                0x07
            );
            return;
        }

    } else if (result != FAT32_OK) {

        printv2(
            "wt: could not open file\n",
            0x07
        );

        return;
    }

    /*
     * Calculate the total length of all
     * arguments after the filename.
     */
    uint32_t total_length = 0;

    for (int i = 2; i < argc; i++) {

        total_length +=
            strlen(argv[i]);

        if (i < argc - 1)
            total_length++;
    }

    if (total_length == 0) {
        printv2(
            "wt: empty text\n",
            0x07
        );
        return;
    }

    /*
     * Build the text into a buffer.
     */
    char buffer[256];

    uint32_t position = 0;

    for (int i = 2; i < argc; i++) {

        size_t length =
            strlen(argv[i]);

        for (size_t j = 0;
             j < length;
             j++) {

            if (position >=
                sizeof(buffer) - 1) {

                printv2(
                    "wt: text too long\n",
                    0x07
                );

                return;
            }

            buffer[position++] =
                argv[i][j];
        }

        if (i < argc - 1) {

            if (position >=
                sizeof(buffer) - 1) {

                printv2(
                    "wt: text too long\n",
                    0x07
                );

                return;
            }

            buffer[position++] = ' ';
        }
    }

    buffer[position] = '\0';

    /*
     * Write from the beginning of the file.
     */
    file.position = 0;

    uint32_t bytes_written = 0;

    result =
        fat32_write(
            &file,
            buffer,
            position,
            &bytes_written
        );

    if (result != FAT32_OK) {

        printv2(
            "wt: write failed\n",
            0x07
        );

        return;
    }

    if (bytes_written != position) {

        printv2(
            "wt: incomplete write\n",
            0x07
        );

        return;
    }

    printv2(
        "Wrote *i bytes to *s\n",
        0x07,
        bytes_written,
        path
    );
}

void CMDmkdir(fat32_t* fs)
{
    if (argc < 2) {
        printv2(
            "Usage: mkdir <directory>\n",
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

    fat32_result_t result =
        fat32_mkdir(
            fs,
            path
        );

    if (result != FAT32_OK) {
        printv2(
            "mkdir: could not create directory\n",
            0x07
        );
        return;
    }

    printv2(
        "Created directory: *s\n",
        0x07,
        path
    );
}

void CMDrmf(fat32_t* fs)
{
    if (argc < 2) {
        printv2(
            "Usage: rmf <file>\n",
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

    fat32_result_t result =
        fat32_remove_file(
            fs,
            path
        );

    if (result == FAT32_NOT_FOUND) {

        printv2(
            "rmf: file not found\n",
            0x07
        );

        return;
    }

    if (result == FAT32_NOT_A_FILE) {

        printv2(
            "rmf: is a directory\n",
            0x07
        );

        return;
    }

    if (result != FAT32_OK) {

        printv2(
            "rmf: could not remove file\n",
            0x07
        );

        return;
    }

    printv2(
        "Removed: *s\n",
        0x07,
        path
    );
}

void CMDrmd(fat32_t* fs)
{
    if (argc < 2) {
        printv2(
            "Usage: rmd <directory>\n",
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

    fat32_result_t result =
        fat32_remove_directory(
            fs,
            path
        );

    if (result == FAT32_NOT_FOUND) {

        printv2(
            "rmd: directory not found\n",
            0x07
        );

        return;
    }

    if (result == FAT32_NOT_A_DIRECTORY) {

        printv2(
            "rmd: not a directory\n",
            0x07
        );

        return;
    }

    if (result == FAT32_DIRECTORY_NOT_EMPTY) {

        printv2(
            "rmd: directory not empty\n",
            0x07
        );

        return;
    }

    if (result != FAT32_OK) {

        printv2(
            "rmd: could not remove directory\n",
            0x07
        );

        return;
    }

    printv2(
        "Removed directory: *s\n",
        0x07,
        path
    );
}

void CMDtrc(fat32_t* fs)
{
    if (argc < 3) {
        printv2(
            "Usage: trc <file> <size>\n",
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

    /*
     * Parse the size manually so we don't
     * depend on strtoul() being available.
     */
    uint32_t size = 0;

    for (uint32_t i = 0;
         argv[2][i] != '\0';
         i++) {

        char c = argv[2][i];

        if (c < '0' || c > '9') {
            printv2(
                "trc: invalid size\n",
                0x07
            );
            return;
        }

        uint32_t digit =
            (uint32_t)(c - '0');

        /*
         * Prevent uint32_t overflow.
         */
        if (size >
            (0xFFFFFFFF - digit) / 10) {

            printv2(
                "trc: size too large\n",
                0x07
            );
            return;
        }

        size =
            size * 10 + digit;
    }

    fat32_file_t file;

    fat32_result_t result =
        fat32_open(
            fs,
            path,
            &file
        );

    if (result == FAT32_NOT_FOUND) {

        printv2(
            "trc: file not found\n",
            0x07
        );

        return;
    }

    if (result == FAT32_NOT_A_FILE) {

        printv2(
            "trc: not a file\n",
            0x07
        );

        return;
    }

    if (result != FAT32_OK) {

        printv2(
            "trc: could not open file\n",
            0x07
        );

        return;
    }

    result =
        fat32_truncate(
            &file,
            size
        );

    if (result != FAT32_OK) {

        printv2(
            "trc: operation failed\n",
            0x07
        );

        return;
    }

    printv2(
        "Truncated *s to *i bytes\n",
        0x07,
        path,
        size
    );
}

void CMDmv(fat32_t* fs)
{
    if (argc < 3) {
        printv2(
            "Usage: mv <old> <new>\n",
            0x07
        );
        return;
    }

    char old_path[256];
    char new_path[256];

    normalize_path(
        argv[1],
        old_path,
        sizeof(old_path)
    );

    normalize_path(
        argv[2],
        new_path,
        sizeof(new_path)
    );

    fat32_result_t result =
        fat32_rename_file(
            fs,
            old_path,
            new_path
        );

    if (result == FAT32_NOT_FOUND) {

        printv2(
            "mv: file not found\n",
            0x07
        );

        return;
    }

    if (result == FAT32_NOT_A_FILE) {

        printv2(
            "mv: not a file\n",
            0x07
        );

        return;
    }

    if (result != FAT32_OK) {

        printv2(
            "mv: could not move file\n",
            0x07
        );

        return;
    }

    printv2(
        "Renamed *s -> *s\n",
        0x07,
        old_path,
        new_path
    );
}

void CMDpwd()
{
    printv2("*s\n", 0x07, cwd);
}

static void exec_script(
    fat32_t *fs,
    char *buffer,
    uint32_t size,
    bool *keep_alive
)
{
    uint32_t position = 0;

    while (position < size && *keep_alive) {

        char *line = &buffer[position];

        /*
         * Find the end of this line.
         */
        while (
            position < size &&
            buffer[position] != '\n' &&
            buffer[position] != '\r'
        ) {
            position++;
        }

        /*
         * Terminate the line.
         */
        if (position < size) {
            buffer[position] = '\0';
        }

        /*
        * Ignore blank lines and comments.
        */
        if (line[0] != '\0' && line[0] != '#') {

            argc = parse_args(
                line,
                argv,
                16
            );

            if (argc > 0) {
                exec(fs, keep_alive);
            }
        }

        /*
         * Skip \r and/or \n.
         */
        while (
            position < size &&
            (
                buffer[position] == '\n' ||
                buffer[position] == '\r'
            )
        ) {
            position++;
        }
    }

    /*
     * Don't leave the shell with the script's arguments.
     */
    argc = 0;
    argv[0] = NULL;
}

void CMDrun(fat32_t *fs, bool *keep_alive)
{
    if (argc < 2) {
        print(
            "run: not enough arguments\n"
            "usage: run <filename>\n"
            "\n"
            "note: run only supports files up to 1024 bytes of size.\n",
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

    fat32_file_t codefile;

    fat32_result_t result =
        fat32_open(
            fs,
            path,
            &codefile
        );

    if (result != FAT32_OK) {
        print(
            "run: unable to open file\n",
            0x07
        );
        return;
    }

    nostack char bfr[1025];

    uint32_t bytes_read = 0;

    result =
        fat32_read(
            &codefile,
            bfr,
            1024,
            &bytes_read
        );

    if (result != FAT32_OK) {
        print(
            "run: unable to read file\n",
            0x07
        );
        return;
    }

    bfr[bytes_read] = '\0';

    exec_script(
        fs,
        bfr,
        bytes_read,
        keep_alive
    );
}

void exec(fat32_t *fs, bool *keep_alive) {
    if(argc==0) {}
    else if(!strcmp(argv[0], "clear")) {
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
    else if(!strcmp(argv[0], "mf")) {
        CMDmf(fs);
    }
    else if(!strcmp(argv[0], "wt")) {
        CMDwt(fs);
    }
    else if(!strcmp(argv[0], "md")) {
        CMDmkdir(fs);
    }
    else if(!strcmp(argv[0], "rmf")) {
        CMDrmf(fs);
    }
    else if(!strcmp(argv[0], "rmd")) {
        CMDrmd(fs);
    }
    else if(!strcmp(argv[0], "trc")) {
        CMDtrc(fs);
    }
    else if(!strcmp(argv[0], "mv")) {
        CMDmv(fs);
    }
    else if(!strcmp(argv[0], "pwd")) {
        CMDpwd();
    }
    else if(!strcmp(argv[0], "exit")) {
        *keep_alive = 0;
    }
    else if(!strcmp(argv[0], "run")) {
        CMDrun(fs, keep_alive);
    }
    else if(!strcmp(argv[0], "help")) {
        printv2("Available commands:\n", 0x07);
        printv2("  clear - Clear the screen\n", 0x07);
        printv2("  echo - Print text to the screen\n", 0x07);
        printv2("  echon - Print text to the screen with a newline\n", 0x07);
        printv2("  uptime - Show system uptime\n", 0x07);
        printv2("  ls - List files and directories\n", 0x07);
        printv2("  exit - Exit the shell\n", 0x07);
        printv2("  help - Show this help message\n", 0x07);
        printv2("  mf - Create an empty file\n", 0x07);
        printv2("  rd - Read and display a file's contents\n", 0x07);
        printv2("  cd - Change the current directory\n", 0x07);
        printv2("  wt - Write text to a file\n", 0x07);
        printv2("  md - Create a new directory\n", 0x07);
        printv2("  rmf - Remove a file\n", 0x07);
        printv2("  rmd - Remove an empty directory\n", 0x07);
        printv2("  trc - Truncate a file to a specified size\n", 0x07);
        printv2("  mv - Rename a file\n", 0x07);
        printv2("  pwd - Print working directory\n", 0x07);
        printv2("  run - Run a file with commands", 0x07);
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

void shell_poweron(fat32_t* fs, bool *keep_alive)
{
    nostack char input[384];

    fat32_file_t fusername;
    if(fat32_open(fs, "/OPT/USERNAME", &fusername) != FAT32_OK)
    {
        sadlog("FAILED TO OPEN /OPT/USERNAME");
        return;
    }
    nostack char username[36];
    uint32_t bytes_read;
    if(fat32_read(&fusername, username, 36, &bytes_read) != FAT32_OK) {
        sadlog("FAILED TO READ /OPT/USERNAME");
        return;
    }

    while(*keep_alive) {

        printv2(
            "*s@imOS~*s>> ",
            vga_color(VGA_LIGHT_BLUE, VGA_BLACK),
            username,
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
        
        exec(fs, keep_alive);
    }
}