#pragma once

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    bool (*read)(
        uint64_t lba,
        uint32_t count,
        void* buffer
    );

    bool (*write)(
        uint64_t lba,
        uint32_t count,
        const void* buffer
    );

    uint32_t sector_size;
} block_device_t;