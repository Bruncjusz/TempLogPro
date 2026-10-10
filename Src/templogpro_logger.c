#include "templogpro_logger.h"
#include "templogpro_diag.h"
#include "templogpro_hw_bridge.h"
#include "templogpro_rtc.h"
#include "templogpro_storage_layout.h"
#include <string.h>

/*
 * Storage model (metadata v3):
 * - Records are the source of truth. Each record carries its timestamp, flags,
 *   sequence (index + 1) and CRC; START/PAUSE/RESUME records describe state.
 * - Metadata is a checkpoint, written only on state changes, interval changes
 *   and after a record sector has been erased. It is not written per sample.
 * - At boot the sector holding the checkpoint position is scanned up to the
 *   first erased slot, and state events found there are replayed.
 * - A record sector is erased only when the log enters it; the checkpoint
 *   written afterwards (flag SECTOR_READY) proves that the sector holds no
 *   stale records, so the boot scan may trust every programmed slot in it.
 * - A slot that is already programmed or fails verification is zeroed and
 *   skipped instead of blocking all further writes.
 */
#define LOGGER_MAGIC                 0x544C4F47u
#define LOGGER_VERSION               3u
#define LOGGER_V2_VERSION            2u
#define LOGGER_LEGACY_VERSION        1u
#define LOGGER_METADATA_SIZE         64u
#define LOGGER_METADATA_V1_SIZE      48u
#define LOGGER_METADATA_CRC_OFFSET   62u
#define LOGGER_METADATA_V1_CRC_OFFSET 46u
#define LOGGER_RECORD_CRC_OFFSET     10u
#define LOGGER_METADATA_SLOTS_PER_SECTOR \
    (TEMPLOGPRO_FLASH_SECTOR_SIZE / LOGGER_METADATA_SIZE)
#define LOGGER_STATS_READ_BATCH_RECORDS 32u
#define LOGGER_FLAG_SECTOR_READY     0x0001u
#define LOGGER_KNOWN_FLAGS           LOGGER_FLAG_SECTOR_READY
#define LOGGER_APPEND_MAX_SLOTS      4u

#define LOGGER_SLOT_WRITTEN          0u
#define LOGGER_SLOT_SKIP             1u
#define LOGGER_SLOT_FAILED           2u

#if ((TEMPLOGPRO_FLASH_SECTOR_SIZE % LOGGER_METADATA_SIZE) != 0u)
#error "Logger metadata slot must divide the Winbond sector size"
#endif

typedef struct
{
    uint32_t magic;
    uint16_t version;
    uint32_t generation;
    uint32_t session_id;
    uint32_t record_count;
    uint32_t next_write_address;
    uint32_t first_record_address;
    uint32_t logging_interval_seconds;
    uint32_t start_timestamp;
    uint32_t stop_timestamp;
    uint32_t active_seconds;
    uint32_t current_run_start_timestamp;
    uint32_t timestamp_high_watermark;
    uint32_t checkpoint_flags;
    TempLogProLoggerState logger_state;
} LoggerMetadata;

/* g_meta always holds the live state; metadata_save() persists a checkpoint. */
static LoggerMetadata g_meta;
static TempLogProLogStats g_stats_cache;
static int64_t g_stats_sum_x256;
static uint32_t g_logger_initialized;
static uint32_t g_stats_cache_valid;
static volatile uint32_t g_logger_flash_busy;
static uint32_t g_metadata_active_address;
static uint8_t g_metadata_sector_buf[TEMPLOGPRO_FLASH_SECTOR_SIZE];
static uint8_t g_stats_read_buf[LOGGER_STATS_READ_BATCH_RECORDS * TEMPLOGPRO_LOG_RECORD_ENCODED_SIZE];
static uint32_t g_stats_rebuild_active;
static uint32_t g_stats_rebuild_index;
static uint32_t g_stats_rebuild_run_start_timestamp;
static uint32_t g_stats_rebuild_active_seconds;
static uint32_t g_stats_rebuild_saw_run_event;

static uint16_t crc16_ccitt_local(const uint8_t *data, uint32_t len);
static void put_u16(uint8_t *buf, uint32_t offset, uint16_t value);
static void put_u32(uint8_t *buf, uint32_t offset, uint32_t value);
static uint16_t get_u16(const uint8_t *buf, uint32_t offset);
static uint32_t get_u32(const uint8_t *buf, uint32_t offset);
static void metadata_defaults(LoggerMetadata *meta);
static void metadata_encode(const LoggerMetadata *meta, uint8_t *buf);
static uint32_t metadata_decode(const uint8_t *buf, LoggerMetadata *meta);
static uint32_t metadata_read_latest(uint32_t sector_address,
                                     LoggerMetadata *meta,
                                     uint32_t *slot_address);
static uint32_t metadata_slot_is_erased(uint32_t address);
static uint32_t metadata_generation_is_newer(uint32_t candidate, uint32_t reference);
static uint32_t metadata_save(void);
static void metadata_checkpoint(void);
static void metadata_apply_event(LoggerMetadata *meta, const TempLogProLogRecord *record);
static void record_encode(const TempLogProLogRecord *record, uint8_t *buf);
static uint32_t record_decode(const uint8_t *buf, TempLogProLogRecord *record);
static uint32_t bytes_are_erased(const uint8_t *buf, uint32_t len);
static uint32_t log_sector_ready(const LoggerMetadata *meta);
static void log_advance(LoggerMetadata *meta);
static uint32_t log_scan_tail(void);
static uint32_t log_prepare_sector(void);
static uint32_t log_write_slot(const TempLogProLogRecord *record);
static void log_burn_slot(uint32_t address);
static uint32_t log_mark_full(void);
static uint32_t logger_flash_read(uint32_t address, uint8_t *buf, uint32_t len);
static uint32_t logger_flash_write(uint32_t address, const uint8_t *buf, uint32_t len);
static uint32_t logger_flash_erase(uint32_t address);
static uint32_t metadata_repair_layout(LoggerMetadata *meta);
static uint32_t metadata_is_sane(const LoggerMetadata *meta);
static void metadata_update_run_time(LoggerMetadata *meta, uint32_t timestamp);
static uint32_t metadata_live_active_seconds(const LoggerMetadata *meta);
static void stats_reset(void);
static void stats_add_record(const TempLogProLogRecord *record);
static void stats_rebuild_begin(void);
static uint32_t stats_rebuild_process_batch(void);
static void stats_rebuild_finish(void);

uint32_t templogpro_logger_init(void)
{
    LoggerMetadata a;
    LoggerMetadata b;
    TempLogProLogRecord last_record;
    uint8_t raw_record[TEMPLOGPRO_LOG_RECORD_ENCODED_SIZE];
    uint32_t address_a = 0u;
    uint32_t address_b = 0u;
    uint32_t valid_a;
    uint32_t valid_b;

    if (templogpro_hw_flash_init() == 0u) {
        templogpro_diag_set_flash(0u);
        templogpro_diag_set_error(TEMPLOGPRO_ERROR_FLASH_READ);
        return 0u;
    }
    templogpro_diag_set_flash(1u);

    memset(&a, 0, sizeof(a));
    memset(&b, 0, sizeof(b));
    valid_a = metadata_read_latest(TEMPLOGPRO_METADATA_A_ADDRESS, &a, &address_a);
    valid_b = metadata_read_latest(TEMPLOGPRO_METADATA_B_ADDRESS, &b, &address_b);

    if ((valid_a != 0u) && (valid_b != 0u)) {
        if (metadata_generation_is_newer(a.generation, b.generation) != 0u) {
            g_meta = a;
            g_metadata_active_address = address_a;
        } else {
            g_meta = b;
            g_metadata_active_address = address_b;
        }
    } else if (valid_a != 0u) {
        g_meta = a;
        g_metadata_active_address = address_a;
    } else if (valid_b != 0u) {
        g_meta = b;
        g_metadata_active_address = address_b;
    } else {
        metadata_defaults(&g_meta);
        g_metadata_active_address = 0u;
    }

    /* Records written after the last checkpoint, including state events. */
    if (log_scan_tail() != 0u) {
        metadata_checkpoint();
    }

    if ((g_meta.timestamp_high_watermark == 0u) && (g_meta.record_count != 0u)) {
        uint32_t last_address = templogpro_storage_record_address(g_meta.record_count - 1u);

        if ((logger_flash_read(last_address, raw_record, sizeof(raw_record)) != 0u) &&
            (record_decode(raw_record, &last_record) != 0u)) {
            g_meta.timestamp_high_watermark = last_record.timestamp;
        }
    }
    if (g_meta.timestamp_high_watermark != 0u) {
        (void)templogpro_rtc_set_minimum_timestamp(g_meta.timestamp_high_watermark);
    }

    g_logger_initialized = 1u;
    stats_rebuild_begin();
    templogpro_diag_set_logger((uint32_t)g_meta.logger_state, g_meta.record_count);
    return 1u;
}

void templogpro_logger_process(void)
{
    if ((g_logger_initialized == 0u) ||
        (g_stats_rebuild_active == 0u) ||
        (g_logger_flash_busy != 0u)) {
        return;
    }

    (void)stats_rebuild_process_batch();
}

uint32_t templogpro_logger_stats_ready(void)
{
    return (g_logger_initialized != 0u) &&
           (g_stats_rebuild_active == 0u) &&
           (g_stats_cache_valid != 0u);
}

uint32_t templogpro_logger_format(void)
{
    metadata_defaults(&g_meta);
    g_metadata_active_address = 0u;
    g_logger_initialized = 1u;
    stats_reset();
    g_stats_rebuild_active = 0u;
    g_stats_rebuild_index = 0u;

    if (logger_flash_erase(TEMPLOGPRO_METADATA_A_ADDRESS) == 0u) {
        templogpro_diag_set_error(TEMPLOGPRO_ERROR_FLASH_WRITE);
        return 0u;
    }
    if (logger_flash_erase(TEMPLOGPRO_METADATA_B_ADDRESS) == 0u) {
        templogpro_diag_set_error(TEMPLOGPRO_ERROR_FLASH_WRITE);
        return 0u;
    }
    if (metadata_save() == 0u) {
        templogpro_diag_set_error(TEMPLOGPRO_ERROR_LOG_METADATA);
        return 0u;
    }

    templogpro_diag_set_logger((uint32_t)g_meta.logger_state, g_meta.record_count);
    return 1u;
}

uint32_t templogpro_logger_start(uint32_t interval_seconds)
{
    if (g_logger_initialized == 0u) {
        if (templogpro_logger_init() == 0u) {
            return 0u;
        }
    }
    if (interval_seconds == 0u) {
        interval_seconds = 60u;
    }

    /* The START record applies the state change; the checkpoint only caches it. */
    g_meta.logging_interval_seconds = interval_seconds;
    if (templogpro_logger_append(0, (uint16_t)(TEMPLOGPRO_LOG_FLAG_START | TEMPLOGPRO_LOG_FLAG_VALID)) == 0u) {
        return 0u;
    }
    metadata_checkpoint();
    return 1u;
}

uint32_t templogpro_logger_resume(uint32_t interval_seconds)
{
    if (g_logger_initialized == 0u) {
        if (templogpro_logger_init() == 0u) {
            return 0u;
        }
    }
    if (interval_seconds == 0u) {
        interval_seconds = 60u;
    }

    g_meta.logging_interval_seconds = interval_seconds;
    if (templogpro_logger_append(0, (uint16_t)(TEMPLOGPRO_LOG_FLAG_RESUME | TEMPLOGPRO_LOG_FLAG_VALID)) == 0u) {
        return 0u;
    }
    metadata_checkpoint();
    return 1u;
}

uint32_t templogpro_logger_stop(void)
{
    if (g_logger_initialized == 0u) {
        return 0u;
    }

    if (templogpro_logger_append(0, (uint16_t)(TEMPLOGPRO_LOG_FLAG_PAUSE | TEMPLOGPRO_LOG_FLAG_VALID)) == 0u) {
        return 0u;
    }
    metadata_checkpoint();
    return 1u;
}

uint32_t templogpro_logger_set_interval(uint32_t interval_seconds)
{
    if (interval_seconds == 0u) {
        return 0u;
    }
    if (g_logger_initialized == 0u) {
        if (templogpro_logger_init() == 0u) {
            return 0u;
        }
    }
    if (g_meta.logging_interval_seconds == interval_seconds) {
        return 1u;
    }

    g_meta.logging_interval_seconds = interval_seconds;
    if (metadata_save() == 0u) {
        templogpro_diag_set_error(TEMPLOGPRO_ERROR_LOG_METADATA);
        return 0u;
    }

    templogpro_diag_set_logger((uint32_t)g_meta.logger_state, g_meta.record_count);
    return 1u;
}

uint32_t templogpro_logger_append(int32_t temperature_x256, uint16_t flags)
{
    TempLogProLogRecord record;
    uint32_t timestamp;
    uint32_t attempt;
    uint32_t result = LOGGER_SLOT_FAILED;

    if (g_logger_initialized == 0u) {
        if (templogpro_logger_init() == 0u) {
            return 0u;
        }
    }
    if (g_meta.record_count >= TEMPLOGPRO_LOG_RECORD_CAPACITY) {
        return log_mark_full();
    }
    if ((g_meta.timestamp_high_watermark != 0u) &&
        (templogpro_rtc_set_minimum_timestamp(g_meta.timestamp_high_watermark) == 0u)) {
        templogpro_diag_set_error(TEMPLOGPRO_ERROR_RTC_INVALID);
        return 0u;
    }
    if (templogpro_rtc_get_timestamp(&timestamp) == 0u) {
        templogpro_diag_set_error(TEMPLOGPRO_ERROR_RTC_INVALID);
        return 0u;
    }
    if ((g_meta.timestamp_high_watermark != 0u) &&
        (timestamp < g_meta.timestamp_high_watermark)) {
        templogpro_diag_set_error(TEMPLOGPRO_ERROR_RTC_INVALID);
        return 0u;
    }

    record.timestamp = timestamp;
    record.temperature_x256 = (int16_t)temperature_x256;
    record.flags = flags;
    record.crc = 0u;

    for (attempt = 0u; attempt < LOGGER_APPEND_MAX_SLOTS; attempt++) {
        if (g_meta.record_count >= TEMPLOGPRO_LOG_RECORD_CAPACITY) {
            return log_mark_full();
        }
        if (log_prepare_sector() == 0u) {
            return 0u;
        }
        record.sequence = (uint16_t)((g_meta.record_count + 1u) & 0xFFFFu);
        result = log_write_slot(&record);
        if (result == LOGGER_SLOT_FAILED) {
            return 0u;
        }
        log_advance(&g_meta);
        if (result == LOGGER_SLOT_WRITTEN) {
            break;
        }
    }
    if (result != LOGGER_SLOT_WRITTEN) {
        templogpro_diag_set_error(TEMPLOGPRO_ERROR_FLASH_WRITE);
        templogpro_diag_set_logger((uint32_t)g_meta.logger_state, g_meta.record_count);
        return 0u;
    }

    if (timestamp > g_meta.timestamp_high_watermark) {
        g_meta.timestamp_high_watermark = timestamp;
    }
    metadata_apply_event(&g_meta, &record);

    if ((g_stats_cache_valid != 0u) && (g_stats_rebuild_active == 0u)) {
        stats_add_record(&record);
    } else if (g_stats_rebuild_active == 0u) {
        stats_rebuild_begin();
    }
    templogpro_diag_set_logger((uint32_t)g_meta.logger_state, g_meta.record_count);
    return 1u;
}

uint32_t templogpro_logger_get_record_count(uint32_t *count)
{
    if (count == 0) {
        return 0u;
    }
    if (g_logger_initialized == 0u) {
        if (templogpro_logger_init() == 0u) {
            *count = 0u;
            return 0u;
        }
    }
    *count = g_meta.record_count;
    return 1u;
}

uint32_t templogpro_logger_read_record(uint32_t index, TempLogProLogRecord *record)
{
    uint8_t raw[TEMPLOGPRO_LOG_RECORD_ENCODED_SIZE];
    uint32_t address;

    if (record == 0) {
        return 0u;
    }
    if (g_logger_initialized == 0u) {
        if (templogpro_logger_init() == 0u) {
            return 0u;
        }
    }
    if (index >= g_meta.record_count) {
        return 0u;
    }
    if (g_logger_flash_busy != 0u) {
        return 0u;
    }

    address = templogpro_storage_record_address(index);
    if (logger_flash_read(address, raw, TEMPLOGPRO_LOG_RECORD_ENCODED_SIZE) == 0u) {
        templogpro_diag_set_error(TEMPLOGPRO_ERROR_FLASH_READ);
        return 0u;
    }
    if (record_decode(raw, record) == 0u) {
        templogpro_diag_set_error(TEMPLOGPRO_ERROR_FLASH_CRC);
        return 0u;
    }
    return 1u;
}

uint32_t templogpro_logger_get_info(TempLogProLoggerInfo *info)
{
    if (info == 0) {
        return 0u;
    }
    if (g_logger_initialized == 0u) {
        if (templogpro_logger_init() == 0u) {
            return 0u;
        }
    }

    info->magic = g_meta.magic;
    info->version = g_meta.version;
    info->generation = g_meta.generation;
    info->session_id = g_meta.session_id;
    info->record_count = g_meta.record_count;
    info->next_write_address = g_meta.next_write_address;
    info->first_record_address = g_meta.first_record_address;
    info->logging_interval_seconds = g_meta.logging_interval_seconds;
    info->start_timestamp = g_meta.start_timestamp;
    info->stop_timestamp = g_meta.stop_timestamp;
    info->active_seconds = metadata_live_active_seconds(&g_meta);
    info->current_run_start_timestamp = g_meta.current_run_start_timestamp;
    info->logger_state = g_meta.logger_state;
    info->capacity_records = TEMPLOGPRO_LOG_RECORD_CAPACITY;
    return 1u;
}

uint32_t templogpro_logger_get_stats(TempLogProLogStats *stats)
{
    if (stats == 0) {
        return 0u;
    }
    memset(stats, 0, sizeof(*stats));
    if (g_logger_initialized == 0u) {
        if (templogpro_logger_init() == 0u) {
            return 0u;
        }
    }
    if (g_logger_flash_busy != 0u) {
        return 0u;
    }

    if (g_stats_rebuild_active != 0u) {
        return 0u;
    }
    if (g_stats_cache_valid == 0u) {
        stats_rebuild_begin();
        if (g_stats_rebuild_active != 0u) {
            return 0u;
        }
    }

    *stats = g_stats_cache;
    return 1u;
}

uint32_t templogpro_logger_get_timestamp_high_watermark(uint32_t *timestamp)
{
    if (timestamp == 0) {
        return 0u;
    }
    if (g_logger_initialized == 0u) {
        if (templogpro_logger_init() == 0u) {
            *timestamp = 0u;
            return 0u;
        }
    }
    *timestamp = g_meta.timestamp_high_watermark;
    return 1u;
}

uint32_t templogpro_logger_flash_is_busy(void)
{
    return (g_logger_flash_busy != 0u) || (templogpro_hw_flash_is_busy() != 0u);
}

static uint16_t crc16_ccitt_local(const uint8_t *data, uint32_t len)
{
    uint16_t crc = 0xFFFFu;
    uint32_t i;
    uint32_t bit;

    for (i = 0u; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (bit = 0u; bit < 8u; bit++) {
            if ((crc & 0x8000u) != 0u) {
                crc = (uint16_t)((crc << 1) ^ 0x1021u);
            } else {
                crc = (uint16_t)(crc << 1);
            }
        }
    }
    return crc;
}

static void put_u16(uint8_t *buf, uint32_t offset, uint16_t value)
{
    buf[offset] = (uint8_t)value;
    buf[offset + 1u] = (uint8_t)(value >> 8);
}

static void put_u32(uint8_t *buf, uint32_t offset, uint32_t value)
{
    buf[offset] = (uint8_t)value;
    buf[offset + 1u] = (uint8_t)(value >> 8);
    buf[offset + 2u] = (uint8_t)(value >> 16);
    buf[offset + 3u] = (uint8_t)(value >> 24);
}

static uint16_t get_u16(const uint8_t *buf, uint32_t offset)
{
    return (uint16_t)(buf[offset] | ((uint16_t)buf[offset + 1u] << 8));
}

static uint32_t get_u32(const uint8_t *buf, uint32_t offset)
{
    return (uint32_t)buf[offset] |
           ((uint32_t)buf[offset + 1u] << 8) |
           ((uint32_t)buf[offset + 2u] << 16) |
           ((uint32_t)buf[offset + 3u] << 24);
}

static void metadata_defaults(LoggerMetadata *meta)
{
    meta->magic = LOGGER_MAGIC;
    meta->version = LOGGER_VERSION;
    meta->generation = 0u;
    meta->session_id = 0u;
    meta->record_count = 0u;
    meta->next_write_address = TEMPLOGPRO_RECORDS_START_ADDRESS;
    meta->first_record_address = TEMPLOGPRO_RECORDS_START_ADDRESS;
    meta->logging_interval_seconds = 60u;
    meta->start_timestamp = 0u;
    meta->stop_timestamp = 0u;
    meta->active_seconds = 0u;
    meta->current_run_start_timestamp = 0u;
    meta->timestamp_high_watermark = 0u;
    meta->checkpoint_flags = 0u;
    meta->logger_state = TEMPLOGPRO_LOGGER_IDLE;
}

static void metadata_encode(const LoggerMetadata *meta, uint8_t *buf)
{
    uint16_t crc;

    memset(buf, 0, LOGGER_METADATA_SIZE);
    put_u32(buf, 0u, meta->magic);
    put_u16(buf, 4u, meta->version);
    put_u16(buf, 6u, 0u);
    put_u32(buf, 8u, meta->generation);
    put_u32(buf, 12u, meta->session_id);
    put_u32(buf, 16u, meta->record_count);
    put_u32(buf, 20u, meta->next_write_address);
    put_u32(buf, 24u, meta->first_record_address);
    put_u32(buf, 28u, meta->logging_interval_seconds);
    put_u32(buf, 32u, meta->start_timestamp);
    put_u32(buf, 36u, meta->stop_timestamp);
    put_u16(buf, 40u, (uint16_t)meta->logger_state);
    put_u16(buf, 42u, 0u);
    put_u32(buf, 44u, meta->active_seconds);
    put_u32(buf, 48u, meta->current_run_start_timestamp);
    put_u32(buf, 52u, meta->timestamp_high_watermark);
    put_u16(buf, 56u, (uint16_t)meta->checkpoint_flags);
    put_u16(buf, 58u, 0u);
    put_u16(buf, 60u, 0u);
    crc = crc16_ccitt_local(buf, LOGGER_METADATA_CRC_OFFSET);
    put_u16(buf, LOGGER_METADATA_CRC_OFFSET, crc);
}

static uint32_t metadata_decode(const uint8_t *buf, LoggerMetadata *meta)
{
    uint16_t version;
    uint16_t stored_crc;
    uint16_t calculated_crc;
    uint16_t crc_offset;

    memset(meta, 0, sizeof(*meta));

    version = get_u16(buf, 4u);
    if (version == LOGGER_LEGACY_VERSION) {
        crc_offset = LOGGER_METADATA_V1_CRC_OFFSET;
    } else if ((version == LOGGER_V2_VERSION) || (version == LOGGER_VERSION)) {
        crc_offset = LOGGER_METADATA_CRC_OFFSET;
    } else {
        return 0u;
    }

    stored_crc = get_u16(buf, crc_offset);
    calculated_crc = crc16_ccitt_local(buf, crc_offset);
    if (stored_crc != calculated_crc) {
        return 0u;
    }

    meta->magic = get_u32(buf, 0u);
    meta->version = version;
    meta->generation = get_u32(buf, 8u);
    meta->session_id = get_u32(buf, 12u);
    meta->record_count = get_u32(buf, 16u);
    meta->next_write_address = get_u32(buf, 20u);
    meta->first_record_address = get_u32(buf, 24u);
    meta->logging_interval_seconds = get_u32(buf, 28u);
    meta->start_timestamp = get_u32(buf, 32u);
    meta->stop_timestamp = get_u32(buf, 36u);
    meta->logger_state = (TempLogProLoggerState)get_u16(buf, 40u);
    if (version != LOGGER_LEGACY_VERSION) {
        meta->active_seconds = get_u32(buf, 44u);
        meta->current_run_start_timestamp = get_u32(buf, 48u);
        meta->timestamp_high_watermark = get_u32(buf, 52u);
    } else {
        meta->active_seconds = 0u;
        meta->current_run_start_timestamp =
            (meta->logger_state == TEMPLOGPRO_LOGGER_LOGGING) ? meta->start_timestamp : 0u;
        meta->timestamp_high_watermark = 0u;
    }
    /* v1/v2 wrote metadata after every record; their sector was erased on entry. */
    meta->checkpoint_flags = (version == LOGGER_VERSION) ? get_u16(buf, 56u) : 0u;
    if (metadata_repair_layout(meta) == 0u) {
        return 0u;
    }

    return metadata_is_sane(meta);
}

static uint32_t metadata_read_latest(uint32_t sector_address,
                                     LoggerMetadata *meta,
                                     uint32_t *slot_address)
{
    LoggerMetadata candidate;
    uint32_t slot;
    uint32_t valid = 0u;

    if ((meta == 0) || (slot_address == 0)) {
        return 0u;
    }
    if (logger_flash_read(sector_address,
                          g_metadata_sector_buf,
                          TEMPLOGPRO_FLASH_SECTOR_SIZE) == 0u) {
        return 0u;
    }

    for (slot = 0u; slot < LOGGER_METADATA_SLOTS_PER_SECTOR; slot++) {
        const uint8_t *raw = &g_metadata_sector_buf[slot * LOGGER_METADATA_SIZE];

        memset(&candidate, 0, sizeof(candidate));
        if (metadata_decode(raw, &candidate) == 0u) {
            continue;
        }
        if ((valid == 0u) ||
            (metadata_generation_is_newer(candidate.generation, meta->generation) != 0u)) {
            *meta = candidate;
            *slot_address = sector_address + (slot * LOGGER_METADATA_SIZE);
            valid = 1u;
        }
    }
    return valid;
}

static uint32_t metadata_slot_is_erased(uint32_t address)
{
    uint8_t raw[LOGGER_METADATA_SIZE];

    if (logger_flash_read(address, raw, sizeof(raw)) == 0u) {
        return 0u;
    }
    return bytes_are_erased(raw, sizeof(raw));
}

static uint32_t metadata_generation_is_newer(uint32_t candidate, uint32_t reference)
{
    return ((int32_t)(candidate - reference) > 0) ? 1u : 0u;
}

static uint32_t metadata_save(void)
{
    uint8_t raw[LOGGER_METADATA_SIZE];
    uint32_t address;
    uint32_t sector_address;
    uint32_t next_address;
    uint32_t need_erase = 0u;

    g_meta.magic = LOGGER_MAGIC;
    g_meta.version = LOGGER_VERSION;
    g_meta.first_record_address = TEMPLOGPRO_RECORDS_START_ADDRESS;
    if (g_meta.logging_interval_seconds == 0u) {
        g_meta.logging_interval_seconds = 60u;
    }
    if (metadata_is_sane(&g_meta) == 0u) {
        return 0u;
    }

    g_meta.generation++;
    metadata_encode(&g_meta, raw);

    if ((g_metadata_active_address >= TEMPLOGPRO_METADATA_A_ADDRESS) &&
        (g_metadata_active_address < (TEMPLOGPRO_METADATA_A_ADDRESS + TEMPLOGPRO_FLASH_SECTOR_SIZE))) {
        sector_address = TEMPLOGPRO_METADATA_A_ADDRESS;
    } else if ((g_metadata_active_address >= TEMPLOGPRO_METADATA_B_ADDRESS) &&
               (g_metadata_active_address < (TEMPLOGPRO_METADATA_B_ADDRESS + TEMPLOGPRO_FLASH_SECTOR_SIZE))) {
        sector_address = TEMPLOGPRO_METADATA_B_ADDRESS;
    } else {
        sector_address = TEMPLOGPRO_METADATA_A_ADDRESS;
    }

    if (g_metadata_active_address != 0u) {
        next_address = g_metadata_active_address + LOGGER_METADATA_SIZE;
        if ((next_address < (sector_address + TEMPLOGPRO_FLASH_SECTOR_SIZE)) &&
            (metadata_slot_is_erased(next_address) != 0u)) {
            address = next_address;
        } else {
            sector_address = (sector_address == TEMPLOGPRO_METADATA_A_ADDRESS) ?
                TEMPLOGPRO_METADATA_B_ADDRESS : TEMPLOGPRO_METADATA_A_ADDRESS;
            address = sector_address;
            need_erase = 1u;
        }
    } else {
        address = sector_address;
        if (metadata_slot_is_erased(address) == 0u) {
            need_erase = 1u;
        }
    }

    if ((need_erase != 0u) && (logger_flash_erase(sector_address) == 0u)) {
        return 0u;
    }
    if (logger_flash_write(address, raw, LOGGER_METADATA_SIZE) == 0u) {
        return 0u;
    }
    g_metadata_active_address = address;
    return 1u;
}

/* A failed checkpoint is not fatal: the records written so far replay at boot. */
static void metadata_checkpoint(void)
{
    if (metadata_save() == 0u) {
        templogpro_diag_set_error(TEMPLOGPRO_ERROR_LOG_METADATA);
    }
    templogpro_diag_set_logger((uint32_t)g_meta.logger_state, g_meta.record_count);
}

/* Shared by live appends and the boot replay so both reach the same state. */
static void metadata_apply_event(LoggerMetadata *meta, const TempLogProLogRecord *record)
{
    if ((record->flags & TEMPLOGPRO_LOG_FLAG_START) != 0u) {
        meta->session_id++;
        if (meta->session_id == 0u) {
            meta->session_id = 1u;
        }
        meta->start_timestamp = record->timestamp;
        meta->stop_timestamp = 0u;
        meta->active_seconds = 0u;
        meta->current_run_start_timestamp = record->timestamp;
        meta->logger_state = TEMPLOGPRO_LOGGER_LOGGING;
    } else if ((record->flags & TEMPLOGPRO_LOG_FLAG_RESUME) != 0u) {
        if (meta->start_timestamp == 0u) {
            meta->start_timestamp = record->timestamp;
        }
        meta->stop_timestamp = 0u;
        meta->current_run_start_timestamp = record->timestamp;
        meta->logger_state = TEMPLOGPRO_LOGGER_LOGGING;
    } else if ((record->flags & TEMPLOGPRO_LOG_FLAG_PAUSE) != 0u) {
        metadata_update_run_time(meta, record->timestamp);
        meta->stop_timestamp = record->timestamp;
        meta->logger_state = TEMPLOGPRO_LOGGER_STOPPED;
    }
}

static void record_encode(const TempLogProLogRecord *record, uint8_t *buf)
{
    uint16_t crc;

    memset(buf, 0, TEMPLOGPRO_LOG_RECORD_ENCODED_SIZE);
    put_u32(buf, 0u, record->timestamp);
    put_u16(buf, 4u, (uint16_t)record->temperature_x256);
    put_u16(buf, 6u, record->flags);
    put_u16(buf, 8u, record->sequence);
    crc = crc16_ccitt_local(buf, LOGGER_RECORD_CRC_OFFSET);
    put_u16(buf, LOGGER_RECORD_CRC_OFFSET, crc);
}

static uint32_t record_decode(const uint8_t *buf, TempLogProLogRecord *record)
{
    uint16_t stored_crc = get_u16(buf, LOGGER_RECORD_CRC_OFFSET);
    uint16_t calculated_crc = crc16_ccitt_local(buf, LOGGER_RECORD_CRC_OFFSET);

    if (stored_crc != calculated_crc) {
        return 0u;
    }
    record->timestamp = get_u32(buf, 0u);
    record->temperature_x256 = (int16_t)get_u16(buf, 4u);
    record->flags = get_u16(buf, 6u);
    record->sequence = get_u16(buf, 8u);
    record->crc = stored_crc;
    return 1u;
}

static uint32_t bytes_are_erased(const uint8_t *buf, uint32_t len)
{
    uint32_t i;

    for (i = 0u; i < len; i++) {
        if (buf[i] != 0xFFu) {
            return 0u;
        }
    }
    return 1u;
}

/* True when the sector holding the next slot was erased by the current log. */
static uint32_t log_sector_ready(const LoggerMetadata *meta)
{
    if ((meta->record_count % TEMPLOGPRO_LOG_RECORDS_PER_SECTOR) != 0u) {
        return 1u;
    }
    return ((meta->checkpoint_flags & LOGGER_FLAG_SECTOR_READY) != 0u) ? 1u : 0u;
}

static void log_advance(LoggerMetadata *meta)
{
    meta->record_count++;
    meta->next_write_address = templogpro_storage_record_address(meta->record_count);
    if ((meta->record_count % TEMPLOGPRO_LOG_RECORDS_PER_SECTOR) == 0u) {
        meta->checkpoint_flags &= ~LOGGER_FLAG_SECTOR_READY;
    }
}

/*
 * Counts every slot after the checkpoint up to the last programmed one and
 * replays valid records. Slots are written strictly in order into a sector
 * erased on entry, so an erased slot before a programmed one is a hole left
 * by a failed write, not the end of the log.
 * Returns 1 when the live state moved past the stored checkpoint.
 */
static uint32_t log_scan_tail(void)
{
    TempLogProLogRecord record;
    uint32_t slot;
    uint32_t probe;
    uint32_t end;
    uint32_t changed = 0u;

    if ((g_meta.record_count >= TEMPLOGPRO_LOG_RECORD_CAPACITY) ||
        (log_sector_ready(&g_meta) == 0u)) {
        return 0u;
    }
    if (logger_flash_read(templogpro_storage_sector_base(g_meta.next_write_address),
                          g_metadata_sector_buf,
                          TEMPLOGPRO_FLASH_SECTOR_SIZE) == 0u) {
        templogpro_diag_set_error(TEMPLOGPRO_ERROR_FLASH_READ);
        return 0u;
    }

    slot = g_meta.record_count % TEMPLOGPRO_LOG_RECORDS_PER_SECTOR;
    end = slot;
    for (probe = slot; probe < TEMPLOGPRO_LOG_RECORDS_PER_SECTOR; probe++) {
        if (bytes_are_erased(&g_metadata_sector_buf[probe * TEMPLOGPRO_LOG_RECORD_ENCODED_SIZE],
                             TEMPLOGPRO_LOG_RECORD_ENCODED_SIZE) == 0u) {
            end = probe + 1u;
        }
    }

    while (slot < end) {
        const uint8_t *raw = &g_metadata_sector_buf[slot * TEMPLOGPRO_LOG_RECORD_ENCODED_SIZE];

        if ((record_decode(raw, &record) != 0u) &&
            (record.sequence == (uint16_t)((g_meta.record_count + 1u) & 0xFFFFu))) {
            if (record.timestamp > g_meta.timestamp_high_watermark) {
                g_meta.timestamp_high_watermark = record.timestamp;
            }
            metadata_apply_event(&g_meta, &record);
        }
        log_advance(&g_meta);
        slot++;
        changed = 1u;
    }
    return changed;
}

/* Erases a sector when the log enters it and records that fact durably. */
static uint32_t log_prepare_sector(void)
{
    if (log_sector_ready(&g_meta) != 0u) {
        return 1u;
    }
    if (logger_flash_erase(g_meta.next_write_address) == 0u) {
        templogpro_diag_set_error(TEMPLOGPRO_ERROR_FLASH_WRITE);
        return 0u;
    }
    g_meta.checkpoint_flags |= LOGGER_FLAG_SECTOR_READY;
    if (metadata_save() == 0u) {
        /* Without the checkpoint the boot scan would ignore this sector. */
        g_meta.checkpoint_flags &= ~LOGGER_FLAG_SECTOR_READY;
        templogpro_diag_set_error(TEMPLOGPRO_ERROR_LOG_METADATA);
        return 0u;
    }
    return 1u;
}

static uint32_t log_write_slot(const TempLogProLogRecord *record)
{
    uint8_t raw[TEMPLOGPRO_LOG_RECORD_ENCODED_SIZE];
    uint8_t current[TEMPLOGPRO_LOG_RECORD_ENCODED_SIZE];
    uint32_t address = g_meta.next_write_address;

    if (templogpro_storage_is_record_address(address) == 0u) {
        templogpro_diag_set_error(TEMPLOGPRO_ERROR_LOG_METADATA);
        return LOGGER_SLOT_FAILED;
    }
    if (logger_flash_read(address, current, sizeof(current)) == 0u) {
        templogpro_diag_set_error(TEMPLOGPRO_ERROR_FLASH_READ);
        return LOGGER_SLOT_FAILED;
    }
    if (bytes_are_erased(current, sizeof(current)) == 0u) {
        /* Left over from an interrupted write: never program over it. */
        templogpro_diag_set_error(TEMPLOGPRO_ERROR_LOG_RECORD);
        log_burn_slot(address);
        return LOGGER_SLOT_SKIP;
    }

    record_encode(record, raw);
    if (logger_flash_write(address, raw, sizeof(raw)) != 0u) {
        return LOGGER_SLOT_WRITTEN;
    }

    /* The chip answered a read, so the bus works and the slot itself failed.
       Zero it where possible; an erased hole is tolerated by the boot scan. */
    templogpro_diag_set_error(TEMPLOGPRO_ERROR_FLASH_WRITE);
    if (logger_flash_read(address, current, sizeof(current)) == 0u) {
        return LOGGER_SLOT_FAILED;
    }
    log_burn_slot(address);
    return LOGGER_SLOT_SKIP;
}

/* All-zero slots fail the record CRC and are counted but never decoded. */
static void log_burn_slot(uint32_t address)
{
    uint8_t zero[TEMPLOGPRO_LOG_RECORD_ENCODED_SIZE];

    memset(zero, 0, sizeof(zero));
    (void)logger_flash_write(address, zero, sizeof(zero));
}

static uint32_t log_mark_full(void)
{
    g_meta.logger_state = TEMPLOGPRO_LOGGER_ERROR;
    g_meta.current_run_start_timestamp = 0u;
    templogpro_diag_set_error(TEMPLOGPRO_ERROR_LOG_FULL);
    (void)metadata_save();
    templogpro_diag_set_logger((uint32_t)g_meta.logger_state, g_meta.record_count);
    return 0u;
}

static uint32_t logger_flash_read(uint32_t address, uint8_t *buf, uint32_t len)
{
    return templogpro_hw_flash_read(address, buf, len);
}

static uint32_t logger_flash_write(uint32_t address, const uint8_t *buf, uint32_t len)
{
    uint32_t ok;

    g_logger_flash_busy = 1u;
    ok = templogpro_hw_flash_write(address, buf, len);
    g_logger_flash_busy = 0u;
    return ok;
}

static uint32_t logger_flash_erase(uint32_t address)
{
    uint32_t ok;

    g_logger_flash_busy = 1u;
    ok = templogpro_hw_flash_erase_sector(address);
    g_logger_flash_busy = 0u;
    return ok;
}

static uint32_t metadata_repair_layout(LoggerMetadata *meta)
{
    uint32_t expected_next;
    uint32_t legacy_next;

    if (meta == 0) {
        return 0u;
    }
    if (meta->record_count > TEMPLOGPRO_LOG_RECORD_CAPACITY) {
        return 0u;
    }
    if (meta->first_record_address != TEMPLOGPRO_RECORDS_START_ADDRESS) {
        return 0u;
    }

    expected_next = templogpro_storage_record_address(meta->record_count);
    legacy_next = TEMPLOGPRO_RECORDS_START_ADDRESS +
                  (meta->record_count * TEMPLOGPRO_LOG_RECORD_ENCODED_SIZE);

    if (meta->next_write_address == expected_next) {
        return 1u;
    }
    if (meta->next_write_address == legacy_next) {
        meta->next_write_address = expected_next;
        return 1u;
    }

    return 0u;
}

static uint32_t metadata_is_sane(const LoggerMetadata *meta)
{
    uint32_t expected_next;

    if (meta->magic != LOGGER_MAGIC) {
        return 0u;
    }
    if ((meta->version != LOGGER_VERSION) &&
        (meta->version != LOGGER_V2_VERSION) &&
        (meta->version != LOGGER_LEGACY_VERSION)) {
        return 0u;
    }
    if (meta->record_count > TEMPLOGPRO_LOG_RECORD_CAPACITY) {
        return 0u;
    }
    if (meta->first_record_address != TEMPLOGPRO_RECORDS_START_ADDRESS) {
        return 0u;
    }
    expected_next = templogpro_storage_record_address(meta->record_count);
    if (meta->next_write_address != expected_next) {
        return 0u;
    }
    if (meta->next_write_address > TEMPLOGPRO_RECORDS_END_ADDRESS) {
        return 0u;
    }
    if (meta->logging_interval_seconds == 0u) {
        return 0u;
    }
    if ((meta->checkpoint_flags & ~LOGGER_KNOWN_FLAGS) != 0u) {
        return 0u;
    }
    if ((meta->logger_state != TEMPLOGPRO_LOGGER_IDLE) &&
        (meta->logger_state != TEMPLOGPRO_LOGGER_LOGGING) &&
        (meta->logger_state != TEMPLOGPRO_LOGGER_STOPPED) &&
        (meta->logger_state != TEMPLOGPRO_LOGGER_ERROR)) {
        return 0u;
    }
    if ((meta->logger_state != TEMPLOGPRO_LOGGER_LOGGING) &&
        (meta->current_run_start_timestamp != 0u)) {
        return 0u;
    }
    return 1u;
}

static void metadata_update_run_time(LoggerMetadata *meta, uint32_t timestamp)
{
    if (meta == 0) {
        return;
    }
    if ((meta->logger_state != TEMPLOGPRO_LOGGER_LOGGING) ||
        (meta->current_run_start_timestamp == 0u)) {
        meta->current_run_start_timestamp = 0u;
        return;
    }
    if (timestamp >= meta->current_run_start_timestamp) {
        meta->active_seconds += timestamp - meta->current_run_start_timestamp;
    }
    meta->current_run_start_timestamp = 0u;
}

static uint32_t metadata_live_active_seconds(const LoggerMetadata *meta)
{
    uint32_t now_ts;
    uint32_t active;

    if (meta == 0) {
        return 0u;
    }

    active = meta->active_seconds;
    if ((meta->logger_state == TEMPLOGPRO_LOGGER_LOGGING) &&
        (meta->current_run_start_timestamp != 0u) &&
        (templogpro_rtc_get_timestamp(&now_ts) != 0u) &&
        (now_ts >= meta->current_run_start_timestamp)) {
        active += now_ts - meta->current_run_start_timestamp;
    }
    return active;
}

static void stats_reset(void)
{
    memset(&g_stats_cache, 0, sizeof(g_stats_cache));
    g_stats_sum_x256 = 0;
    g_stats_cache.valid = 1u;
    g_stats_cache_valid = 1u;
}

static void stats_add_record(const TempLogProLogRecord *record)
{
    if (record == 0) {
        return;
    }

    if (g_stats_cache.valid == 0u) {
        stats_reset();
    }

    g_stats_cache.alarm_flags |=
        record->flags & (TEMPLOGPRO_LOG_FLAG_SENSOR_ERR |
                         TEMPLOGPRO_LOG_FLAG_TEMP_HIGH |
                         TEMPLOGPRO_LOG_FLAG_TEMP_LOW);

    if (g_stats_cache.first_timestamp == 0u) {
        g_stats_cache.first_timestamp = record->timestamp;
        g_stats_cache.last_timestamp = record->timestamp;
    } else {
        if (record->timestamp < g_stats_cache.first_timestamp) {
            g_stats_cache.first_timestamp = record->timestamp;
        }
        if (record->timestamp > g_stats_cache.last_timestamp) {
            g_stats_cache.last_timestamp = record->timestamp;
        }
    }

    if (((record->flags & TEMPLOGPRO_LOG_FLAG_VALID) != 0u) &&
        ((record->flags & (TEMPLOGPRO_LOG_FLAG_SENSOR_ERR |
                           TEMPLOGPRO_LOG_FLAG_START |
                           TEMPLOGPRO_LOG_FLAG_PAUSE |
                           TEMPLOGPRO_LOG_FLAG_RESUME |
                           TEMPLOGPRO_LOG_FLAG_TIME_SET |
                           TEMPLOGPRO_LOG_FLAG_TIME_GUARD)) == 0u)) {
        if (g_stats_cache.sample_count == 0u) {
            g_stats_cache.min_temperature_x256 = record->temperature_x256;
            g_stats_cache.max_temperature_x256 = record->temperature_x256;
        } else {
            if (record->temperature_x256 < g_stats_cache.min_temperature_x256) {
                g_stats_cache.min_temperature_x256 = record->temperature_x256;
            }
            if (record->temperature_x256 > g_stats_cache.max_temperature_x256) {
                g_stats_cache.max_temperature_x256 = record->temperature_x256;
            }
        }
        g_stats_sum_x256 += record->temperature_x256;
        g_stats_cache.sample_count++;
        g_stats_cache.avg_temperature_x256 =
            (int32_t)(g_stats_sum_x256 / (int64_t)g_stats_cache.sample_count);
    }

    if (record->timestamp > g_meta.timestamp_high_watermark) {
        g_meta.timestamp_high_watermark = record->timestamp;
        (void)templogpro_rtc_set_minimum_timestamp(record->timestamp);
    }

    if ((g_stats_cache.last_timestamp >= g_stats_cache.first_timestamp) &&
        (g_stats_cache.first_timestamp != 0u)) {
        g_stats_cache.duration_seconds = g_stats_cache.last_timestamp - g_stats_cache.first_timestamp;
    }
    g_stats_cache.valid = 1u;
    g_stats_cache_valid = 1u;
}

static void stats_rebuild_begin(void)
{
    stats_reset();
    g_stats_rebuild_index = 0u;
    g_stats_rebuild_run_start_timestamp = 0u;
    g_stats_rebuild_active_seconds = 0u;
    g_stats_rebuild_saw_run_event = 0u;
    g_stats_rebuild_active = (g_meta.record_count != 0u) ? 1u : 0u;

    if (g_stats_rebuild_active == 0u) {
        stats_rebuild_finish();
    }
}

static uint32_t stats_rebuild_process_batch(void)
{
    TempLogProLogRecord record;
    uint32_t slot;
    uint32_t batch_count;
    uint32_t sector_remaining;
    uint32_t address;
    uint32_t item;

    if (g_stats_rebuild_active == 0u) {
        return 1u;
    }
    if (g_logger_flash_busy != 0u) {
        return 0u;
    }
    if (g_stats_rebuild_index >= g_meta.record_count) {
        stats_rebuild_finish();
        return 1u;
    }

    slot = g_stats_rebuild_index % TEMPLOGPRO_LOG_RECORDS_PER_SECTOR;
    batch_count = g_meta.record_count - g_stats_rebuild_index;
    sector_remaining = TEMPLOGPRO_LOG_RECORDS_PER_SECTOR - slot;

    if (batch_count > LOGGER_STATS_READ_BATCH_RECORDS) {
        batch_count = LOGGER_STATS_READ_BATCH_RECORDS;
    }
    if (batch_count > sector_remaining) {
        batch_count = sector_remaining;
    }

    address = templogpro_storage_record_address(g_stats_rebuild_index);
    if (logger_flash_read(address,
                          g_stats_read_buf,
                          batch_count * TEMPLOGPRO_LOG_RECORD_ENCODED_SIZE) == 0u) {
        g_stats_rebuild_active = 0u;
        g_stats_cache_valid = 0u;
        g_stats_cache.valid = 0u;
        templogpro_diag_set_error(TEMPLOGPRO_ERROR_FLASH_READ);
        return 0u;
    }

    for (item = 0u; item < batch_count; item++) {
        const uint8_t *raw = &g_stats_read_buf[item * TEMPLOGPRO_LOG_RECORD_ENCODED_SIZE];

        if (record_decode(raw, &record) == 0u) {
            templogpro_diag_set_error(TEMPLOGPRO_ERROR_FLASH_CRC);
            continue;
        }
        if ((record.flags & TEMPLOGPRO_LOG_FLAG_START) != 0u) {
            g_stats_rebuild_run_start_timestamp = record.timestamp;
            g_stats_rebuild_saw_run_event = 1u;
        } else if ((record.flags & TEMPLOGPRO_LOG_FLAG_RESUME) != 0u) {
            if (g_stats_rebuild_run_start_timestamp == 0u) {
                g_stats_rebuild_run_start_timestamp = record.timestamp;
                g_stats_rebuild_saw_run_event = 1u;
            }
        } else if ((record.flags & TEMPLOGPRO_LOG_FLAG_PAUSE) != 0u) {
            if ((g_stats_rebuild_run_start_timestamp != 0u) &&
                (record.timestamp >= g_stats_rebuild_run_start_timestamp)) {
                g_stats_rebuild_active_seconds +=
                    record.timestamp - g_stats_rebuild_run_start_timestamp;
            }
            g_stats_rebuild_run_start_timestamp = 0u;
        }
        stats_add_record(&record);
    }

    g_stats_rebuild_index += batch_count;
    if (g_stats_rebuild_index >= g_meta.record_count) {
        stats_rebuild_finish();
    }
    return 1u;
}

static void stats_rebuild_finish(void)
{
    uint32_t live_active_seconds;
    uint32_t now_ts;

    if ((g_stats_rebuild_saw_run_event == 0u) &&
        (g_meta.start_timestamp != 0u) &&
        (g_meta.logger_state == TEMPLOGPRO_LOGGER_LOGGING)) {
        g_stats_rebuild_run_start_timestamp = g_meta.current_run_start_timestamp;
        if (g_stats_rebuild_run_start_timestamp == 0u) {
            g_stats_rebuild_run_start_timestamp = g_meta.start_timestamp;
        }
    }

    live_active_seconds = g_stats_rebuild_active_seconds;
    if ((g_meta.logger_state == TEMPLOGPRO_LOGGER_LOGGING) &&
        (g_stats_rebuild_run_start_timestamp != 0u) &&
        (templogpro_rtc_get_timestamp(&now_ts) != 0u) &&
        (now_ts >= g_stats_rebuild_run_start_timestamp)) {
        live_active_seconds += now_ts - g_stats_rebuild_run_start_timestamp;
    }

    if (g_meta.version == LOGGER_LEGACY_VERSION) {
        g_meta.active_seconds = g_stats_rebuild_active_seconds;
        g_meta.current_run_start_timestamp =
            (g_meta.logger_state == TEMPLOGPRO_LOGGER_LOGGING) ?
            g_stats_rebuild_run_start_timestamp : 0u;
    }

    g_stats_cache.total_seconds = g_stats_cache.duration_seconds;
    g_stats_cache.active_seconds = live_active_seconds;
    g_stats_cache.duration_seconds = live_active_seconds;
    g_stats_cache.valid = 1u;
    g_stats_cache_valid = 1u;
    g_stats_rebuild_active = 0u;
}
