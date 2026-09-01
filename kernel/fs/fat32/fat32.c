#include "fat32.h"

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


static bool is_power_of_two(uint32_t value)
{
    return value != 0 &&
           (value & (value - 1)) == 0;
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
    if (fs == 0 || cluster < 2)
        return 0;

    return fs->data_start +
       (uint32_t)(
           ((uint64_t)(cluster - 2) *
            fs->sectors_per_cluster)
       );
}


fat32_result_t fat32_read_fat_entry(
    fat32_t* fs,
    uint32_t cluster,
    uint32_t* value
)
{
    if (fs == 0 || value == 0)
        return FAT32_ERROR;

    uint32_t fat_offset =
        cluster * 4;

    uint32_t fat_sector =
        fs->fat_start +
        (fat_offset / fs->bytes_per_sector);

    uint32_t offset =
        fat_offset % fs->bytes_per_sector;

    /*
     * A FAT32 entry can theoretically cross
     * a sector boundary, so handle it.
     */

    if (offset <= fs->bytes_per_sector - 4) {

        if (!fs->device->read(
            fat_sector,
            1,
            sector_buffer))
            return FAT32_IO_ERROR;

        *value =
            read_u32(
                &sector_buffer[offset]
            ) & 0x0FFFFFFF;

        return FAT32_OK;
    }

    /*
     * Cross-sector entry.
     */

    uint8_t entry[4];

    uint32_t first =
        fs->bytes_per_sector - offset;

    if (!fs->device->read(
        fat_sector,
        1,
        sector_buffer))
        return FAT32_IO_ERROR;

    for (uint32_t i = 0; i < first; i++)
        entry[i] =
            sector_buffer[offset + i];

    if (!fs->device->read(
        fat_sector + 1,
        1,
        sector_buffer))
        return FAT32_IO_ERROR;

    for (uint32_t i = first; i < 4; i++)
        entry[i] =
            sector_buffer[i - first];

    *value =
        read_u32(entry) & 0x0FFFFFFF;

    return FAT32_OK;
}


fat32_result_t fat32_next_cluster(
    fat32_t* fs,
    uint32_t cluster,
    uint32_t* next
)
{
    if (fs == 0 || next == 0)
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

    if (value >= 0x0FFFFFF8) {
        *next = 0;
        return FAT32_END_OF_FILE;
    }

    if (value == 0x0FFFFFF7)
        return FAT32_ERROR;

    if (value < 2)
        return FAT32_ERROR;

    *next = value;

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

    uint32_t current = file->first_cluster;

    for (uint32_t i = 0; i < cluster_index; i++) {
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

/*
 * Convert an 8.3 filename such as:
 *
 *     HELLO.TXT
 *
 * into:
 *
 *     "HELLO   TXT"
 */
static bool make_83_name(
    const char* input,
    uint8_t output[11]
)
{
    if (input == 0)
        return false;

    for (uint32_t i = 0; i < 11; i++)
        output[i] = ' ';

    uint32_t i = 0;

    while (
        input[i] != '\0' &&
        input[i] != '.' &&
        i < 8
    ) {
        char c = input[i];

        if (c >= 'a' && c <= 'z')
            c -= 'a' - 'A';

        output[i] = (uint8_t)c;
        i++;
    }

    if (input[i] == '.')
        i++;

    uint32_t ext = 0;

    while (
        input[i] != '\0' &&
        ext < 3
    ) {
        char c = input[i];

        if (c >= 'a' && c <= 'z')
            c -= 'a' - 'A';

        output[8 + ext] =
            (uint8_t)c;

        ext++;
        i++;
    }

    /*
     * If there is more than 8 chars in the
     * filename or more than 3 in the extension,
     * reject it for now.
     */

    if (input[i] != '\0')
        return false;

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


fat32_result_t fat32_find_in_directory(
    fat32_t* fs,
    uint32_t directory_cluster,
    const char* name,
    uint32_t* found_cluster,
    uint32_t* found_size,
    bool* is_directory
)
{
    if (fs == 0 ||
        name == 0 ||
        found_cluster == 0 ||
        found_size == 0 ||
        is_directory == 0)
        return FAT32_ERROR;

    uint8_t target_name[11];
    bool target_is_83 =
        make_83_name(name, target_name);

    uint32_t cluster =
        directory_cluster;

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

        fat32_result_t search_result =
            fat32_find_in_directory(
                fs,
                current_directory,
                component,
                &found_cluster,
                &found_size,
                &found_directory
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

            result->cluster =
                found_cluster;

            result->size =
                found_size;

            result->directory =
                found_directory;

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
        return FAT32_ERROR;

    *bytes_read = 0;

    if (file->fs == 0)
        return FAT32_ERROR;

    if (file->directory)
        return FAT32_ERROR;

    if (file->position >= file->size)
        return FAT32_END_OF_FILE;

    uint32_t remaining =
        file->size - file->position;

    if (size > remaining)
        size = remaining;

    fat32_t* fs = file->fs;

    /*
     * First version: one-sector clusters.
     */

    if (fs->sectors_per_cluster != 1)
        return FAT32_ERROR;

    uint32_t cluster =
        file->first_cluster;

    /*
     * Find the cluster containing
     * the current file position.
     */
    uint32_t cluster_index =
        file->position / FAT32_SECTOR_SIZE;

    for (uint32_t i = 0;
         i < cluster_index;
         i++) {

        uint32_t next;

        fat32_result_t result =
            fat32_next_cluster(
                fs,
                cluster,
                &next
            );

        if (result != FAT32_OK)
            return FAT32_ERROR;

        cluster = next;
    }

    uint32_t offset =
        file->position %
        FAT32_SECTOR_SIZE;

    uint8_t* output =
        (uint8_t*)buffer;

    while (*bytes_read < size) {

        uint32_t lba =
            fat32_cluster_to_lba(
                fs,
                cluster
            );

        if (!fs->device->read(
            lba,
            1,
            sector_buffer))
            return FAT32_IO_ERROR;

        uint32_t available =
            FAT32_SECTOR_SIZE - offset;

        uint32_t wanted =
            size - *bytes_read;

        uint32_t amount =
            wanted < available
                ? wanted
                : available;

        for (uint32_t i = 0;
             i < amount;
             i++) {

            output[*bytes_read + i] =
                sector_buffer[offset + i];
        }

        *bytes_read += amount;
        file->position += amount;

        offset = 0;

        if (*bytes_read >= size)
            break;

        uint32_t next;

        fat32_result_t result =
            fat32_next_cluster(
                fs,
                cluster,
                &next
            );

        if (result != FAT32_OK)
            return FAT32_ERROR;

        cluster = next;
    }

    return FAT32_OK;
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