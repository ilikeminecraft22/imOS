#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "../../storage/block_device.h"

#define ATA_SECTOR_SIZE 512

#ifdef __cplusplus
extern "C" {
#endif

uint8_t ata_get_last_status(void);
uint8_t ata_get_last_error(void);

bool ata_initialize(void);

bool ata_read(
    uint64_t lba,
    uint32_t count,
    void* buffer
);

bool ata_write( 
    uint64_t lba,
    uint32_t count,
    const void* buffer
);

int ata_identify(void);

block_device_t* ata_get_block_device(void);

#ifdef __cplusplus
}
#endif