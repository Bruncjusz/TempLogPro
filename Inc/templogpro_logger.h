#ifndef TEMPLOGPRO_LOGGER_H
#define TEMPLOGPRO_LOGGER_H

#include <stdint.h>

typedef enum
{
    TEMPLOGPRO_LOGGER_IDLE = 0,
    TEMPLOGPRO_LOGGER_LOGGING,
    TEMPLOGPRO_LOGGER_STOPPED,
    TEMPLOGPRO_LOGGER_ERROR
} TempLogProLoggerState;

typedef struct
{
    uint32_t timestamp;
    int16_t temperature_x256;
    uint16_t flags;
    uint16_t sequence;
    uint16_t crc;
} TempLogProLogRecord;

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
    TempLogProLoggerState logger_state;
    uint32_t capacity_records;
} TempLogProLoggerInfo;

typedef struct
{
    uint32_t valid;
    uint32_t sample_count;
    int32_t min_temperature_x256;
    int32_t max_temperature_x256;
    int32_t avg_temperature_x256;
    uint32_t first_timestamp;
    uint32_t last_timestamp;
    uint32_t duration_seconds;
    uint32_t active_seconds;
    uint32_t total_seconds;
    uint16_t alarm_flags;
} TempLogProLogStats;

#define TEMPLOGPRO_LOG_FLAG_VALID       0x0001u
#define TEMPLOGPRO_LOG_FLAG_SENSOR_ERR  0x0002u
#define TEMPLOGPRO_LOG_FLAG_START       0x0004u
#define TEMPLOGPRO_LOG_FLAG_PAUSE       0x0008u
#define TEMPLOGPRO_LOG_FLAG_STOP        TEMPLOGPRO_LOG_FLAG_PAUSE
#define TEMPLOGPRO_LOG_FLAG_TEMP_HIGH   0x0010u
#define TEMPLOGPRO_LOG_FLAG_TEMP_LOW    0x0020u
#define TEMPLOGPRO_LOG_FLAG_RESUME      0x0040u
#define TEMPLOGPRO_LOG_FLAG_TIME_SET    0x0080u
#define TEMPLOGPRO_LOG_FLAG_TIME_GUARD  0x0100u

uint32_t templogpro_logger_init(void);
void templogpro_logger_process(void);
uint32_t templogpro_logger_stats_ready(void);
uint32_t templogpro_logger_format(void);
uint32_t templogpro_logger_start(uint32_t interval_seconds);
uint32_t templogpro_logger_resume(uint32_t interval_seconds);
uint32_t templogpro_logger_stop(void);
uint32_t templogpro_logger_set_interval(uint32_t interval_seconds);
uint32_t templogpro_logger_append(int32_t temperature_x256, uint16_t flags);
uint32_t templogpro_logger_get_record_count(uint32_t *count);
uint32_t templogpro_logger_read_record(uint32_t index, TempLogProLogRecord *record);
uint32_t templogpro_logger_get_info(TempLogProLoggerInfo *info);
uint32_t templogpro_logger_get_stats(TempLogProLogStats *stats);
uint32_t templogpro_logger_get_timestamp_high_watermark(uint32_t *timestamp);
uint32_t templogpro_logger_flash_is_busy(void);

#endif
