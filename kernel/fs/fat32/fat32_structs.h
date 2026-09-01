#pragma once

#include <stdint.h>

namespace fat32 {

constexpr uint8_t ATTR_READ_ONLY = 0x01;
constexpr uint8_t ATTR_HIDDEN    = 0x02;
constexpr uint8_t ATTR_SYSTEM    = 0x04;
constexpr uint8_t ATTR_VOLUME_ID = 0x08;
constexpr uint8_t ATTR_DIRECTORY = 0x10;
constexpr uint8_t ATTR_ARCHIVE   = 0x20;
constexpr uint8_t ATTR_LFN       = 0x0F;

constexpr uint32_t END_OF_CHAIN = 0x0FFFFFF8;

struct DirectoryEntry {
    uint8_t  name[11];
    uint8_t  attributes;
    uint8_t  reserved;
    uint8_t  creation_time_tenths;

    uint16_t creation_time;
    uint16_t creation_date;
    uint16_t last_access_date;

    uint16_t first_cluster_high;

    uint16_t write_time;
    uint16_t write_date;

    uint16_t first_cluster_low;

    uint32_t file_size;
};

static_assert(sizeof(DirectoryEntry) == 32);

}