#include "templogpro_usb_msc_storage.h"
#include "templogpro_usb_files.h"
#include <stdint.h>
#include <string.h>

#define MSC_SECTOR_SIZE              512U
#define MSC_DISK_TOTAL_SECTORS       32768U
#define MSC_PARTITION_START_SECTOR   1U
#define MSC_PARTITION_SECTORS        (MSC_DISK_TOTAL_SECTORS - MSC_PARTITION_START_SECTOR)
#define MSC_RESERVED_SECTORS         1U
#define MSC_FAT_COUNT                2U
#define MSC_SECTORS_PER_FAT          128U
#define MSC_ROOT_ENTRY_COUNT         512U
#define MSC_ROOT_DIR_SECTORS         ((MSC_ROOT_ENTRY_COUNT * 32U) / MSC_SECTOR_SIZE)
#define MSC_ROOT_DIR_FIRST_SECTOR    (MSC_RESERVED_SECTORS + (MSC_FAT_COUNT * MSC_SECTORS_PER_FAT))
#define MSC_DATA_FIRST_SECTOR        (MSC_ROOT_DIR_FIRST_SECTOR + MSC_ROOT_DIR_SECTORS)
#define MSC_HTML_FIRST_CLUSTER       2U

static int8_t storage_init(uint8_t lun);
static int8_t storage_get_capacity(uint8_t lun, uint32_t *block_num, uint16_t *block_size);
static int8_t storage_is_ready(uint8_t lun);
static int8_t storage_is_write_protected(uint8_t lun);
static int8_t storage_read(uint8_t lun, uint8_t *buf, uint32_t blk_addr, uint16_t blk_len);
static int8_t storage_write(uint8_t lun, uint8_t *buf, uint32_t blk_addr, uint16_t blk_len);
static int8_t storage_get_max_lun(void);

typedef struct
{
    uint32_t valid;
    uint32_t html_size;
    uint32_t html_clusters;
    uint32_t csv_first;
    uint32_t csv_size;
    uint32_t csv_clusters;
    uint32_t readme_first;
    uint32_t readme_size;
    uint32_t readme_clusters;
    uint32_t power_first;
    uint32_t power_size;
    uint32_t power_clusters;
} MscLayoutCache;

static MscLayoutCache g_layout_cache;
static volatile uint32_t g_config_write_status;
static volatile uint32_t g_time_write_status;

static void build_sector(uint32_t sector, uint8_t *buf);
static void build_mbr_sector(uint8_t *buf);
static void build_boot_sector(uint8_t *buf);
static void build_fat_sector(uint32_t fat_sector, uint8_t *buf);
static void build_root_sector(uint32_t root_sector, uint8_t *buf);
static void build_html_sector(uint32_t file_offset, uint8_t *buf);
static void build_csv_sector(uint32_t file_offset, uint8_t *buf);
static void build_readme_sector(uint32_t file_offset, uint8_t *buf);
static void build_power_sector(uint32_t file_offset, uint8_t *buf);
static void write_dir_entry(uint8_t *buf, uint32_t offset, const char name[11], uint16_t cluster, uint32_t size);
static uint32_t write_lfn_name(uint8_t *buf, uint32_t offset, const char *name, uint8_t checksum);
static uint8_t lfn_checksum(const char name[11]);
static void lfn_put_char(uint8_t *buf, uint32_t offset, char ch);
static void layout_cache_reset(void);
static void layout_cache_ensure(void);
static uint32_t cluster_count(uint32_t size);
static uint16_t fat_chain_entry_value(uint32_t entry, uint32_t first, uint32_t count);
static uint16_t fat_entry_value(uint32_t entry);
static void put_u16(uint8_t *buf, uint32_t offset, uint16_t value);
static void put_u32(uint8_t *buf, uint32_t offset, uint32_t value);

static int8_t inquiry_data[] = {
    0x00,
    0x80,
    0x02,
    0x02,
    (STANDARD_INQUIRY_DATA_LEN - 5),
    0x00,
    0x00,
    0x00,
    /* Vendor (8, blank) + product (16): Windows shows "TempLogPro USB Device". */
    ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ',
    'T', 'e', 'm', 'p', 'L', 'o', 'g', 'P',
    'r', 'o', ' ', ' ', ' ', ' ', ' ', ' ',
    '0', '0', '1', '0',
};

USBD_StorageTypeDef TEMPLOGPRO_MSC_STORAGE = {
    storage_init,
    storage_get_capacity,
    storage_is_ready,
    storage_is_write_protected,
    storage_read,
    storage_write,
    storage_get_max_lun,
    inquiry_data,
};

static int8_t storage_init(uint8_t lun)
{
    (void)lun;
    templogpro_usb_files_snapshot_reset();
    layout_cache_reset();
    g_config_write_status = 0U;
    g_time_write_status = 0U;
    return 0;
}

static int8_t storage_get_capacity(uint8_t lun, uint32_t *block_num, uint16_t *block_size)
{
    (void)lun;

    if ((block_num == 0) || (block_size == 0)) {
        return -1;
    }

    *block_num = MSC_DISK_TOTAL_SECTORS;
    *block_size = MSC_SECTOR_SIZE;
    return 0;
}

static int8_t storage_is_ready(uint8_t lun)
{
    (void)lun;
    return 0;
}

static int8_t storage_is_write_protected(uint8_t lun)
{
    (void)lun;
    return 0;
}

static int8_t storage_read(uint8_t lun, uint8_t *buf, uint32_t blk_addr, uint16_t blk_len)
{
    uint16_t i;

    (void)lun;

    if (buf == 0) {
        return -1;
    }

    if ((blk_addr >= MSC_DISK_TOTAL_SECTORS) || ((uint32_t)blk_len > (MSC_DISK_TOTAL_SECTORS - blk_addr))) {
        return -1;
    }

    for (i = 0; i < blk_len; i++) {
        build_sector(blk_addr + i, &buf[(uint32_t)i * MSC_SECTOR_SIZE]);
    }

    return 0;
}

static int8_t storage_write(uint8_t lun, uint8_t *buf, uint32_t blk_addr, uint16_t blk_len)
{
    (void)lun;
    (void)buf;
    (void)blk_addr;
    (void)blk_len;
    return 0;
}

static int8_t storage_get_max_lun(void)
{
    return 0;
}

void templogpro_usb_msc_storage_process(void)
{
}

void templogpro_usb_msc_storage_layout_reset(void)
{
    layout_cache_reset();
}

uint32_t templogpro_usb_msc_config_write_status(void)
{
    return g_config_write_status;
}

uint32_t templogpro_usb_msc_time_write_status(void)
{
    return g_time_write_status;
}

static void build_sector(uint32_t sector, uint8_t *buf)
{
    uint32_t part_sector;
    uint32_t data_sector;
    uint32_t cluster;

    memset(buf, 0, MSC_SECTOR_SIZE);
    layout_cache_ensure();

    if (sector == 0U) {
        build_mbr_sector(buf);
        return;
    }

    if (sector < MSC_PARTITION_START_SECTOR) {
        return;
    }

    part_sector = sector - MSC_PARTITION_START_SECTOR;
    if (part_sector >= MSC_PARTITION_SECTORS) {
        return;
    }

    if (part_sector == 0U) {
        build_boot_sector(buf);
        return;
    }

    if ((part_sector >= 1U) && (part_sector < (1U + MSC_SECTORS_PER_FAT))) {
        build_fat_sector(part_sector - 1U, buf);
        return;
    }

    if ((part_sector >= (1U + MSC_SECTORS_PER_FAT)) &&
        (part_sector < (1U + (MSC_FAT_COUNT * MSC_SECTORS_PER_FAT)))) {
        build_fat_sector(part_sector - 1U - MSC_SECTORS_PER_FAT, buf);
        return;
    }

    if ((part_sector >= MSC_ROOT_DIR_FIRST_SECTOR) && (part_sector < MSC_DATA_FIRST_SECTOR)) {
        build_root_sector(part_sector - MSC_ROOT_DIR_FIRST_SECTOR, buf);
        return;
    }

    if (part_sector >= MSC_DATA_FIRST_SECTOR) {
        data_sector = part_sector - MSC_DATA_FIRST_SECTOR;
        cluster = data_sector + 2U;

        if ((cluster >= MSC_HTML_FIRST_CLUSTER) &&
            (cluster < (MSC_HTML_FIRST_CLUSTER + g_layout_cache.html_clusters))) {
            build_html_sector((cluster - MSC_HTML_FIRST_CLUSTER) * MSC_SECTOR_SIZE, buf);
            return;
        }
        if ((cluster >= g_layout_cache.csv_first) &&
            (cluster < (g_layout_cache.csv_first + g_layout_cache.csv_clusters))) {
            build_csv_sector((cluster - g_layout_cache.csv_first) * MSC_SECTOR_SIZE, buf);
            return;
        }
        if ((cluster >= g_layout_cache.readme_first) &&
            (cluster < (g_layout_cache.readme_first + g_layout_cache.readme_clusters))) {
            build_readme_sector((cluster - g_layout_cache.readme_first) * MSC_SECTOR_SIZE, buf);
            return;
        }
        if ((cluster >= g_layout_cache.power_first) &&
            (cluster < (g_layout_cache.power_first + g_layout_cache.power_clusters))) {
            build_power_sector((cluster - g_layout_cache.power_first) * MSC_SECTOR_SIZE, buf);
            return;
        }
    }
}

static void build_mbr_sector(uint8_t *buf)
{
    put_u32(buf, 440U, 0x544D5055U);
    buf[446] = 0x00U;
    buf[447] = 0x01U;
    buf[448] = 0x01U;
    buf[449] = 0x00U;
    buf[450] = 0x06U;
    buf[451] = 0xFEU;
    buf[452] = 0xFFU;
    buf[453] = 0xFFU;
    put_u32(buf, 454U, MSC_PARTITION_START_SECTOR);
    put_u32(buf, 458U, MSC_PARTITION_SECTORS);
    buf[510] = 0x55U;
    buf[511] = 0xAAU;
}

static void build_boot_sector(uint8_t *buf)
{
    static const uint8_t jump[3] = {0xEB, 0x3C, 0x90};
    static const uint8_t oem[8] = {'T', 'E', 'M', 'P', 'T', 'A', 'L', 'E'};
    static const uint8_t label[11] = {'T', 'e', 'm', 'p', 'L', 'o', 'g', 'P', 'r', 'o', ' '};
    static const uint8_t fs_type[8] = {'F', 'A', 'T', '1', '6', ' ', ' ', ' '};

    memcpy(&buf[0], jump, sizeof(jump));
    memcpy(&buf[3], oem, sizeof(oem));
    put_u16(buf, 11U, MSC_SECTOR_SIZE);
    buf[13] = 1U;
    put_u16(buf, 14U, MSC_RESERVED_SECTORS);
    buf[16] = MSC_FAT_COUNT;
    put_u16(buf, 17U, MSC_ROOT_ENTRY_COUNT);
    put_u16(buf, 19U, MSC_PARTITION_SECTORS);
    buf[21] = 0xF8U;
    put_u16(buf, 22U, MSC_SECTORS_PER_FAT);
    put_u16(buf, 24U, 32U);
    put_u16(buf, 26U, 64U);
    put_u32(buf, 28U, MSC_PARTITION_START_SECTOR);
    put_u32(buf, 32U, 0U);
    buf[36] = 0x80U;
    buf[37] = 0U;
    buf[38] = 0x29U;
    put_u32(buf, 39U, 0x544D5054U);
    memcpy(&buf[43], label, sizeof(label));
    memcpy(&buf[54], fs_type, sizeof(fs_type));
    buf[510] = 0x55U;
    buf[511] = 0xAAU;
}

static void build_fat_sector(uint32_t fat_sector, uint8_t *buf)
{
    uint32_t first_entry = fat_sector * (MSC_SECTOR_SIZE / 2U);
    uint32_t i;

    for (i = 0U; i < (MSC_SECTOR_SIZE / 2U); i++) {
        put_u16(buf, i * 2U, fat_entry_value(first_entry + i));
    }
}

static void build_root_sector(uint32_t root_sector, uint8_t *buf)
{
    static const char html_alias[11] = {'T','E','M','P','L','O','~','1','H','T','M'};
    static const char csv_name[11] = {'D','A','T','A',' ',' ',' ',' ','C','S','V'};
    static const char readme_name[11] = {'R','E','A','D','M','E',' ',' ','T','X','T'};
    static const char power_name[11] = {'P','O','W','E','R',' ',' ',' ','T','X','T'};
    uint8_t checksum;
    uint32_t offset;

    if (root_sector != 0U) {
        return;
    }

    memcpy(&buf[0], "TempLogPro ", 11U);
    buf[11] = 0x08U;
    put_u16(buf, 22U, 0x5A21U);
    put_u16(buf, 24U, 0x5B39U);

    checksum = lfn_checksum(html_alias);
    offset = 32U + write_lfn_name(buf, 32U, "TempLogPro.html", checksum);
    write_dir_entry(buf, offset, html_alias, MSC_HTML_FIRST_CLUSTER, g_layout_cache.html_size);
    write_dir_entry(buf, offset + 32U, csv_name, (uint16_t)g_layout_cache.csv_first, g_layout_cache.csv_size);
    write_dir_entry(buf, offset + 64U, readme_name, (uint16_t)g_layout_cache.readme_first, g_layout_cache.readme_size);
    write_dir_entry(buf, offset + 96U, power_name, (uint16_t)g_layout_cache.power_first, g_layout_cache.power_size);
}

static void build_html_sector(uint32_t file_offset, uint8_t *buf)
{
    (void)templogpro_html_read(file_offset, buf, MSC_SECTOR_SIZE);
}

static void build_csv_sector(uint32_t file_offset, uint8_t *buf)
{
    (void)templogpro_csv_read(file_offset, buf, MSC_SECTOR_SIZE);
}

static void build_readme_sector(uint32_t file_offset, uint8_t *buf)
{
    (void)templogpro_readme_read(file_offset, buf, MSC_SECTOR_SIZE);
}

static void build_power_sector(uint32_t file_offset, uint8_t *buf)
{
    (void)templogpro_power_read(file_offset, buf, MSC_SECTOR_SIZE);
}

static void write_dir_entry(uint8_t *buf, uint32_t offset, const char name[11], uint16_t cluster, uint32_t size)
{
    memcpy(&buf[offset], name, 11U);
    buf[offset + 11U] = 0x20U;
    put_u16(buf, offset + 14U, 0x0000U);
    put_u16(buf, offset + 16U, 0x5B39U);
    put_u16(buf, offset + 18U, 0x5B39U);
    put_u16(buf, offset + 20U, 0x0000U);
    put_u16(buf, offset + 22U, 0x5A21U);
    put_u16(buf, offset + 24U, 0x5B39U);
    put_u16(buf, offset + 26U, cluster);
    put_u32(buf, offset + 28U, size);
}

/* Long file name entries, last part first (VFAT order); 13 UCS-2 characters
   per entry, terminated with 0x0000 and padded with 0xFFFF. Returns the bytes
   written so the 8.3 entry can follow. */
static uint32_t write_lfn_name(uint8_t *buf, uint32_t offset, const char *name, uint8_t checksum)
{
    static const uint8_t map[13] = {
        1U, 3U, 5U, 7U, 9U, 14U, 16U, 18U, 20U, 22U, 24U, 28U, 30U
    };
    uint32_t len = (uint32_t)strlen(name);
    uint32_t count = (len + 12U) / 13U;
    uint32_t e;

    for (e = 0U; e < count; e++) {
        uint32_t seq = count - e;
        uint32_t at = offset + (e * 32U);
        uint32_t i;

        memset(&buf[at], 0xFF, 32U);
        buf[at] = (uint8_t)(seq | ((e == 0U) ? 0x40U : 0U));
        buf[at + 11U] = 0x0FU;
        buf[at + 12U] = 0x00U;
        buf[at + 13U] = checksum;
        put_u16(buf, at + 26U, 0U);
        for (i = 0U; i < 13U; i++) {
            uint32_t pos = ((seq - 1U) * 13U) + i;
            if (pos < len) {
                lfn_put_char(buf, at + map[i], name[pos]);
            } else if (pos == len) {
                put_u16(buf, at + map[i], 0x0000U);
            }
        }
    }
    return count * 32U;
}

static uint8_t lfn_checksum(const char name[11])
{
    uint8_t sum = 0U;
    uint32_t i;

    for (i = 0U; i < 11U; i++) {
        sum = (uint8_t)(((sum & 1U) ? 0x80U : 0U) + (sum >> 1U) + (uint8_t)name[i]);
    }
    return sum;
}

static void lfn_put_char(uint8_t *buf, uint32_t offset, char ch)
{
    buf[offset] = (uint8_t)ch;
    buf[offset + 1U] = 0U;
}

static void layout_cache_reset(void)
{
    memset(&g_layout_cache, 0, sizeof(g_layout_cache));
}

static void layout_cache_ensure(void)
{
    if (g_layout_cache.valid != 0U) {
        return;
    }

    templogpro_usb_files_snapshot_ensure();

    g_layout_cache.html_size = templogpro_html_get_size();
    g_layout_cache.csv_size = templogpro_csv_get_size();
    g_layout_cache.readme_size = templogpro_readme_get_size();
    g_layout_cache.power_size = templogpro_power_get_size();

    g_layout_cache.html_clusters = cluster_count(g_layout_cache.html_size);
    g_layout_cache.csv_first = MSC_HTML_FIRST_CLUSTER + g_layout_cache.html_clusters;
    g_layout_cache.csv_clusters = cluster_count(g_layout_cache.csv_size);
    g_layout_cache.readme_first = g_layout_cache.csv_first + g_layout_cache.csv_clusters;
    g_layout_cache.readme_clusters = cluster_count(g_layout_cache.readme_size);
    g_layout_cache.power_first = g_layout_cache.readme_first + g_layout_cache.readme_clusters;
    g_layout_cache.power_clusters = cluster_count(g_layout_cache.power_size);

    g_layout_cache.valid = 1U;
}

static uint32_t cluster_count(uint32_t size)
{
    uint32_t count = (size + MSC_SECTOR_SIZE - 1U) / MSC_SECTOR_SIZE;

    return (count == 0U) ? 1U : count;
}

static uint16_t fat_chain_entry_value(uint32_t entry, uint32_t first, uint32_t count)
{
    uint32_t last;

    if (count == 0U) {
        return 0x0000U;
    }
    last = first + count - 1U;
    if ((entry < first) || (entry > last)) {
        return 0x0000U;
    }
    if (entry == last) {
        return 0xFFFFU;
    }
    return (uint16_t)(entry + 1U);
}

static uint16_t fat_entry_value(uint32_t entry)
{
    uint16_t value;

    if (entry == 0U) {
        return 0xFFF8U;
    }
    if (entry == 1U) {
        return 0xFFFFU;
    }

    value = fat_chain_entry_value(entry, MSC_HTML_FIRST_CLUSTER, g_layout_cache.html_clusters);
    if (value != 0x0000U) {
        return value;
    }
    value = fat_chain_entry_value(entry, g_layout_cache.csv_first, g_layout_cache.csv_clusters);
    if (value != 0x0000U) {
        return value;
    }
    value = fat_chain_entry_value(entry, g_layout_cache.readme_first, g_layout_cache.readme_clusters);
    if (value != 0x0000U) {
        return value;
    }
    value = fat_chain_entry_value(entry, g_layout_cache.power_first, g_layout_cache.power_clusters);
    if (value != 0x0000U) {
        return value;
    }

    return 0x0000U;
}

static void put_u16(uint8_t *buf, uint32_t offset, uint16_t value)
{
    buf[offset] = (uint8_t)value;
    buf[offset + 1U] = (uint8_t)(value >> 8);
}

static void put_u32(uint8_t *buf, uint32_t offset, uint32_t value)
{
    buf[offset] = (uint8_t)value;
    buf[offset + 1U] = (uint8_t)(value >> 8);
    buf[offset + 2U] = (uint8_t)(value >> 16);
    buf[offset + 3U] = (uint8_t)(value >> 24);
}
