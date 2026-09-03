#include "fat32.h"
#include "../../lib/stdcon.h"
#include <stdint.h>
#include <stdbool.h>

static uint8_t sector_buffer[FAT32_SECTOR_SIZE];

typedef struct __attribute__((packed)) {
    uint8_t jump[3];
    uint8_t oem_name[8];

    uint16_t bytes_per_sector;
    uint8_t sectors_per_cluster;
    uint16_t reserved_sectors;
    uint8_t number_of_fats;
    uint16_t root_entry_count;

    uint16_t total_sectors_16;
    uint8_t media;
    uint16_t fat_size_16;

    uint16_t sectors_per_track;
    uint16_t number_of_heads;
    uint32_t hidden_sectors;

    uint32_t total_sectors_32;

    uint32_t fat_size_32;
    uint16_t ext_flags;
    uint16_t fs_version;

    uint32_t root_cluster;

    uint16_t fs_info;
    uint16_t backup_boot_sector;

    uint8_t reserved[12];

    uint8_t drive_number;
    uint8_t reserved1;
    uint8_t boot_signature;

    uint32_t volume_id;

    uint8_t volume_label[11];
    uint8_t filesystem_type[8];
} fat32_bpb_t;


typedef struct __attribute__((packed)) {
    uint8_t name[11];
    uint8_t attributes;
    uint8_t reserved;
    uint8_t creation_time_tenths;

    uint16_t creation_time;
    uint16_t creation_date;
    uint16_t last_access_date;

    uint16_t first_cluster_high;

    uint16_t write_time;
    uint16_t write_date;

    uint16_t first_cluster_low;

    uint32_t file_size;
} fat32_directory_entry_t;

typedef struct __attribute__((packed)) {
    uint8_t sequence;

    uint16_t name1[5];

    uint8_t attributes;
    uint8_t type;
    uint8_t checksum;

    uint16_t name2[6];

    uint16_t first_cluster;

    uint16_t name3[2];
} fat32_lfn_entry_t;

typedef struct {
    uint32_t cluster;
    uint32_t size;
    bool directory;

    uint32_t directory_cluster;
    uint32_t directory_entry_index;
} fat32_path_result_t;

static void clear_string(
    char* string,
    uint32_t size
)
{
    for (uint32_t i = 0; i < size; i++)
        string[i] = '\0';
}

static bool string_equals_ignore_case(
    const char* a,
    const char* b
)
{
    uint32_t i = 0;

    while (a[i] != '\0' &&
           b[i] != '\0') {

        char ca = a[i];
        char cb = b[i];

        if (ca >= 'A' && ca <= 'Z')
            ca += 'a' - 'A';

        if (cb >= 'A' && cb <= 'Z')
            cb += 'a' - 'A';

        if (ca != cb)
            return false;

        i++;
    }

    return a[i] == '\0' &&
           b[i] == '\0';
}

fat32_result_t fat32_split_parent_path(
    const char* path,
    char* parent_path,
    uint32_t parent_size,
    char* name,
    uint32_t name_size
)
{
    if (path == 0 ||
        parent_path == 0 ||
        name == 0)
        return FAT32_ERROR;

    if (parent_size == 0 ||
        name_size == 0)
        return FAT32_ERROR;

    uint32_t length = 0;

    while (path[length] != '\0') {
        length++;

        if (length >= 256)
            return FAT32_ERROR;
    }

    if (length == 0)
        return FAT32_ERROR;

    /*
     * Remove trailing slashes.
     */
    while (length > 1 &&
           path[length - 1] == '/') {
        length--;
    }

    /*
     * Find the final slash.
     */
    uint32_t slash = 0xFFFFFFFF;

    for (uint32_t i = 0; i < length; i++) {
        if (path[i] == '/')
            slash = i;
    }

    /*
     * No slash means there is no
     * valid parent path.
     */
    if (slash == 0xFFFFFFFF)
        return FAT32_ERROR;

    uint32_t name_length =
        length - slash - 1;

    if (name_length == 0 ||
        name_length >= name_size)
        return FAT32_ERROR;

    /*
     * Copy filename.
     */
    for (uint32_t i = 0;
         i < name_length;
         i++) {

        name[i] =
            path[slash + 1 + i];
    }

    name[name_length] = '\0';

    /*
     * Root directory.
     */
    if (slash == 0) {

        if (parent_size < 2)
            return FAT32_ERROR;

        parent_path[0] = '/';
        parent_path[1] = '\0';

        return FAT32_OK;
    }

    if (slash + 1 >= parent_size)
        return FAT32_ERROR;

    for (uint32_t i = 0;
         i < slash;
         i++) {

        parent_path[i] = path[i];
    }

    parent_path[slash] = '\0';

    return FAT32_OK;
}

static void make_83_display_name(
    const uint8_t name[11],
    char* output,
    uint32_t output_size
)
{
    if (output_size == 0)
        return;

    uint32_t pos = 0;

    /*
     * Filename.
     */
    for (uint32_t i = 0; i < 8; i++) {

        if (name[i] == ' ')
            break;

        if (pos >= output_size - 1)
            break;

        output[pos++] = (char)name[i];
    }

    /*
     * Extension.
     */
    bool has_extension = false;

    for (uint32_t i = 8; i < 11; i++) {
        if (name[i] != ' ') {
            has_extension = true;
            break;
        }
    }

    if (has_extension &&
        pos < output_size - 1) {

        output[pos++] = '.';

        for (uint32_t i = 8; i < 11; i++) {

            if (name[i] == ' ')
                break;

            if (pos >= output_size - 1)
                break;

            output[pos++] = (char)name[i];
        }
    }

    output[pos] = '\0';
}

static uint32_t lfn_sequence(
    const uint8_t* entry
)
{
    return entry[0] & 0x1F;
}

static void lfn_write_chars(
    char* output,
    uint32_t output_size,
    const uint8_t* entry
)
{
    uint32_t sequence =
        lfn_sequence(entry);

    if (sequence == 0)
        return;

    uint32_t base =
        (sequence - 1) * 13;

    const uint32_t offsets[] = {
        1, 3, 5, 7, 9,
        14, 16, 18, 20, 22, 24,
        28, 30
    };

    for (uint32_t i = 0; i < 13; i++) {

        uint16_t c =
            ((uint16_t)entry[offsets[i]]) |
            ((uint16_t)entry[offsets[i] + 1] << 8);

        if (c == 0x0000 ||
            c == 0xFFFF)
            continue;

        if (base + i >= output_size - 1)
            continue;

        /*
         * For now, support ASCII cleanly.
         * Non-ASCII becomes '?'.
         */
        if (c <= 0x7F)
            output[base + i] = (char)c;
        else
            output[base + i] = '?';
    }
}

static uint32_t string_length(
    const char* string
)
{
    uint32_t length = 0;

    while (string[length] != '\0')
        length++;

    return length;
}

static uint16_t read_entry_u16(
    const uint8_t* data
)
{
    return
        ((uint16_t)data[0]) |
        ((uint16_t)data[1] << 8);
}

static uint32_t read_u32(const uint8_t* data)
{
    return
        ((uint32_t)data[0]) |
        ((uint32_t)data[1] << 8) |
        ((uint32_t)data[2] << 16) |
        ((uint32_t)data[3] << 24);
}

static void write_u32(
    uint8_t* p,
    uint32_t value
)
{
    p[0] = (uint8_t)(value & 0xFF);
    p[1] = (uint8_t)((value >> 8) & 0xFF);
    p[2] = (uint8_t)((value >> 16) & 0xFF);
    p[3] = (uint8_t)((value >> 24) & 0xFF);
}

static uint16_t read_u16(
    const uint8_t* p
)
{
    return
        (uint16_t)p[0] |
        ((uint16_t)p[1] << 8);
}

static void write_u16(
    uint8_t* p,
    uint16_t value
)
{
    p[0] = (uint8_t)(value & 0xFF);
    p[1] = (uint8_t)((value >> 8) & 0xFF);
}

static bool is_power_of_two(uint32_t value)
{
    return value != 0 &&
           (value & (value - 1)) == 0;
}

static fat32_result_t fat32_update_file_entry(
    fat32_file_t* file
)
{
    printv2(
        "ENTER update_file_entry\n",
        0x07
    );

    if (file == 0 ||
        file->fs == 0)
    {
        return FAT32_ERROR;
    }

    printv2(
        "if (file==0 ||...\n",
        0x07
    );

    if (file->directory)
        return FAT32_NOT_A_FILE;

    printv2(
        "checked if its not a file\n",
        0x07
    );

    fat32_t* fs = file->fs;

    if (fs->device == 0 ||
        fs->bytes_per_sector == 0 ||
        fs->sectors_per_cluster == 0)
    {
        return FAT32_ERROR;
    }

    printv2(
        "weird line idk what it means some zeros\n",
        0x07
    );

    if (file->directory_cluster < 2)
        return FAT32_ERROR;

    printv2(
        "directory cluster too small (<2) :O\n",
        0x07
    );

    /*
     * A FAT32 directory entry is 32 bytes.
     */
    uint32_t entries_per_sector =
        fs->bytes_per_sector / 32;

    if (entries_per_sector == 0)
        return FAT32_ERROR;

    printv2(
        "entries per sector == 0\n",
        0x07
    );

    uint32_t sector_index =
        file->directory_entry_index /
        entries_per_sector;

    uint32_t entry_in_sector =
        file->directory_entry_index %
        entries_per_sector;

    /*
     * Find the cluster containing
     * the directory sector.
     */
    uint32_t cluster_index =
        sector_index /
        fs->sectors_per_cluster;

    uint32_t sector_in_cluster =
        sector_index %
        fs->sectors_per_cluster;

    fat32_file_t directory_file = {
        .fs = fs,
        .first_cluster = file->directory_cluster,
        .size = 0,
        .position = 0,
        .directory_cluster = 0,
        .directory_entry_index = 0,
        .directory = true
    };

    uint32_t directory_sector_cluster;

    printv2(
        "Directory cluster: *i\n",
        0x07,
        file->directory_cluster
    );

    printv2(
        "Directory entry index: *i\n",
        0x07,
        file->directory_entry_index
    );

    printv2(
        "Directory sector index: *i\n",
        0x07,
        sector_index
    );

    printv2(
        "Directory cluster index: *i\n",
        0x07,
        cluster_index
    );

    fat32_result_t result =
        fat32_get_cluster_at(
            fs,
            &directory_file,
            cluster_index,
            &directory_sector_cluster
        );

    if (result != FAT32_OK)
    {
        angrylog(
            "GET DIRECTORY CLUSTER FAILED"
        );

        printv2(
            "Result: *i\n",
            0x07,
            result
        );

        return result;
    }

    printv2(
        "Directory sector cluster: *i\n",
        0x07,
        directory_sector_cluster
    );

    if (result != FAT32_OK)
        return result;

    uint32_t directory_lba =
        fat32_cluster_to_lba(
            fs,
            directory_sector_cluster
        );

    if (directory_lba == 0)
        return FAT32_ERROR;

    uint32_t lba =
        directory_lba +
        sector_in_cluster;

    /*
     * Read the directory sector so we preserve
     * every field we aren't changing.
     */
    if (!fs->device->read(
        lba,
        1,
        sector_buffer
    ))
    {
        angrylog(
            "DIRECTORY SECTOR READ FAILED"
        );

        printv2(
            "LBA: *i\n",
            0x07,
            lba
        );

        return FAT32_IO_ERROR;
    }

    uint32_t entry_offset =
        entry_in_sector * 32;

    if (entry_offset + 32 >
        fs->bytes_per_sector)
    {
        return FAT32_ERROR;
    }

    /*
     * First cluster high word.
     * Directory entry offset 20.
     */
    write_u16(
        &sector_buffer[entry_offset + 20],
        (uint16_t)(
            file->first_cluster >> 16
        )
    );

    /*
     * First cluster low word.
     * Directory entry offset 26.
     */
    write_u16(
        &sector_buffer[entry_offset + 26],
        (uint16_t)(
            file->first_cluster & 0xFFFF
        )
    );

    /*
     * File size.
     * Directory entry offset 28.
     */
    write_u32(
        &sector_buffer[entry_offset + 28],
        file->size
    );

    /*
     * Write the directory sector back.
     */
    if (!fs->device->write(
        lba,
        1,
        sector_buffer
    ))
    {
        return FAT32_IO_ERROR;
    }

    return FAT32_OK;
}

fat32_result_t fat32_mount(
    fat32_t* fs,
    block_device_t* device
)
{
    if (fs == 0 || device == 0)
        return FAT32_ERROR;

    if (device->read == 0)
        return FAT32_ERROR;

    if (device->sector_size != FAT32_SECTOR_SIZE)
        return FAT32_INVALID_BPB;

    if (!device->read(0, 1, sector_buffer))
        return FAT32_IO_ERROR;

    if (sector_buffer[510] != 0x55 ||
        sector_buffer[511] != 0xAA)
        return FAT32_INVALID_BOOT_SECTOR;

    fat32_bpb_t* bpb =
        (fat32_bpb_t*)sector_buffer;

    if (bpb->bytes_per_sector != 512)
        return FAT32_INVALID_BPB;

    if (!is_power_of_two(
        bpb->sectors_per_cluster))
        return FAT32_INVALID_BPB;

    if (bpb->reserved_sectors == 0)
        return FAT32_INVALID_BPB;

    if (bpb->number_of_fats == 0)
        return FAT32_INVALID_BPB;
    
    if (bpb->root_entry_count != 0)
        return FAT32_NOT_FAT32;

    if (bpb->fat_size_32 == 0)
        return FAT32_NOT_FAT32;

    uint32_t total_sectors;

    if (bpb->total_sectors_16 != 0)
        total_sectors = bpb->total_sectors_16;
    else
        total_sectors = bpb->total_sectors_32;

    if (total_sectors == 0)
        return FAT32_INVALID_BPB;

    uint32_t fat_start =
        bpb->reserved_sectors;

    uint32_t data_start =
        fat_start +
        (bpb->number_of_fats * bpb->fat_size_32);

    if (data_start >= total_sectors)
        return FAT32_INVALID_BPB;

    uint32_t data_sectors =
        total_sectors - data_start;

    uint32_t total_clusters =
        data_sectors / bpb->sectors_per_cluster;

    if (total_clusters < 65525)
        return FAT32_NOT_FAT32;

    if (bpb->root_cluster < 2)
        return FAT32_INVALID_BPB;

    fs->device = device;

    fs->bytes_per_sector =
        bpb->bytes_per_sector;

    fs->sectors_per_cluster =
        bpb->sectors_per_cluster;

    fs->reserved_sectors =
        bpb->reserved_sectors;

    fs->number_of_fats =
        bpb->number_of_fats;

    fs->fat_size =
        bpb->fat_size_32;

    fs->total_sectors =
        total_sectors;

    fs->root_cluster =
        bpb->root_cluster;

    fs->fat_start =
        fat_start;

    fs->data_start =
        data_start;

    fs->total_clusters =
        total_clusters;

    return FAT32_OK;
}


uint32_t fat32_cluster_to_lba(

    fat32_t* fs,

    uint32_t cluster

)

{

    if (fs == 0)

        return 0;

    if (cluster < 2)

        return 0;

    uint32_t max_cluster =

        fs->total_clusters + 1;

    if (cluster > max_cluster)

        return 0;

    return

        fs->data_start +

        (uint32_t)(

            (uint64_t)(cluster - 2) *

            fs->sectors_per_cluster

        );

}


fat32_result_t fat32_read_fat_entry(

    fat32_t* fs,

    uint32_t cluster,

    uint32_t* value

)

{

    if (fs == 0 ||

        value == 0)

        return FAT32_ERROR;

    if (cluster < 2)

        return FAT32_ERROR;

    uint32_t max_cluster =

        fs->total_clusters + 1;

    if (cluster > max_cluster)

        return FAT32_ERROR;

    uint32_t fat_offset =

        cluster * 4;

    uint32_t fat_sector =

        fs->fat_start +

        (fat_offset /

         fs->bytes_per_sector);

    uint32_t offset =

        fat_offset %

        fs->bytes_per_sector;

    if (!fs->device->read(

        fat_sector,

        1,

        sector_buffer

    ))

        return FAT32_IO_ERROR;

    *value =

        read_u32(

            &sector_buffer[offset]

        ) & 0x0FFFFFFF;

    return FAT32_OK;

}

fat32_result_t fat32_write_fat_entry(
    fat32_t* fs,
    uint32_t cluster,
    uint32_t value
)
{
    if (fs == 0 ||
        fs->device == 0)
        return FAT32_ERROR;

    if (cluster < 2)
        return FAT32_ERROR;

    uint32_t max_cluster =
        fs->total_clusters + 1;

    if (cluster > max_cluster)
        return FAT32_ERROR;

    if (fs->bytes_per_sector == 0 ||
        fs->fat_size == 0 ||
        fs->number_of_fats == 0)
        return FAT32_ERROR;

    /*
     * FAT32 only uses the lower 28 bits.
     */
    value &= 0x0FFFFFFF;

    uint32_t fat_offset =
        cluster * 4;

    uint32_t sector_offset =
        fat_offset /
        fs->bytes_per_sector;

    uint32_t offset =
        fat_offset %
        fs->bytes_per_sector;

    /*
     * A FAT32 entry is 4 bytes. Make sure
     * it cannot cross the sector boundary.
     */
    if (offset + 4 >
        fs->bytes_per_sector)
        return FAT32_ERROR;

    uint32_t fat_sector =
        fs->fat_start +
        sector_offset;

    /*
     * Read the FAT sector once.
     */
    if (!fs->device->read(
        fat_sector,
        1,
        sector_buffer
    ))
        return FAT32_IO_ERROR;

    /*
     * Preserve the upper four reserved bits.
     */
    uint32_t old_value =
        read_u32(
            &sector_buffer[offset]
        );

    uint32_t new_value =
        (old_value & 0xF0000000) |
        value;

    write_u32(
        &sector_buffer[offset],
        new_value
    );

    /*
     * Write the modified sector to every FAT.
     */
    for (uint32_t fat = 0;
         fat < fs->number_of_fats;
         fat++) {

        uint32_t copy_sector =
            fs->fat_start +
            (fat * fs->fat_size) +
            sector_offset;

        if (!fs->device->write(
            copy_sector,
            1,
            sector_buffer
        ))
            return FAT32_IO_ERROR;
    }

    return FAT32_OK;
}

static fat32_result_t fat32_zero_cluster(
    fat32_t* fs,
    uint32_t cluster
)
{
    if (fs == 0 ||
        fs->device == 0)
        return FAT32_ERROR;

    if (cluster < 2 ||
        cluster > fs->total_clusters + 1)
        return FAT32_ERROR;

    static uint8_t zero_buffer[FAT32_SECTOR_SIZE];

    for (uint32_t i = 0;
         i < fs->sectors_per_cluster;
         i++) {

        uint32_t lba =
            fat32_cluster_to_lba(
                fs,
                cluster
            );

        if (lba == 0)
            return FAT32_ERROR;

        lba += i;

        if (!fs->device->write(
            lba,
            1,
            zero_buffer
        ))
            return FAT32_IO_ERROR;
    }

    return FAT32_OK;
}

fat32_result_t fat32_next_cluster(

    fat32_t* fs,

    uint32_t cluster,

    uint32_t* next

)

{

    if (fs == 0 ||

        next == 0)

        return FAT32_ERROR;

    if (cluster < 2)

        return FAT32_ERROR;

    uint32_t max_cluster =

        fs->total_clusters + 1;

    if (cluster > max_cluster)

        return FAT32_ERROR;

    uint32_t value;

    fat32_result_t result =

        fat32_read_fat_entry(

            fs,

            cluster,

            &value

        );

    if (result != FAT32_OK)

        return result;

    /*
     * FAT32 uses only the lower 28 bits.
     */

    value &= 0x0FFFFFFF;

    /*
     * End of cluster chain.
     */

    if (value >= 0x0FFFFFF8) {

        *next = 0;

        return FAT32_END_OF_FILE;

    }

    /*
     * Bad cluster.
     */

    if (value == 0x0FFFFFF7)

        return FAT32_ERROR;

    /*
     * Free, reserved, or invalid cluster.
     */

    if (value < 2)

        return FAT32_ERROR;

    if (value > max_cluster)

        return FAT32_ERROR;

    *next = value;

    return FAT32_OK;

}

static fat32_result_t fat32_extend_file_chain(
    fat32_file_t* file,
    uint32_t* new_cluster
)
{
    if (file == 0 ||
        new_cluster == 0)
    {
        return FAT32_ERROR;
    }

    if (file->fs == 0)
        return FAT32_ERROR;

    if (file->directory)
        return FAT32_NOT_A_FILE;

    fat32_t* fs = file->fs;

    /*
     * The file must already have a first
     * cluster. Zero-length files will need
     * special handling later when we implement
     * writing to an empty file.
     */
    if (file->first_cluster < 2)
        return FAT32_ERROR;

    /*
     * Find the last cluster in the existing
     * FAT chain.
     */
    uint32_t current =
        file->first_cluster;

    for (;;) {

        uint32_t next;

        fat32_result_t result =
            fat32_next_cluster(
                fs,
                current,
                &next
            );

        if (result == FAT32_END_OF_FILE)
            break;

        if (result != FAT32_OK)
            return result;

        current = next;
    }

    /*
     * Allocate a new cluster.
     *
     * fat32_allocate_cluster() marks the
     * new cluster as EOC and zeroes it.
     */
    uint32_t allocated;

    fat32_result_t result =
        fat32_allocate_cluster(
            fs,
            &allocated
        );

    if (result != FAT32_OK)
        return result;

    /*
     * Link the old last cluster to the
     * newly allocated cluster.
     */
    result =
        fat32_write_fat_entry(
            fs,
            current,
            allocated
        );

    if (result != FAT32_OK) {

        /*
         * Allocation succeeded but linking
         * failed, so release the new cluster.
         */
        fat32_write_fat_entry(
            fs,
            allocated,
            0
        );

        return result;
    }

    *new_cluster = allocated;

    return FAT32_OK;
}

static fat32_result_t fat32_get_cluster_at(

    fat32_t* fs,

    fat32_file_t* file,

    uint32_t cluster_index,

    uint32_t* cluster

)

{

    if (fs == 0 ||

        file == 0 ||

        cluster == 0)

        return FAT32_ERROR;

    if (file->first_cluster < 2)

        return FAT32_ERROR;

    uint32_t current =

        file->first_cluster;

    /*
     * A valid chain cannot contain more clusters
     * than exist in the filesystem.
     */

    if (cluster_index >=

        fs->total_clusters)

        return FAT32_ERROR;

    for (

        uint32_t i = 0;

        i < cluster_index;

        i++

    ) {

        uint32_t next;

        fat32_result_t result =

            fat32_next_cluster(

                fs,

                current,

                &next

            );

        if (result != FAT32_OK)

            return result;

        current = next;

    }

    *cluster = current;

    return FAT32_OK;

}

fat32_result_t fat32_allocate_cluster(
    fat32_t* fs,
    uint32_t* cluster
)
{
    if (fs == 0 ||
        fs->device == 0 ||
        cluster == 0)
        return FAT32_ERROR;

    if (fs->total_clusters == 0)
        return FAT32_ERROR;

    uint32_t max_cluster =
        fs->total_clusters + 1;

    /*
     * Cluster 0 and 1 are reserved.
     * Start searching at cluster 2.
     */
    for (uint32_t current = 2;
         current <= max_cluster;
         current++) {

        uint32_t value;

        fat32_result_t result =
            fat32_read_fat_entry(
                fs,
                current,
                &value
            );

        if (result != FAT32_OK)
            return result;

        /*
         * A zero FAT entry means the cluster
         * is free.
         */
        if (value != 0)
            continue;

        /*
         * Mark the cluster as the end of a
         * cluster chain.
         */
        result =
            fat32_write_fat_entry(
                fs,
                current,
                0x0FFFFFFF
            );

        if (result != FAT32_OK)
            return result;

        /*
         * Clear the cluster before returning it.
         */
        result =
            fat32_zero_cluster(
                fs,
                current
            );

        if (result != FAT32_OK) {

            /*
             * We failed after marking the cluster
             * allocated, so release it again.
             */
            fat32_write_fat_entry(
                fs,
                current,
                0
            );

            return result;
        }

        *cluster = current;

        return FAT32_OK;
    }

    /*
     * No free clusters.
     */
    return FAT32_ERROR;
}

static fat32_result_t fat32_append_cluster(
    fat32_t* fs,
    uint32_t current_cluster,
    uint32_t* new_cluster
)
{
    if (fs == 0 ||
        new_cluster == 0)
    {
        return FAT32_ERROR;
    }

    if (current_cluster < 2)
        return FAT32_ERROR;

    /*
     * Allocate a new cluster.
     */
    uint32_t allocated;

    fat32_result_t result =
        fat32_allocate_cluster(
            fs,
            &allocated
        );

    if (result != FAT32_OK)
        return result;

    /*
     * Link the current cluster to
     * the newly allocated cluster.
     */
    result =
        fat32_write_fat_entry(
            fs,
            current_cluster,
            allocated
        );

    if (result != FAT32_OK)
    {
        /*
         * Allocation succeeded but linking
         * failed, so release the new cluster.
         */
        fat32_write_fat_entry(
            fs,
            allocated,
            0
        );

        return result;
    }

    *new_cluster =
        allocated;

    return FAT32_OK;
}

/*
 * Convert an 8.3 filename such as:
 *
 *     HELLO.TXT
 *
 * into:
 *
 *     "HELLO   TXT"
 */
bool make_83_name(

    const char* input,

    uint8_t output[11]

)

{

    if (input == 0)

        return false;

    for (uint32_t i = 0; i < 11; i++)

        output[i] = ' ';

    uint32_t i = 0;

    uint32_t name_length = 0;

    while (

        input[i] != '\0' &&

        input[i] != '.'

    ) {

        if (name_length >= 8)

            return false;

        char c = input[i];

        if (c >= 'a' && c <= 'z')

            c -= 'a' - 'A';

        output[name_length] =

            (uint8_t)c;

        name_length++;

        i++;

    }

    if (input[i] == '\0')

        return true;

    /*
     * Skip the dot.
     */

    i++;

    uint32_t extension_length = 0;

    while (input[i] != '\0') {

        if (extension_length >= 3)

            return false;

        char c = input[i];

        if (c >= 'a' && c <= 'z')

            c -= 'a' - 'A';

        output[8 + extension_length] =

            (uint8_t)c;

        extension_length++;

        i++;

    }

    return true;

}


static bool names_equal(
    const uint8_t a[11],
    const uint8_t b[11]
)
{
    for (uint32_t i = 0; i < 11; i++) {
        if (a[i] != b[i])
            return false;
    }

    return true;
}


static uint32_t directory_entry_cluster(
    const fat32_directory_entry_t* entry
)
{
    return
        ((uint32_t)entry->first_cluster_high << 16) |
        entry->first_cluster_low;
}

fat32_result_t fat32_find_free_directory_entry(
    fat32_t* fs,
    uint32_t directory_cluster,
    uint32_t* entry_index
)
{
    if (fs == 0 ||
        fs->device == 0 ||
        entry_index == 0)
        return FAT32_ERROR;

    if (directory_cluster < 2 ||
        directory_cluster > fs->total_clusters + 1)
        return FAT32_ERROR;

    uint32_t cluster = directory_cluster;
    uint32_t global_entry_index = 0;

    uint32_t entries_per_sector =
        fs->bytes_per_sector / 32;

    if (entries_per_sector == 0)
        return FAT32_ERROR;

    for (;;) {

        for (uint32_t sector_in_cluster = 0;
             sector_in_cluster < fs->sectors_per_cluster;
             sector_in_cluster++) {

            uint32_t lba =
                fat32_cluster_to_lba(
                    fs,
                    cluster
                );

            if (lba == 0)
                return FAT32_ERROR;

            lba += sector_in_cluster;

            if (!fs->device->read(
                lba,
                1,
                sector_buffer
            ))
                return FAT32_IO_ERROR;

            for (uint32_t i = 0;
                 i < entries_per_sector;
                 i++) {

                uint8_t first_byte =
                    sector_buffer[i * 32];

                if (first_byte == 0x00 ||
                    first_byte == 0xE5) {

                    *entry_index =
                        global_entry_index + i;

                    return FAT32_OK;
                }
            }

            global_entry_index +=
                entries_per_sector;
        }

        uint32_t next;

        fat32_result_t result =
            fat32_next_cluster(
                fs,
                cluster,
                &next
            );

        if (result == FAT32_END_OF_FILE) {

            /*
             * No free entry exists in the
             * existing directory chain.
             *
             * We'll handle directory growth
             * later.
             */
            return FAT32_ERROR;
        }

        if (result != FAT32_OK)
            return result;

        cluster = next;
    }
}

static fat32_result_t fat32_write_directory_entry_data(
    fat32_t* fs,
    uint32_t directory_cluster,
    uint32_t entry_index,
    const uint8_t short_name[11],
    uint8_t attributes,
    uint32_t first_cluster,
    uint32_t size
)
{
    if (fs == 0 ||
        fs->device == 0 ||
        short_name == 0)
        return FAT32_ERROR;

    if (directory_cluster < 2 ||
        directory_cluster > fs->total_clusters + 1)
        return FAT32_ERROR;

    uint32_t entries_per_sector =
        fs->bytes_per_sector / 32;

    if (entries_per_sector == 0)
        return FAT32_ERROR;

    uint32_t sector_index =
        entry_index / entries_per_sector;

    uint32_t entry_in_sector =
        entry_index % entries_per_sector;

    uint32_t cluster_index =
        sector_index /
        fs->sectors_per_cluster;

    uint32_t sector_in_cluster =
        sector_index %
        fs->sectors_per_cluster;

    fat32_file_t directory;

    directory.fs = fs;
    directory.first_cluster =
        directory_cluster;
    directory.size = 0;
    directory.position = 0;
    directory.directory_cluster = 0;
    directory.directory_entry_index = 0;
    directory.directory = true;

    uint32_t cluster;

    fat32_result_t result =
        fat32_get_cluster_at(
            fs,
            &directory,
            cluster_index,
            &cluster
        );

    if (result != FAT32_OK)
        return result;

    uint32_t lba =
        fat32_cluster_to_lba(
            fs,
            cluster
        );

    if (lba == 0)
        return FAT32_ERROR;

    lba += sector_in_cluster;

    if (!fs->device->read(
        lba,
        1,
        sector_buffer
    ))
        return FAT32_IO_ERROR;

    uint32_t offset =
        entry_in_sector * 32;

    /*
     * Clear the entire directory entry.
     */
    for (uint32_t i = 0; i < 32; i++)
        sector_buffer[offset + i] = 0;

    /*
     * Short 8.3 name.
     */
    for (uint32_t i = 0; i < 11; i++)
        sector_buffer[offset + i] =
            short_name[i];

    /*
     * Attributes.
     */
    sector_buffer[offset + 11] =
        attributes;

    /*
     * First cluster.
     */
    write_u16(
        &sector_buffer[offset + 20],
        (uint16_t)(
            first_cluster >> 16
        )
    );

    write_u16(
        &sector_buffer[offset + 26],
        (uint16_t)(
            first_cluster & 0xFFFF
        )
    );

    /*
     * File size.
     */
    write_u32(
        &sector_buffer[offset + 28],
        size
    );

    if (!fs->device->write(
        lba,
        1,
        sector_buffer
    ))
        return FAT32_IO_ERROR;

    return FAT32_OK;
}

static fat32_result_t fat32_write_directory_entry(
    fat32_t* fs,
    uint32_t directory_cluster,
    uint32_t entry_index,
    const uint8_t short_name[11]
)
{
    return fat32_write_directory_entry_data(
        fs,
        directory_cluster,
        entry_index,
        short_name,
        FAT32_ATTR_ARCHIVE,
        0,
        0
    );
}

fat32_result_t fat32_find_in_directory(
    fat32_t* fs,
    uint32_t directory_cluster,
    const char* name,
    uint32_t* found_cluster,
    uint32_t* found_size,
    bool* is_directory,
    uint32_t* found_entry_index
)
{
    if (fs == 0 ||
        name == 0 ||
        found_cluster == 0 ||
        found_size == 0 ||
        is_directory == 0 ||
        found_entry_index == 0)
        return FAT32_ERROR;

    uint8_t target_name[11];
    bool target_is_83 =
        make_83_name(name, target_name);

    uint32_t cluster =
        directory_cluster;

    uint32_t entry_index = 0;

    /*
     * Enough for a 255-character FAT long filename.
     */
    char lfn_name[256];

    uint32_t lfn_length = 0;

    bool collecting_lfn = false;

    while (true) {

        uint32_t lba =
            fat32_cluster_to_lba(
                fs,
                cluster
            );

        for (
            uint32_t sector = 0;
            sector < fs->sectors_per_cluster;
            sector++
        ) {

            if (!fs->device->read(
                lba + sector,
                1,
                sector_buffer
            ))
                return FAT32_IO_ERROR;

            uint32_t entries =
                fs->bytes_per_sector / 32;

            for (
                uint32_t i = 0;
                i < entries;
                i++
            ) {
                uint8_t* raw =
                    &sector_buffer[i * 32];

                /*
                 * 0x00 = end of directory.
                 */
                if (raw[0] == 0x00)
                    return FAT32_NOT_FOUND;

                /*
                 * Deleted.
                 */
                if (raw[0] == 0xE5) {
                    collecting_lfn = false;
                    lfn_length = 0;
                    continue;
                }

                uint8_t attributes = raw[11];

                /*
                 * LFN entry.
                 */
                if (attributes == FAT32_ATTR_LFN) {

                    fat32_lfn_entry_t* lfn =
                        (fat32_lfn_entry_t*)raw;

                    uint8_t sequence =
                        lfn->sequence & 0x1F;

                    /*
                     * The last LFN entry appears first.
                     * Start a fresh name when we see it.
                     */
                    if (lfn->sequence &
                        FAT32_LFN_SEQUENCE_LAST) {

                        lfn_length = 0;
                        collecting_lfn = true;

                        for (
                            uint32_t j = 0;
                            j < sizeof(lfn_name);
                            j++
                        )
                            lfn_name[j] = '\0';
                    }

                    if (!collecting_lfn)
                        continue;

                    /*
                     * Determine where this fragment belongs.
                     *
                     * Sequence 1 is the beginning of the
                     * filename, but is stored closest to the
                     * normal directory entry.
                     */
                    uint32_t position =
                        (sequence - 1) * 13;

                    /*
                     * Name part 1.
                     */
                    for (uint32_t j = 0; j < 5; j++) {

                        uint16_t c =
                            read_entry_u16(
                                raw + 1 + (j * 2)
                            );

                        if (c == 0x0000 ||
                            c == 0xFFFF)
                            continue;

                        if (position + j <
                            sizeof(lfn_name) - 1) {

                            /*
                             * Basic ASCII support.
                             */
                            lfn_name[position + j] =
                                (c <= 0x7F)
                                    ? (char)c
                                    : '?';
                        }
                    }

                    /*
                     * Name part 2.
                     */
                    for (uint32_t j = 0; j < 6; j++) {

                        uint16_t c =
                            read_entry_u16(
                                raw + 14 + (j * 2)
                            );

                        if (c == 0x0000 ||
                            c == 0xFFFF)
                            continue;

                        if (position + 5 + j <
                            sizeof(lfn_name) - 1) {

                            lfn_name[position + 5 + j] =
                                (c <= 0x7F)
                                    ? (char)c
                                    : '?';
                        }
                    }

                    /*
                     * Name part 3.
                     */
                    for (uint32_t j = 0; j < 2; j++) {

                        uint16_t c =
                            read_entry_u16(
                                raw + 28 + (j * 2)
                            );

                        if (c == 0x0000 ||
                            c == 0xFFFF)
                            continue;

                        if (position + 11 + j <
                            sizeof(lfn_name) - 1) {

                            lfn_name[position + 11 + j] =
                                (c <= 0x7F)
                                    ? (char)c
                                    : '?';
                        }
                    }

                    /*
                     * Keep track of maximum used length.
                     */
                    uint32_t end =
                        position + 13;

                    if (end > lfn_length)
                        lfn_length = end;

                    continue;
                }

                /*
                 * Normal directory entry.
                 */
                fat32_directory_entry_t* entry =
                    (fat32_directory_entry_t*)raw;

                /*
                 * Ignore volume labels.
                 */
                if (attributes &
                    FAT32_ATTR_VOLUME_ID) {

                    collecting_lfn = false;
                    lfn_length = 0;
                    continue;
                }

                bool matches = false;

                /*
                 * Try the long filename first.
                 */
                if (collecting_lfn) {

                    lfn_name[sizeof(lfn_name) - 1] =
                        '\0';

                    uint32_t len = 0;

                    while (
                        len < sizeof(lfn_name) &&
                        lfn_name[len] != '\0'
                    )
                        len++;

                    /*
                     * Compare requested path component
                     * case-insensitively.
                     */
                    bool equal = true;

                    uint32_t j = 0;

                    while (
                        name[j] != '\0' &&
                        lfn_name[j] != '\0'
                    ) {
                        char a = name[j];
                        char b = lfn_name[j];

                        if (a >= 'A' && a <= 'Z')
                            a += 'a' - 'A';

                        if (b >= 'A' && b <= 'Z')
                            b += 'a' - 'A';

                        if (a != b) {
                            equal = false;
                            break;
                        }

                        j++;
                    }

                    if (equal &&
                        name[j] == '\0' &&
                        lfn_name[j] == '\0') {

                        matches = true;
                    }
                }

                /*
                 * If it wasn't an LFN match, try the
                 * normal 8.3 name.
                 */
                if (!matches && target_is_83) {

                    matches =
                        names_equal(
                            entry->name,
                            target_name
                        );
                }

                /*
                 * LFN sequence belongs to this entry.
                 * Clear it after examining the normal entry.
                 */
                collecting_lfn = false;
                lfn_length = 0;

                if (!matches)
                    continue;

                *found_cluster =
                    directory_entry_cluster(entry);

                *found_size =
                    read_u32(
                        (uint8_t*)&entry->file_size
                    );

                *is_directory =
                    (attributes &
                    FAT32_ATTR_DIRECTORY) != 0;

                *found_entry_index =
                    entry_index + i;

                return FAT32_OK;
            }
            entry_index += entries;
        }

        uint32_t next;

        fat32_result_t result =
            fat32_next_cluster(
                fs,
                cluster,
                &next
            );

        if (result == FAT32_END_OF_FILE)
            return FAT32_NOT_FOUND;

        if (result != FAT32_OK)
            return result;

        cluster = next;
    }
}

static fat32_result_t fat32_resolve_path(
    fat32_t* fs,
    const char* path,
    fat32_path_result_t* result
)
{
    if (fs == 0 ||
        path == 0 ||
        result == 0)
        return FAT32_ERROR;

    if (path[0] != '/')
        return FAT32_ERROR;

    /*
     * "/" refers to the root directory.
     */
    if (path[1] == '\0') {

        result->cluster = fs->root_cluster;
        result->size = 0;
        result->directory = true;

        return FAT32_OK;
    }

    uint32_t current_directory =
        fs->root_cluster;

    const char* current =
        path + 1;

    char component[256];

    while (true) {

        /*
         * Skip repeated slashes.
         *
         * This means paths like:
         *
         * /TEST//HELLO.TXT
         *
         * still work.
         */
        while (*current == '/')
            current++;

        /*
         * Finished.
         */
        if (*current == '\0') {

            result->cluster =
                current_directory;

            result->size = 0;
            result->directory = true;

            return FAT32_OK;
        }

        /*
         * Extract one path component.
         */
        uint32_t length = 0;

        while (
            current[length] != '\0' &&
            current[length] != '/' &&
            length < sizeof(component) - 1
        ) {
            component[length] =
                current[length];

            length++;
        }

        component[length] = '\0';

        if (length == 0)
            return FAT32_ERROR;

        /*
         * Search the current directory.
         */
        uint32_t found_cluster;
        uint32_t found_size;
        bool found_directory;
        uint32_t found_entry_index;

        fat32_result_t search_result =
            fat32_find_in_directory(
                fs,
                current_directory,
                component,
                &found_cluster,
                &found_size,
                &found_directory,
                &found_entry_index
            );

        if (search_result != FAT32_OK)
            return search_result;

        /*
         * Advance past this component.
         */
        current += length;

        /*
         * Skip slashes so we can determine whether
         * there is another component.
         */
        while (*current == '/')
            current++;

        /*
         * This was the final component.
         */
        if (*current == '\0') {

            result->cluster = found_cluster;
            result->size = found_size;
            result->directory = found_directory;

            result->directory_cluster =
                current_directory;

            result->directory_entry_index =
                found_entry_index;

            return FAT32_OK;
        }

        /*
         * There are more components.
         *
         * Therefore this component MUST be
         * a directory.
         */
        if (!found_directory)
            return FAT32_NOT_A_DIRECTORY;

        current_directory =
            found_cluster;
    }
}

fat32_result_t fat32_open(
    fat32_t* fs,
    const char* path,
    fat32_file_t* file
)
{
    if (fs == 0 ||
        path == 0 ||
        file == 0)
        return FAT32_ERROR;

    fat32_path_result_t result;

    fat32_result_t resolve_result =
        fat32_resolve_path(
            fs,
            path,
            &result
        );

    if (resolve_result != FAT32_OK)
        return resolve_result;

    /*
     * The requested path must refer to a file.
     */
    if (result.directory)
        return FAT32_NOT_A_FILE;

    file->fs = fs;
    file->first_cluster =
        result.cluster;
    file->size =
        result.size;
    file->position = 0;

    file->directory_cluster =
        result.directory_cluster;

    file->directory_entry_index =
        result.directory_entry_index;

    file->directory = false;

    return FAT32_OK;
}

fat32_result_t fat32_read(
    fat32_file_t* file,
    void* buffer,
    uint32_t size,
    uint32_t* bytes_read
)
{
    if (file == 0 ||
        buffer == 0 ||
        bytes_read == 0)
    {
        return FAT32_ERROR;
    }

    *bytes_read = 0;

    if (file->fs == 0)
        return FAT32_ERROR;

    if (file->directory)
        return FAT32_NOT_A_FILE;

    if (file->position >= file->size)
        return FAT32_END_OF_FILE;

    fat32_t* fs = file->fs;

    uint32_t remaining =
        file->size - file->position;

    if (size > remaining)
        size = remaining;

    uint32_t cluster_size =
        fs->bytes_per_sector *
        fs->sectors_per_cluster;

    if (cluster_size == 0)
        return FAT32_ERROR;

    /*
     * Find the cluster containing the
     * current file position.
     */

    uint32_t cluster_index =
        file->position / cluster_size;

    uint32_t cluster;

    fat32_result_t result =
        fat32_get_cluster_at(
            fs,
            file,
            cluster_index,
            &cluster
        );

    if (result != FAT32_OK)
        return result;

    /*
     * Offset within the current cluster.
     */

    uint32_t cluster_offset =
        file->position % cluster_size;

    uint8_t* output =
        (uint8_t*)buffer;

    while (*bytes_read < size)
    {
        /*
         * Determine which sector inside
         * the cluster contains our data.
         */

        uint32_t sector_in_cluster =
            cluster_offset /
            fs->bytes_per_sector;

        uint32_t offset_in_sector =
            cluster_offset %
            fs->bytes_per_sector;

        uint32_t lba =
            fat32_cluster_to_lba(
                fs,
                cluster
            ) +
            sector_in_cluster;

        if (!fs->device->read(
            lba,
            1,
            sector_buffer
        ))
        {
            return FAT32_IO_ERROR;
        }

        uint32_t available =
            fs->bytes_per_sector -
            offset_in_sector;

        uint32_t wanted =
            size - *bytes_read;

        uint32_t amount =
            wanted < available
                ? wanted
                : available;

        for (uint32_t i = 0;
             i < amount;
             i++)
        {
            output[*bytes_read + i] =
                sector_buffer[
                    offset_in_sector + i
                ];
        }

        *bytes_read += amount;

        file->position += amount;

        cluster_offset += amount;

        /*
         * If we've reached the end of
         * the cluster, move to the next
         * cluster.
         */

        if (cluster_offset >= cluster_size)
        {
            cluster_offset = 0;

            if (*bytes_read >= size)
                break;

            uint32_t next;

            result =
                fat32_next_cluster(
                    fs,
                    cluster,
                    &next
                );

            /*
             * The FAT chain ending before
             * file_size is corruption.
             */

            if (result ==
                FAT32_END_OF_FILE)
            {
                return FAT32_ERROR;
            }

            if (result != FAT32_OK)
                return result;

            cluster = next;
        }
    }

    return FAT32_OK;
}

fat32_result_t fat32_write(
    fat32_file_t* file,
    const void* buffer,
    uint32_t size,
    uint32_t* bytes_written
)
{
    if (file == 0 ||
        buffer == 0 ||
        bytes_written == 0)
    {
        return FAT32_ERROR;
    }

    *bytes_written = 0;

    if (file->fs == 0)
        return FAT32_ERROR;

    if (file->directory)
        return FAT32_NOT_A_FILE;

    if (size == 0)
        return FAT32_OK;

    fat32_t* fs = file->fs;

    if (fs->device == 0 ||
        fs->bytes_per_sector == 0 ||
        fs->sectors_per_cluster == 0)
    {
        return FAT32_ERROR;
    }

    uint32_t cluster_size =
        fs->bytes_per_sector *
        fs->sectors_per_cluster;

    if (cluster_size == 0)
        return FAT32_ERROR;

    /*
     * For now, don't support writing past EOF
     * with a gap. That requires zero-filling
     * the gap first.
     */
    if (file->position > file->size)
        return FAT32_ERROR;

    uint32_t old_size =
        file->size;

    uint32_t old_first_cluster =
        file->first_cluster;

    const uint8_t* input =
        (const uint8_t*)buffer;

    /*
     * Calculate where the write ends.
     */
    uint64_t write_end =
        (uint64_t)file->position +
        (uint64_t)size;

    if (write_end > 0xFFFFFFFFULL)
        return FAT32_ERROR;

    /*
     * Find the final cluster touched by
     * this write.
     */
    uint32_t final_cluster_index =
        (uint32_t)(
            (write_end - 1) /
            cluster_size
        );

    /*
     * Determine how many clusters the file
     * already requires.
     *
     * A zero-length file has zero clusters
     * in normal FAT32 usage.
     */
    uint32_t old_cluster_count = 0;

    if (file->size != 0)
    {
        old_cluster_count =
            (file->size + cluster_size - 1) /
            cluster_size;
    }

    /*
     * Make sure the file has a first cluster
     * if this write requires one.
     */
    if (file->first_cluster < 2)
    {
        if (file->size != 0)
            return FAT32_ERROR;

        uint32_t new_cluster;

        fat32_result_t result =
            fat32_allocate_cluster(
                fs,
                &new_cluster
            );

        if (result != FAT32_OK)
            return result;

        file->first_cluster =
            new_cluster;
    }

    /*
     * Find the cluster where the write starts
     * while making sure the chain is long
     * enough for the entire write.
     */
    uint32_t start_cluster_index =
        file->position /
        cluster_size;

    uint32_t current_cluster =
        file->first_cluster;

    uint32_t start_cluster =
        0;

    for (uint32_t cluster_index = 0;
        cluster_index <= final_cluster_index;
        cluster_index++)
    {
        /*
        * Remember the cluster where the
        * actual write begins.
        */
        if (cluster_index == start_cluster_index)
        {
            start_cluster =
                current_cluster;
        }

        /*
        * We have reached the final cluster
        * required by this write.
        */
        if (cluster_index == final_cluster_index)
            break;

        uint32_t next_cluster;

        fat32_result_t result =
            fat32_next_cluster(
                fs,
                current_cluster,
                &next_cluster
            );

        if (result == FAT32_END_OF_FILE)
        {
            /*
            * We need another cluster.
            */
            result =
                fat32_append_cluster(
                    fs,
                    current_cluster,
                    &next_cluster
                );

            if (result != FAT32_OK)
                return result;
        }
        else if (result != FAT32_OK)
        {
            return result;
        }

        current_cluster =
            next_cluster;
    }

    if (start_cluster < 2)
        return FAT32_ERROR;

    /*
     * Start writing at the requested position.
     */
    uint32_t cluster_offset =
        file->position %
        cluster_size;

    uint32_t cluster =
        start_cluster;

    printv2(
        "Writing from cluster *i\n",
        0x07,
        start_cluster
    );

    printv2(
        "Final required cluster index: *i\n",
        0x07,
        final_cluster_index
    );

    while (*bytes_written < size)
    {
        uint32_t sector_in_cluster =
            cluster_offset /
            fs->bytes_per_sector;

        uint32_t offset_in_sector =
            cluster_offset %
            fs->bytes_per_sector;

        uint32_t cluster_lba =
            fat32_cluster_to_lba(
                fs,
                cluster
            );

        if (cluster_lba == 0)
            return FAT32_ERROR;

        uint32_t lba =
            cluster_lba +
            sector_in_cluster;

        uint32_t available =
            fs->bytes_per_sector -
            offset_in_sector;

        uint32_t wanted =
            size -
            *bytes_written;

        uint32_t amount =
            wanted < available
                ? wanted
                : available;

        /*
         * Full-sector write.
         */
        if (offset_in_sector == 0 &&
            amount == fs->bytes_per_sector)
        {
            if (!fs->device->write(
                lba,
                1,
                &input[*bytes_written]
            ))
            {
                angrylog("DATA SECTOR WRITE FAILED");

                printv2(
                    "LBA: *i\n",
                    0x07,
                    lba
                );

                printv2(
                    "Cluster: *i\n",
                    0x07,
                    cluster
                );

                return FAT32_IO_ERROR;
            }
        }
        else
        {
            /*
             * Partial-sector write.
             *
             * Read the old sector first so that
             * bytes outside our write are preserved.
             */
            if (!fs->device->read(
                lba,
                1,
                sector_buffer
            ))
            {
                return FAT32_IO_ERROR;
            }

            for (uint32_t i = 0;
                 i < amount;
                 i++)
            {
                sector_buffer[
                    offset_in_sector + i
                ] =
                    input[
                        *bytes_written + i
                    ];
            }

            if (!fs->device->write(
                lba,
                1,
                sector_buffer
            ))
            {
                angrylog("PARTIAL DATA WRITE FAILED");

                printv2(
                    "LBA: *i\n",
                    0x07,
                    lba
                );

                printv2(
                    "Cluster: *i\n",
                    0x07,
                    cluster
                );

                return FAT32_IO_ERROR;
            }
        }

        *bytes_written += amount;
        file->position += amount;
        cluster_offset += amount;

        /*
         * Move to the next cluster when the
         * current cluster is completely consumed.
         */
        if (cluster_offset >= cluster_size)
        {
            cluster_offset = 0;

            if (*bytes_written >= size)
                break;

            uint32_t next_cluster;

            fat32_result_t result =
                fat32_next_cluster(
                    fs,
                    cluster,
                    &next_cluster
                );

            if (result != FAT32_OK)
                return result;

            printv2(
                "Moving from cluster *i to *i\n",
                0x07,
                cluster,
                next_cluster
            );

            cluster =
                next_cluster;
                printv2(
                    "Now writing cluster *i\n",
                    0x07,
                    cluster
                );
        }
    }

    /*
     * Update the in-memory file size.
     */
    if (file->position > file->size)
        file->size = file->position;

    /*
     * Persist the directory entry if the
     * file size or first cluster changed.
     */
    if (file->size != old_size ||
    file->first_cluster != old_first_cluster)
    {
        printv2(
            "Updating directory entry...\n",
            0x07
        );

        fat32_result_t result =
            fat32_update_file_entry(
                file
            );

        if (result != FAT32_OK)
        {
            angrylog(
                "DIRECTORY ENTRY UPDATE FAILED"
            );

            printv2(
                "Directory result: *i\n",
                0x07,
                result
            );

            return result;
        }

        printv2(
            "Directory entry updated\n",
            0x07
        );
    }

    return FAT32_OK;
}

static fat32_result_t fat32_free_cluster_chain(
    fat32_t* fs,
    uint32_t first_cluster
)
{
    if (fs == 0 ||
        fs->device == 0)
        return FAT32_ERROR;

    /*
     * Empty file.
     */
    if (first_cluster == 0)
        return FAT32_OK;

    if (first_cluster < 2 ||
        first_cluster > fs->total_clusters + 1)
        return FAT32_ERROR;

    uint32_t current = first_cluster;

    for (;;) {

        uint32_t next;

        fat32_result_t result =
            fat32_read_fat_entry(
                fs,
                current,
                &next
            );

        if (result != FAT32_OK)
            return result;

        next &= 0x0FFFFFFF;

        /*
         * Free the current cluster.
         */
        result =
            fat32_write_fat_entry(
                fs,
                current,
                0
            );

        if (result != FAT32_OK)
            return result;

        /*
         * We reached the end of the chain.
         */
        if (next >= 0x0FFFFFF8)
            return FAT32_OK;

        /*
         * Bad cluster.
         */
        if (next == 0x0FFFFFF7)
            return FAT32_ERROR;

        /*
         * Invalid next cluster.
         */
        if (next < 2 ||
            next > fs->total_clusters + 1)
            return FAT32_ERROR;

        current = next;
    }
}

fat32_result_t fat32_opendir(
    fat32_t* fs,
    const char* path,
    fat32_dir_t* dir
)
{
    if (fs == 0 ||
        path == 0 ||
        dir == 0)
        return FAT32_ERROR;

    fat32_path_result_t result;

    fat32_result_t resolve_result =
        fat32_resolve_path(
            fs,
            path,
            &result
        );

    if (resolve_result != FAT32_OK)
        return resolve_result;

    /*
     * The requested path must be a directory.
     */
    if (!result.directory)
        return FAT32_NOT_A_DIRECTORY;

    dir->fs = fs;

    dir->cluster =
        result.cluster;

    dir->sector_in_cluster = 0;

    dir->entry_in_sector = 0;

    dir->finished = false;

    dir->has_lfn = false;

    for (uint32_t i = 0;
         i < sizeof(dir->lfn_name);
         i++) {

        dir->lfn_name[i] = '\0';
    }

    return FAT32_OK;
}

fat32_result_t fat32_readdir(
    fat32_dir_t* dir,
    char* name,
    uint32_t name_size,
    bool* is_directory,
    uint32_t* size
)
{
    if (dir == 0 ||
        name == 0 ||
        is_directory == 0 ||
        size == 0 ||
        name_size == 0)
        return FAT32_ERROR;

    if (dir->finished)
        return FAT32_END_OF_FILE;

    fat32_t* fs = dir->fs;

    while (!dir->finished) {

        uint32_t lba =
            fat32_cluster_to_lba(
                fs,
                dir->cluster
            ) + dir->sector_in_cluster;

        if (!fs->device->read(
            lba,
            1,
            sector_buffer
        ))
            return FAT32_IO_ERROR;

        uint32_t entries_per_sector =
            fs->bytes_per_sector / 32;

        while (
            dir->entry_in_sector <
            entries_per_sector
        ) {
            uint8_t* raw =
                &sector_buffer[
                    dir->entry_in_sector * 32
                ];

            dir->entry_in_sector++;

            /*
             * 0x00 = no more entries.
             */
            if (raw[0] == 0x00) {
                dir->finished = true;
                return FAT32_END_OF_FILE;
            }

            /*
             * Deleted entry.
             */
            if (raw[0] == 0xE5) {
                dir->has_lfn = false;
                clear_string(
                    dir->lfn_name,
                    sizeof(dir->lfn_name)
                );
                continue;
            }

            /*
             * LFN entry.
             */
            if (raw[11] == FAT32_ATTR_LFN) {

                /*
                 * Last LFN entry starts a new name.
                 */
                if (raw[0] &
                    FAT32_LFN_SEQUENCE_LAST) {

                    dir->has_lfn = true;

                    clear_string(
                        dir->lfn_name,
                        sizeof(dir->lfn_name)
                    );
                }

                if (dir->has_lfn)
                    lfn_write_chars(
                        dir->lfn_name,
                        sizeof(dir->lfn_name),
                        raw
                    );

                continue;
            }

            /*
             * Normal directory entry.
             */
            fat32_directory_entry_t* entry =
                (fat32_directory_entry_t*)raw;

            /*
             * Volume label is not a file.
             */
            if (entry->attributes &
                FAT32_ATTR_VOLUME_ID) {

                dir->has_lfn = false;

                clear_string(
                    dir->lfn_name,
                    sizeof(dir->lfn_name)
                );

                continue;
            }

            /*
             * Decide what name to return.
             */
            char entry_name[256];

            clear_string(
                entry_name,
                sizeof(entry_name)
            );

            if (dir->has_lfn) {

                for (uint32_t i = 0;
                     i < sizeof(entry_name) - 1;
                     i++) {

                    entry_name[i] =
                        dir->lfn_name[i];

                    if (dir->lfn_name[i] == '\0')
                        break;
                }

            }
            else {
                make_83_display_name(
                    entry->name,
                    entry_name,
                    sizeof(entry_name)
                );
            }

            /*
             * Clear LFN state now that its
             * associated normal entry has been read.
             */
            dir->has_lfn = false;

            clear_string(
                dir->lfn_name,
                sizeof(dir->lfn_name)
            );

            /*
             * Ignore . and .. for now.
             *
             * We'll probably want a flag for this later.
             */
            if (
                (entry->name[0] == '.' &&
                 entry->name[1] == ' ') ||
                (entry->name[0] == '.' &&
                 entry->name[1] == '.' &&
                 entry->name[2] == ' ')
            ) {
                continue;
            }

            /*
             * Copy name to caller.
             */
            uint32_t i = 0;

            while (
                entry_name[i] != '\0' &&
                i < name_size - 1
            ) {
                name[i] =
                    entry_name[i];

                i++;
            }

            name[i] = '\0';

            *is_directory =
                (entry->attributes &
                 FAT32_ATTR_DIRECTORY) != 0;

            *size =
                read_u32(
                    (uint8_t*)&entry->file_size
                );

            return FAT32_OK;
        }

        /*
         * Move to next sector within cluster.
         */
        dir->sector_in_cluster++;
        dir->entry_in_sector = 0;

        if (
            dir->sector_in_cluster >=
            fs->sectors_per_cluster
        ) {
            dir->sector_in_cluster = 0;

            uint32_t next;

            fat32_result_t result =
                fat32_next_cluster(
                    fs,
                    dir->cluster,
                    &next
                );

            if (result ==
                FAT32_END_OF_FILE) {

                dir->finished = true;
                return FAT32_END_OF_FILE;
            }

            if (result != FAT32_OK)
                return result;

            dir->cluster = next;
        }
    }

    return FAT32_END_OF_FILE;
}

fat32_result_t fat32_create_file(
    fat32_t* fs,
    const char* path,
    fat32_file_t* file
)
{
    if (fs == 0 ||
        path == 0 ||
        file == 0)
        return FAT32_ERROR;

    char parent_path[256];
    char name[256];

    fat32_result_t result =
        fat32_split_parent_path(
            path,
            parent_path,
            sizeof(parent_path),
            name,
            sizeof(name)
        );

    if (result != FAT32_OK)
        return result;

    /*
     * Resolve the parent directory.
     */
    fat32_path_result_t parent;

    result =
        fat32_resolve_path(
            fs,
            parent_path,
            &parent
        );

    if (result != FAT32_OK)
        return result;

    if (!parent.directory)
        return FAT32_NOT_A_DIRECTORY;

    /*
     * Make sure the filename is valid 8.3.
     */
    uint8_t short_name[11];

    if (!make_83_name(
            name,
            short_name
        ))
        return FAT32_ERROR;

    /*
     * Make sure the file doesn't
     * already exist.
     */
    uint32_t existing_cluster;
    uint32_t existing_size;
    bool existing_directory;
    uint32_t existing_entry_index;

    result =
        fat32_find_in_directory(
            fs,
            parent.cluster,
            name,
            &existing_cluster,
            &existing_size,
            &existing_directory,
            &existing_entry_index
        );

    if (result == FAT32_OK)
        return FAT32_ERROR;

    if (result != FAT32_NOT_FOUND)
        return result;

    /*
     * Find a free directory entry.
     */
    uint32_t entry_index;

    result =
        fat32_find_free_directory_entry(
            fs,
            parent.cluster,
            &entry_index
        );

    if (result != FAT32_OK)
        return result;

    /*
     * Create an empty file.
     */
    result =
        fat32_write_directory_entry(
            fs,
            parent.cluster,
            entry_index,
            short_name
        );

    if (result != FAT32_OK)
        return result;

    /*
     * Initialize the file handle.
     */
    file->fs = fs;
    file->first_cluster = 0;
    file->size = 0;
    file->position = 0;

    file->directory_cluster =
        parent.cluster;

    file->directory_entry_index =
        entry_index;

    file->directory = false;

    return FAT32_OK;
}

fat32_result_t fat32_initialize_directory(
    fat32_t* fs,
    uint32_t directory_cluster,
    uint32_t parent_cluster
)
{
    if (fs == 0 ||
        fs->device == 0)
        return FAT32_ERROR;

    if (directory_cluster < 2 ||
        directory_cluster > fs->total_clusters + 1)
        return FAT32_ERROR;

    if (parent_cluster < 2 ||
        parent_cluster > fs->total_clusters + 1)
        return FAT32_ERROR;

    /*
     * The cluster was already zeroed by
     * fat32_allocate_cluster().
     *
     * Read the first sector so we can
     * write the "." and ".." entries.
     */
    uint32_t lba =
        fat32_cluster_to_lba(
            fs,
            directory_cluster
        );

    if (lba == 0)
        return FAT32_ERROR;

    if (!fs->device->read(
        lba,
        1,
        sector_buffer
    ))
        return FAT32_IO_ERROR;

    /*
     * "." entry
     */
    for (uint32_t i = 0; i < 11; i++)
        sector_buffer[i] = ' ';

    sector_buffer[0] = '.';

    sector_buffer[11] =
        FAT32_ATTR_DIRECTORY;

    write_u16(
        &sector_buffer[20],
        (uint16_t)(
            directory_cluster >> 16
        )
    );

    write_u16(
        &sector_buffer[26],
        (uint16_t)(
            directory_cluster & 0xFFFF
        )
    );

    write_u32(
        &sector_buffer[28],
        0
    );

    /*
     * ".." entry
     */
    uint32_t offset = 32;

    for (uint32_t i = 0; i < 11; i++)
        sector_buffer[offset + i] = ' ';

    sector_buffer[offset + 0] = '.';
    sector_buffer[offset + 1] = '.';

    sector_buffer[offset + 11] =
        FAT32_ATTR_DIRECTORY;

    write_u16(
        &sector_buffer[offset + 20],
        (uint16_t)(
            parent_cluster >> 16
        )
    );

    write_u16(
        &sector_buffer[offset + 26],
        (uint16_t)(
            parent_cluster & 0xFFFF
        )
    );

    write_u32(
        &sector_buffer[offset + 28],
        0
    );

    if (!fs->device->write(
        lba,
        1,
        sector_buffer
    ))
        return FAT32_IO_ERROR;

    return FAT32_OK;
}

fat32_result_t fat32_mkdir(
    fat32_t* fs,
    const char* path
)
{
    if (fs == 0 ||
        path == 0)
        return FAT32_ERROR;

    /*
     * Split:
     *
     * /TESTDIR/NEW
     *
     * into:
     *
     * parent = /TESTDIR
     * name   = NEW
     */
    char parent_path[256];
    char name[256];

    fat32_result_t result =
        fat32_split_parent_path(
            path,
            parent_path,
            sizeof(parent_path),
            name,
            sizeof(name)
        );

    if (result != FAT32_OK)
        return result;

    /*
     * Resolve the parent directory.
     */
    fat32_path_result_t parent;

    result =
        fat32_resolve_path(
            fs,
            parent_path,
            &parent
        );

    if (result != FAT32_OK)
        return result;

    if (!parent.directory)
        return FAT32_NOT_A_DIRECTORY;

    /*
     * Convert the directory name
     * into an 8.3 name.
     */
    uint8_t short_name[11];

    if (!make_83_name(
            name,
            short_name
        ))
        return FAT32_ERROR;

    /*
     * Make sure the directory doesn't
     * already exist.
     */
    uint32_t existing_cluster;
    uint32_t existing_size;
    bool existing_directory;
    uint32_t existing_entry_index;

    result =
        fat32_find_in_directory(
            fs,
            parent.cluster,
            name,
            &existing_cluster,
            &existing_size,
            &existing_directory,
            &existing_entry_index
        );

    if (result == FAT32_OK)
        return FAT32_ERROR;

    if (result != FAT32_NOT_FOUND)
        return result;

    /*
     * Find somewhere in the parent directory
     * to put the new entry.
     */
    uint32_t entry_index;

    result =
        fat32_find_free_directory_entry(
            fs,
            parent.cluster,
            &entry_index
        );

    if (result != FAT32_OK)
        return result;

    /*
     * Allocate a cluster for the new directory.
     *
     * fat32_allocate_cluster() also zeroes it.
     */
    uint32_t directory_cluster;

    result =
        fat32_allocate_cluster(
            fs,
            &directory_cluster
        );

    if (result != FAT32_OK)
        return result;

    /*
     * Initialize "." and "..".
     */
    result =
        fat32_initialize_directory(
            fs,
            directory_cluster,
            parent.cluster
        );

    if (result != FAT32_OK) {
        /*
         * Roll back the allocation if
         * directory initialization fails.
         */
        fat32_write_fat_entry(
            fs,
            directory_cluster,
            0
        );

        return result;
    }

    /*
     * Write the directory entry in the
     * parent directory.
     */
    result =
        fat32_write_directory_entry_data(
            fs,
            parent.cluster,
            entry_index,
            short_name,
            FAT32_ATTR_DIRECTORY,
            directory_cluster,
            0
        );

    if (result != FAT32_OK) {
        /*
         * Roll back the allocated cluster.
         */
        fat32_write_fat_entry(
            fs,
            directory_cluster,
            0
        );

        return result;
    }

    return FAT32_OK;
}

static fat32_result_t fat32_delete_directory_entry(
    fat32_t* fs,
    uint32_t directory_cluster,
    uint32_t entry_index
)
{
    if (fs == 0 ||
        fs->device == 0)
        return FAT32_ERROR;

    if (directory_cluster < 2 ||
        directory_cluster > fs->total_clusters + 1)
        return FAT32_ERROR;

    uint32_t entries_per_sector =
        fs->bytes_per_sector / 32;

    if (entries_per_sector == 0)
        return FAT32_ERROR;

    uint32_t sector_index =
        entry_index / entries_per_sector;

    uint32_t entry_in_sector =
        entry_index % entries_per_sector;

    uint32_t cluster_index =
        sector_index /
        fs->sectors_per_cluster;

    uint32_t sector_in_cluster =
        sector_index %
        fs->sectors_per_cluster;

    fat32_file_t directory;

    directory.fs = fs;
    directory.first_cluster =
        directory_cluster;
    directory.size = 0;
    directory.position = 0;
    directory.directory_cluster = 0;
    directory.directory_entry_index = 0;
    directory.directory = true;

    uint32_t cluster;

    fat32_result_t result =
        fat32_get_cluster_at(
            fs,
            &directory,
            cluster_index,
            &cluster
        );

    if (result != FAT32_OK)
        return result;

    uint32_t lba =
        fat32_cluster_to_lba(
            fs,
            cluster
        );

    if (lba == 0)
        return FAT32_ERROR;

    lba += sector_in_cluster;

    if (!fs->device->read(
        lba,
        1,
        sector_buffer
    ))
        return FAT32_IO_ERROR;

    uint32_t offset =
        entry_in_sector * 32;

    /*
     * FAT32 marks a deleted directory
     * entry by changing its first byte
     * to 0xE5.
     */
    sector_buffer[offset] = 0xE5;

    if (!fs->device->write(
        lba,
        1,
        sector_buffer
    ))
        return FAT32_IO_ERROR;

    return FAT32_OK;
}

fat32_result_t fat32_remove_file(
    fat32_t* fs,
    const char* path
)
{
    if (fs == 0 ||
        path == 0)
        return FAT32_ERROR;

    char parent_path[256];
    char name[256];

    fat32_result_t result =
        fat32_split_parent_path(
            path,
            parent_path,
            sizeof(parent_path),
            name,
            sizeof(name)
        );

    if (result != FAT32_OK)
        return result;

    fat32_path_result_t parent;

    result =
        fat32_resolve_path(
            fs,
            parent_path,
            &parent
        );

    if (result != FAT32_OK)
        return result;

    if (!parent.directory)
        return FAT32_NOT_A_DIRECTORY;

    uint32_t found_cluster;
    uint32_t found_size;
    bool is_directory;
    uint32_t found_entry_index;

    result =
        fat32_find_in_directory(
            fs,
            parent.cluster,
            name,
            &found_cluster,
            &found_size,
            &is_directory,
            &found_entry_index
        );

    if (result != FAT32_OK)
        return result;

    if (is_directory)
        return FAT32_NOT_A_FILE;

    /*
     * Remove the directory entry first.
     *
     * If freeing the FAT chain fails afterwards,
     * the file is gone from the directory but its
     * clusters may be leaked. That's safer than
     * leaving a directory entry pointing at freed
     * clusters.
     */
    result =
        fat32_delete_directory_entry(
            fs,
            parent.cluster,
            found_entry_index
        );

    if (result != FAT32_OK)
        return result;

    /*
     * Empty files have no cluster chain.
     */
    if (found_cluster == 0)
        return FAT32_OK;

    return fat32_free_cluster_chain(
        fs,
        found_cluster
    );
}

static fat32_result_t fat32_directory_is_empty(
    fat32_t* fs,
    uint32_t directory_cluster,
    bool* empty
)
{
    if (fs == 0 ||
        fs->device == 0 ||
        empty == 0)
        return FAT32_ERROR;

    if (directory_cluster < 2 ||
        directory_cluster > fs->total_clusters + 1)
        return FAT32_ERROR;

    uint32_t cluster =
        directory_cluster;

    uint32_t entries_per_sector =
        fs->bytes_per_sector / 32;

    if (entries_per_sector == 0)
        return FAT32_ERROR;

    for (;;) {

        for (uint32_t sector_in_cluster = 0;
             sector_in_cluster < fs->sectors_per_cluster;
             sector_in_cluster++) {

            uint32_t lba =
                fat32_cluster_to_lba(
                    fs,
                    cluster
                );

            if (lba == 0)
                return FAT32_ERROR;

            lba += sector_in_cluster;

            if (!fs->device->read(
                lba,
                1,
                sector_buffer
            ))
                return FAT32_IO_ERROR;

            for (uint32_t i = 0;
                 i < entries_per_sector;
                 i++) {

                uint32_t offset =
                    i * 32;

                uint8_t first_byte =
                    sector_buffer[offset];

                /*
                 * 0x00 means the rest of the
                 * directory is unused.
                 */
                if (first_byte == 0x00) {
                    *empty = true;
                    return FAT32_OK;
                }

                /*
                 * Deleted entries don't count.
                 */
                if (first_byte == 0xE5)
                    continue;

                uint8_t attributes =
                    sector_buffer[offset + 11];

                /*
                 * Long filename entries don't
                 * represent actual files/directories.
                 */
                if (attributes == FAT32_ATTR_LFN)
                    continue;

                /*
                 * Ignore "." and "..".
                 */
                if (first_byte == '.') {

                    if (sector_buffer[offset + 1] == ' ' ||
                        sector_buffer[offset + 1] == '.') {
                        continue;
                    }
                }

                /*
                 * Anything else means the directory
                 * contains a real entry.
                 */
                *empty = false;
                return FAT32_OK;
            }
        }

        uint32_t next;

        fat32_result_t result =
            fat32_next_cluster(
                fs,
                cluster,
                &next
            );

        if (result == FAT32_END_OF_FILE) {
            *empty = true;
            return FAT32_OK;
        }

        if (result != FAT32_OK)
            return result;

        cluster = next;
    }
}

fat32_result_t fat32_remove_directory(
    fat32_t* fs,
    const char* path
)
{
    if (fs == 0 ||
        path == 0)
        return FAT32_ERROR;

    char parent_path[256];
    char name[256];

    fat32_result_t result =
        fat32_split_parent_path(
            path,
            parent_path,
            sizeof(parent_path),
            name,
            sizeof(name)
        );

    if (result != FAT32_OK)
        return result;

    /*
     * Don't allow removing the root directory.
     */
    if (!strcmp(path, "/"))
        return FAT32_ERROR;

    fat32_path_result_t parent;

    result =
        fat32_resolve_path(
            fs,
            parent_path,
            &parent
        );

    if (result != FAT32_OK)
        return result;

    if (!parent.directory)
        return FAT32_NOT_A_DIRECTORY;

    uint32_t found_cluster;
    uint32_t found_size;
    bool is_directory;
    uint32_t found_entry_index;

    result =
        fat32_find_in_directory(
            fs,
            parent.cluster,
            name,
            &found_cluster,
            &found_size,
            &is_directory,
            &found_entry_index
        );

    if (result != FAT32_OK)
        return result;

    if (!is_directory)
        return FAT32_NOT_A_DIRECTORY;

    /*
     * Make sure the directory has no real
     * files or subdirectories.
     */
    bool empty;

    result =
        fat32_directory_is_empty(
            fs,
            found_cluster,
            &empty
        );

    if (result != FAT32_OK)
        return result;

    if (!empty)
        return FAT32_DIRECTORY_NOT_EMPTY;

    /*
     * Remove the directory entry first.
     */
    result =
        fat32_delete_directory_entry(
            fs,
            parent.cluster,
            found_entry_index
        );

    if (result != FAT32_OK)
        return result;

    /*
     * A directory currently has one cluster
     * in our implementation, but using the
     * generic chain freeing function means
     * this also works if that changes later.
     */
    return fat32_free_cluster_chain(
        fs,
        found_cluster
    );
}