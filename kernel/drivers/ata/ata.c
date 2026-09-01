#include "ata.h"
#include "../../lib/io.h"

#define ATA_PRIMARY_BASE       0x1F0
#define ATA_PRIMARY_CONTROL    0x3F6

#define ATA_DATA               (ATA_PRIMARY_BASE + 0)
#define ATA_ERROR              (ATA_PRIMARY_BASE + 1)
#define ATA_FEATURES           (ATA_PRIMARY_BASE + 1)
#define ATA_SECTOR_COUNT       (ATA_PRIMARY_BASE + 2)
#define ATA_LBA_LOW            (ATA_PRIMARY_BASE + 3)
#define ATA_LBA_MID            (ATA_PRIMARY_BASE + 4)
#define ATA_LBA_HIGH           (ATA_PRIMARY_BASE + 5)
#define ATA_DRIVE              (ATA_PRIMARY_BASE + 6)
#define ATA_STATUS             (ATA_PRIMARY_BASE + 7)
#define ATA_COMMAND            (ATA_PRIMARY_BASE + 7)

#define ATA_CMD_READ_SECTORS   0x20
#define ATA_CMD_WRITE_SECTORS  0x30
#define ATA_CMD_IDENTIFY       0xEC

#define ATA_STATUS_ERR         0x01
#define ATA_STATUS_DRQ         0x08
#define ATA_STATUS_DF          0x20
#define ATA_STATUS_RDY         0x40
#define ATA_STATUS_BSY         0x80

static bool initialized = false;

static uint8_t ata_last_status = 0;
static uint8_t ata_last_error = 0;

static void ata_io_wait(void)
{
    inb(ATA_PRIMARY_CONTROL);
    inb(ATA_PRIMARY_CONTROL);
    inb(ATA_PRIMARY_CONTROL);
    inb(ATA_PRIMARY_CONTROL);
}


static bool ata_wait_bsy(void)
{
    for (uint32_t i = 0; i < 1000000; i++) {

        uint8_t status = inb(ATA_STATUS);

        if (!(status & ATA_STATUS_BSY))
            return true;
    }

    return false;
}


static bool ata_wait_drq(void)
{
    for (uint32_t i = 0; i < 1000000; i++) {

        uint8_t status = inb(ATA_STATUS);

        if (status & ATA_STATUS_ERR) {
            ata_last_status = status;
            ata_last_error = inb(ATA_ERROR);
            return false;
        }

        if (status & ATA_STATUS_DF) {
            ata_last_status = status;
            return false;
        }

        if (!(status & ATA_STATUS_BSY) &&
            (status & ATA_STATUS_DRQ)) {

            return true;
        }
    }

    ata_last_status = inb(ATA_STATUS);

    return false;
}


static void ata_select_master(void)
{
    outb(ATA_DRIVE, 0xE0);
    ata_io_wait();
}


int ata_identify(void)
{
    ata_select_master();

    outb(ATA_SECTOR_COUNT, 0);
    outb(ATA_LBA_LOW, 0);
    outb(ATA_LBA_MID, 0);
    outb(ATA_LBA_HIGH, 0);

    outb(ATA_COMMAND, ATA_CMD_IDENTIFY);

    uint8_t status = inb(ATA_STATUS);

    if (status == 0)
        return 1;

    if (!ata_wait_bsy())
        return 2;

    uint8_t mid = inb(ATA_LBA_MID);
    uint8_t high = inb(ATA_LBA_HIGH);

    if (mid != 0 || high != 0)
        return 3;

    if (!ata_wait_drq())
        return 4;

    /*
     * Consume IDENTIFY data.
     */
    for (uint32_t i = 0; i < 256; i++)
        inw(ATA_DATA);

    return 0;
}


static bool ata_read_sector(
    uint32_t lba,
    void* buffer
)
{
    /*
     * Select primary master in LBA28 mode.
     */
    outb(
        ATA_DRIVE,
        0xE0 | ((lba >> 24) & 0x0F)
    );

    ata_io_wait();

    uint8_t select_status = inb(ATA_STATUS);

    /*
     * Wait until the device is ready.
     */
    if (!ata_wait_bsy())
        return false;

    /*
     * One sector.
     */
    outb(ATA_SECTOR_COUNT, 1);

    /*
     * LBA bits 0-23.
     */
    outb(
        ATA_LBA_LOW,
        (uint8_t)(lba & 0xFF)
    );

    outb(
        ATA_LBA_MID,
        (uint8_t)((lba >> 8) & 0xFF)
    );

    outb(
        ATA_LBA_HIGH,
        (uint8_t)((lba >> 16) & 0xFF)
    );

    /*
     * Issue READ SECTORS.
     */
    outb(
        ATA_COMMAND,
        ATA_CMD_READ_SECTORS
    );

    /*
     * Wait for BSY to clear.
     */
    if (!ata_wait_bsy())
        return false;

    /*
     * Read status after command.
     */
    uint8_t status = inb(ATA_STATUS);

    ata_last_status = status;

    if (status & ATA_STATUS_ERR) {
        ata_last_error = inb(ATA_ERROR);
        return false;
    }

    if (status & ATA_STATUS_DF)
        return false;

    /*
     * Wait for DRQ.
     */
    if (!(status & ATA_STATUS_DRQ)) {

        if (!ata_wait_drq())
            return false;
    }

    /*
     * Read 512 bytes.
     */
    uint16_t* output =
        (uint16_t*)buffer;

    for (uint32_t i = 0; i < 256; i++)
        output[i] = inw(ATA_DATA);

    return true;
}


bool ata_initialize(void)
{
    int result = ata_identify();

    if (result != 0)
        return false;

    initialized = true;

    return true;
}


bool ata_read(
    uint64_t lba,
    uint32_t count,
    void* buffer
)
{
    if (!initialized)
        return false;

    if (buffer == 0)
        return false;

    if (lba > 0x0FFFFFFF)
        return false;

    if (count == 0)
        return true;

    uint8_t* output =
        (uint8_t*)buffer;

    for (uint32_t i = 0; i < count; i++) {

        if (lba + i > 0x0FFFFFFF)
            return false;

        if (!ata_read_sector(
            (uint32_t)(lba + i),
            output + (i * ATA_SECTOR_SIZE)
        )) {
            return false;
        }
    }

    return true;
}


static bool ata_write_sector(
    uint32_t lba,
    const void* buffer
)
{
    if (buffer == 0)
        return false;

    /*
     * Select primary master, LBA28 mode.
     */
    outb(
        ATA_DRIVE,
        0xE0 | ((lba >> 24) & 0x0F)
    );

    ata_io_wait();

    /*
     * Wait for the drive to become ready.
     */
    if (!ata_wait_bsy())
        return false;

    /*
     * Write exactly one sector.
     */
    outb(ATA_SECTOR_COUNT, 1);

    outb(
        ATA_LBA_LOW,
        (uint8_t)(lba & 0xFF)
    );

    outb(
        ATA_LBA_MID,
        (uint8_t)((lba >> 8) & 0xFF)
    );

    outb(
        ATA_LBA_HIGH,
        (uint8_t)((lba >> 16) & 0xFF)
    );

    /*
     * WRITE SECTORS.
     */
    outb(
        ATA_COMMAND,
        ATA_CMD_WRITE_SECTORS
    );

    /*
     * Wait until the device requests data.
     */
    if (!ata_wait_drq())
        return false;

    /*
     * Send 512 bytes = 256 words.
     */
    const uint16_t* input =
        (const uint16_t*)buffer;

    for (uint32_t i = 0; i < 256; i++)
        outw(ATA_DATA, input[i]);

    /*
     * Wait for the write to finish.
     */
    if (!ata_wait_bsy())
        return false;

    /*
     * Check for errors.
     */
    uint8_t status = inb(ATA_STATUS);

    ata_last_status = status;

    if (status & ATA_STATUS_ERR) {
        ata_last_error = inb(ATA_ERROR);
        return false;
    }

    if (status & ATA_STATUS_DF)
        return false;

    return true;
}

bool ata_write(
    uint64_t lba,
    uint32_t count,
    const void* buffer
)
{
    if (!initialized)
        return false;

    if (buffer == 0)
        return false;

    if (lba > 0x0FFFFFFF)
        return false;

    if (count == 0)
        return true;

    const uint8_t* input =
        (const uint8_t*)buffer;

    for (uint32_t i = 0; i < count; i++) {

        if (lba + i > 0x0FFFFFFF)
            return false;

        if (!ata_write_sector(
                (uint32_t)(lba + i),
                input + (i * ATA_SECTOR_SIZE)
            )) {
            return false;
        }
    }

    return true;
}

static block_device_t ata_block_device = {
    .read = ata_read,
    .write = ata_write,
    .sector_size = ATA_SECTOR_SIZE
};

block_device_t* ata_get_block_device(void)
{
    return &ata_block_device;
}

uint8_t ata_get_last_status(void)
{
    return ata_last_status;
}

uint8_t ata_get_last_error(void)
{
    return ata_last_error;
}