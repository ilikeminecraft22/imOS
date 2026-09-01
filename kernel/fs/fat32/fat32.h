#pragma once

#include <stdint.h>
#include <stdbool.h>

#include "../../storage/block_device.h"

#define FAT32_SECTOR_SIZE 512

#define FAT32_ATTR_READ_ONLY 0x01
#define FAT32_ATTR_HIDDEN    0x02
#define FAT32_ATTR_SYSTEM    0x04
#define FAT32_ATTR_VOLUME_ID 0x08
#define FAT32_ATTR_DIRECTORY 0x10
#define FAT32_ATTR_ARCHIVE   0x20
#define FAT32_ATTR_LFN       0x0F

#define FAT32_LFN_SEQUENCE_LAST 0x40

typedef enum {
    FAT32_OK = 0,
    FAT32_ERROR,
    FAT32_IO_ERROR,
    FAT32_INVALID_BOOT_SECTOR,
    FAT32_NOT_FAT32,
    FAT32_INVALID_BPB,
    FAT32_NOT_FOUND,
    FAT32_NOT_A_FILE,
    FAT32_NOT_A_DIRECTORY,
    FAT32_END_OF_FILE
} fat32_result_t;

typedef struct {
    block_device_t* device;

    uint32_t bytes_per_sector;
    uint32_t sectors_per_cluster;

    uint32_t reserved_sectors;
    uint32_t number_of_fats;
    uint32_t fat_size;

    uint32_t total_sectors;
    uint32_t root_cluster;

    uint32_t fat_start;
    uint32_t data_start;

    uint32_t total_clusters;
} fat32_t;

typedef struct {
    fat32_t* fs;

    uint32_t first_cluster;
    uint32_t size;
    uint32_t position;

    uint32_t directory_cluster;
    uint32_t directory_entry_index;

    bool directory;
} fat32_file_t;

typedef struct {
    fat32_t* fs;

    uint32_t cluster;
    uint32_t sector_in_cluster;
    uint32_t entry_in_sector;

    bool finished;

    char lfn_name[256];
    bool has_lfn;
} fat32_dir_t;

fat32_result_t fat32_mount(
    fat32_t* fs,
    block_device_t* device
);

fat32_result_t fat32_open(
    fat32_t* fs,
    const char* path,
    fat32_file_t* file
);

fat32_result_t fat32_read(
    fat32_file_t* file,
    void* buffer,
    uint32_t size,
    uint32_t* bytes_read
);

// fat32_result_t fat32_write(
//     fat32_file_t* file,
//     const void* buffer,
//     uint32_t size,
//     uint32_t* bytes_written
// );

fat32_result_t fat32_opendir(
    fat32_t* fs,
    const char* path,
    fat32_dir_t* dir
);

fat32_result_t fat32_readdir(
    fat32_dir_t* dir,
    char* name,
    uint32_t name_size,
    bool* is_directory,
    uint32_t* size
);

uint32_t fat32_cluster_to_lba(
    fat32_t* fs,
    uint32_t cluster
);

fat32_result_t fat32_read_fat_entry(
    fat32_t* fs,
    uint32_t cluster,
    uint32_t* value
);

fat32_result_t fat32_next_cluster(
    fat32_t* fs,
    uint32_t cluster,
    uint32_t* next
);