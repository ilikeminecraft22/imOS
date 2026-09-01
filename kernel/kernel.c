#include <stdint.h>
#include <stdbool.h>

#include "lib/vga.h"
#include "lib/stdcon.h"
#include "lib/idt.h"
#include "lib/pit.h"
#include "lib/shell.h"

#include "drivers/ata/ata.h"
#include "storage/block_device.h"
#include "fs/fat32/fat32.h"

void kmain(void)
{
    uint16_t kernel_code_selector = 0x08;

    init_interrupts(kernel_code_selector);


    /*
     * Initialize ATA.
     */

    if (!ata_initialize()) {
        angrylog("ATA INITIALIZATION FAILED");
        return;
    }

    happylog("ATA INITIALIZATION SUCCESSFUL");

    uint8_t sector[512];

    if (!ata_read(0, 1, sector)) {

        angrylog("ATA SECTOR 0 READ FAILED");

        printv2(
            "STATUS: *i ERROR: *i\n",
            0x07,
            ata_get_last_status(),
            ata_get_last_error()
        );

        return;
    }

    happylog("SECTOR 0 READ SUCCESSFULLY");

    block_device_t disk = {
        .read = ata_read,
        .write = ata_write,
        .sector_size = ATA_SECTOR_SIZE
    };

    fat32_t fs;

    fat32_result_t mresult = fat32_mount(&fs, &disk);

    if (mresult != FAT32_OK) {
        angrylog("FAT32 MOUNT FAILED");

        switch (mresult) {
            case FAT32_IO_ERROR:
                angrylog("IO ERROR");
                break;

            case FAT32_INVALID_BOOT_SECTOR:
                angrylog("INVALID BOOT SECTOR");
                break;

            case FAT32_NOT_FAT32:
                angrylog("NOT FAT32");
                break;

            case FAT32_INVALID_BPB:
                angrylog("INVALID BPB");
                break;

            default:
                angrylog("UNKNOWN FAT32 ERROR");
                break;
        }

        return;
    }

    happylog("FAT32 MOUNT SUCCESSFUL");


    uint32_t root_lba =
        fat32_cluster_to_lba(
            &fs,
            fs.root_cluster
        );

    printv2(
        "ROOT LBA: *i\n",
        0x07,
        root_lba
    );

    uint8_t root_sector[512];

    if (!disk.read(
        root_lba,
        1,
        root_sector
    )) {
        angrylog("FAILED TO READ ROOT DIRECTORY");
        return;
    }

    happylog("ROOT DIRECTORY READ SUCCESSFULLY");
    
    /////////////////////////////////////

    for (int i = 0x0; i < 0x0F; i++) {
        putc('#', i);
    }

    print("\n", 0x07);

    happylog("Happy log!");
    normallog("Normal log!");
    sadlog("Sad log!");
    angrylog("Angry log!");

    sleep(SEC(1));

    happylog("Booting into imOS!");

    sleep(MS(500));

    clear_screen(0x07);

    printv2("Hello from imOS!\n", 0x07);


    uint8_t write_buffer[512];
    uint8_t read_buffer[512];

    for (uint32_t i = 0; i < 512; i++)
        write_buffer[i] = 'A';

    if (!ata_write(100000, 1, write_buffer)) {

        angrylog("ATA WRITE FAILED");

        printv2(
            "STATUS: *i ERROR: *i\n",
            0x07,
            ata_get_last_status(),
            ata_get_last_error()
        );

        return;
    }

    happylog("ATA WRITE SUCCESSFUL");

    if (!ata_read(100000, 1, read_buffer)) {

        angrylog("ATA READ AFTER WRITE FAILED");
        return;
    }

    bool correct = true;

    for (uint32_t i = 0; i < 512; i++) {

        if (read_buffer[i] != 'A') {
            correct = false;
            break;
        }
    }

    if (correct)
        happylog("ATA WRITE/READ TEST PASSED");
    else
        angrylog("ATA WRITE/READ DATA MISMATCH");

    shell_poweron(&fs);

    for (;;) {
        __asm__ volatile("hlt");
    }
}