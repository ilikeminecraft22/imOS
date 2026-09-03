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

static bool fat32_test_result(
    const char* name,
    fat32_result_t actual,
    fat32_result_t expected
)
{
    if (actual != expected) {
        printv2(
            "FAIL: *s (got *i, expected *i)\n",
            0x07,
            name,
            actual,
            expected
        );

        return false;
    }

    printv2(
        "PASS: *s\n",
        0x07,
        name
    );

    return true;
}

static bool fat32_test_bool(
    const char* name,
    bool value
)
{
    if (!value) {
        printv2(
            "FAIL: *s\n",
            0x07,
            name
        );

        return false;
    }

    printv2(
        "PASS: *s\n",
        0x07,
        name
    );

    return true;
}

void kmain(void)
{
    uint16_t kernel_code_selector = 0x08;

    init_interrupts(
        kernel_code_selector
    );

    /*
     * Initialize ATA.
     */
    if (!ata_initialize()) {

        angrylog("ATA INITIALIZATION FAILED");

        printv2(
            "STATUS: *i ERROR: *i\n",
            0x07,
            ata_get_last_status(),
            ata_get_last_error()
        );

        return;
    }

    happylog("ATA INITIALIZATION SUCCESSFUL");

    block_device_t* device =
        ata_get_block_device();

    /*
     * Mount FAT32 filesystem.
     */
    fat32_t fs;

    fat32_result_t mresult =
        fat32_mount(
            &fs,
            device
        );

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

    /*
     * Show filesystem geometry.
     */
    uint32_t cluster_size =
        fs.bytes_per_sector *
        fs.sectors_per_cluster;

    printv2(
        "Bytes per sector: *i\n",
        0x07,
        fs.bytes_per_sector
    );

    printv2(
        "Sectors per cluster: *i\n",
        0x07,
        fs.sectors_per_cluster
    );

    printv2(
        "Cluster size: *i bytes\n",
        0x07,
        cluster_size
    );

    /*
     * Start the shell.
     */
    shell_poweron(&fs);

    /*
     * Keep the kernel alive.
     */
    for (;;) {

        __asm__ volatile("hlt");
    }
}