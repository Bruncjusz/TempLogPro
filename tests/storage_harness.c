/*
 * Runs the production logger and configuration code on a simulated NOR flash.
 * Built for Cortex-M3 and executed in unicorn by tests/storage_vectors.py.
 *
 * NOR model: erase sets a 4 KiB sector to 0xFF, program can only clear bits
 * (AND), and every write is verified like w25x40_page_program(). The Python
 * side can make the N-th program/erase operation lose power halfway through
 * (the emulator stops, RAM is wiped, NOR keeps its partial state) or make one
 * address fail to program.
 */
#include <stdint.h>
#include <stddef.h>
#include "templogpro_config.h"
#include "templogpro_diag.h"
#include "templogpro_hw_bridge.h"
#include "templogpro_logger.h"
#include "templogpro_rtc.h"
#include "templogpro_storage_layout.h"

#define NOR            ((volatile uint8_t *)0x60000000u)
#define HALT_ADDRESS   0x0801FFF1u

#define PHASE_WORKLOAD 1u
#define PHASE_VERIFY   2u
#define PHASE_LEGACY   3u

#define CHECK(cond) do { if (!(cond)) { return (uint32_t)__LINE__; } } while (0)

typedef struct
{
    /* inputs */
    uint32_t phase;
    uint32_t crash_at_op;        /* 0 = never */
    uint32_t stuck_address;      /* 0 = none; programs there never stick */
    uint32_t samples;
    uint32_t do_format;
    uint32_t now;
    uint32_t expect_records;     /* VERIFY: acknowledged slots before the crash */
    uint32_t expect_samples;     /* VERIFY: acknowledged measurements */
    uint32_t expect_config;      /* VERIFY: 0 none, else bitmask of accepted intervals */
    uint32_t legacy_count;       /* LEGACY: record_count in the v2 metadata */
    uint32_t loose;              /* VERIFY: interrupted CLEAR, the old log may return */
    /* outputs */
    uint32_t ops;
    uint32_t crashed;
    uint32_t acked_records;
    uint32_t acked_samples;
    uint32_t config_stage;
    uint32_t format_done;
    uint32_t final_records;
    uint32_t final_state;
    uint32_t meta_writes;
    uint32_t last_error;
} HarnessControl;

HarnessControl ctl;

void *memset(void *dst, int value, size_t len)
{
    uint8_t *d = dst;
    while (len--) {
        *d++ = (uint8_t)value;
    }
    return dst;
}

void *memcpy(void *dst, const void *src, size_t len)
{
    uint8_t *d = dst;
    const uint8_t *s = src;
    while (len--) {
        *d++ = *s++;
    }
    return dst;
}

int memcmp(const void *a, const void *b, size_t len)
{
    const uint8_t *x = a;
    const uint8_t *y = b;
    while (len--) {
        if (*x != *y) {
            return (int)*x - (int)*y;
        }
        x++;
        y++;
    }
    return 0;
}

static void power_loss(void)
{
    ctl.crashed = 1u;
    ((void (*)(void))HALT_ADDRESS)();
}

static uint32_t next_op_crashes(void)
{
    ctl.ops++;
    return (ctl.crash_at_op != 0u) && (ctl.ops == ctl.crash_at_op);
}

/* ---- hardware and service stubs used by the production code ---- */

uint32_t templogpro_hw_flash_init(void) { return 1u; }
uint32_t templogpro_hw_flash_is_busy(void) { return 0u; }

uint32_t templogpro_hw_flash_read(uint32_t address, uint8_t *buf, uint32_t len)
{
    uint32_t i;

    if ((address + len) > TEMPLOGPRO_FLASH_SIZE_BYTES) {
        return 0u;
    }
    for (i = 0u; i < len; i++) {
        buf[i] = NOR[address + i];
    }
    return 1u;
}

uint32_t templogpro_hw_flash_write(uint32_t address, const uint8_t *buf, uint32_t len)
{
    uint32_t i;

    if ((address + len) > TEMPLOGPRO_FLASH_SIZE_BYTES) {
        return 0u;
    }
    if ((address >= TEMPLOGPRO_METADATA_A_ADDRESS) && (address < TEMPLOGPRO_RECORDS_START_ADDRESS)) {
        ctl.meta_writes++;
    }
    while (len > 0u) {
        uint32_t chunk = TEMPLOGPRO_FLASH_PAGE_SIZE - (address & (TEMPLOGPRO_FLASH_PAGE_SIZE - 1u));
        if (chunk > len) {
            chunk = len;
        }
        if (next_op_crashes()) {
            for (i = 0u; i < (chunk + 1u) / 2u; i++) {
                NOR[address + i] &= (uint8_t)(buf[i] | 0x0Fu);
            }
            power_loss();
        }
        for (i = 0u; i < chunk; i++) {
            if ((ctl.stuck_address != 0u) &&
                (address + i >= ctl.stuck_address) &&
                (address + i < ctl.stuck_address + TEMPLOGPRO_LOG_RECORD_ENCODED_SIZE)) {
                continue;
            }
            NOR[address + i] &= buf[i];
        }
        for (i = 0u; i < chunk; i++) {
            if (NOR[address + i] != buf[i]) {
                return 0u;
            }
        }
        address += chunk;
        buf += chunk;
        len -= chunk;
    }
    return 1u;
}

uint32_t templogpro_hw_flash_erase_sector(uint32_t address)
{
    uint32_t base = address & ~(TEMPLOGPRO_FLASH_SECTOR_SIZE - 1u);
    uint32_t i;

    if (base >= TEMPLOGPRO_FLASH_SIZE_BYTES) {
        return 0u;
    }
    if (next_op_crashes()) {
        for (i = 0u; i < TEMPLOGPRO_FLASH_SECTOR_SIZE / 2u; i++) {
            NOR[base + i] = 0xFFu;
        }
        power_loss();
    }
    for (i = 0u; i < TEMPLOGPRO_FLASH_SECTOR_SIZE; i++) {
        NOR[base + i] = 0xFFu;
    }
    return 1u;
}

uint32_t templogpro_rtc_get_timestamp(uint32_t *timestamp)
{
    *timestamp = ctl.now;
    return 1u;
}

uint32_t templogpro_rtc_set_minimum_timestamp(uint32_t timestamp)
{
    if (ctl.now < timestamp) {
        ctl.now = timestamp;
    }
    return 1u;
}

void templogpro_rtc_clear_minimum_timestamp(void) { }

void templogpro_diag_set_flash(uint32_t ok) { (void)ok; }
void templogpro_diag_set_error(TempLogProError error) { ctl.last_error = (uint32_t)error; }
void templogpro_diag_set_logger(uint32_t state, uint32_t record_count) { (void)state; (void)record_count; }

/* ---- scenarios ---- */

static uint32_t set_interval_config(uint32_t interval)
{
    TempLogProConfig config;

    templogpro_config_get(&config);
    config.interval_seconds = interval;
    return templogpro_config_set(&config);
}

static uint32_t workload(void)
{
    uint32_t i;
    TempLogProLoggerInfo info;

    (void)templogpro_config_init();
    CHECK(set_interval_config(11u));
    ctl.config_stage = 1u;
    CHECK(templogpro_logger_init());
    if (ctl.do_format != 0u) {
        CHECK(templogpro_logger_format());
    }
    ctl.format_done = 1u;
    CHECK(templogpro_logger_start(10u));
    CHECK(templogpro_logger_get_info(&info));
    ctl.acked_records = info.record_count;

    for (i = 0u; i < ctl.samples; i++) {
        ctl.now += 10u;
        CHECK(templogpro_logger_append((int32_t)(ctl.acked_samples * 256u), TEMPLOGPRO_LOG_FLAG_VALID));
        ctl.acked_samples++;
        CHECK(templogpro_logger_get_info(&info));
        ctl.acked_records = info.record_count;
        if ((i % 150u) == 149u) {
            ctl.now += 5u;
            CHECK(templogpro_logger_stop());
            ctl.now += 30u;
            CHECK(templogpro_logger_resume(10u));
            CHECK(templogpro_logger_get_info(&info));
            ctl.acked_records = info.record_count;
        }
        if (i == (ctl.samples / 2u)) {
            CHECK(set_interval_config(22u));
            ctl.config_stage = 2u;
        }
    }
    CHECK(templogpro_logger_get_info(&info));
    ctl.final_records = info.record_count;
    ctl.final_state = (uint32_t)info.logger_state;
    return 0u;
}

static uint32_t check_log_consistent(uint32_t count, uint32_t *samples_out)
{
    TempLogProLogRecord record;
    uint32_t index;
    uint32_t last_ts = 0u;
    uint32_t samples = 0u;

    for (index = 0u; index < count; index++) {
        if (templogpro_logger_read_record(index, &record) == 0u) {
            continue;  /* skipped (zeroed) slot */
        }
        CHECK(record.sequence == (uint16_t)(index + 1u));
        CHECK(record.timestamp >= last_ts);
        last_ts = record.timestamp;
        if ((record.flags & (TEMPLOGPRO_LOG_FLAG_START | TEMPLOGPRO_LOG_FLAG_PAUSE |
                             TEMPLOGPRO_LOG_FLAG_RESUME)) == 0u) {
            /* measurements carry their ordinal, so a gap or a stale record shows */
            CHECK(record.temperature_x256 == (int16_t)(samples * 256u));
            samples++;
        }
    }
    *samples_out = samples;
    return 0u;
}

static uint32_t verify(void)
{
    TempLogProLoggerInfo info;
    TempLogProLoggerInfo again;
    TempLogProConfig config;
    uint32_t status;
    uint32_t samples;
    uint32_t result;
    uint32_t i;

    status = templogpro_config_init();
    templogpro_config_get(&config);
    if (ctl.expect_config != 0u) {
        CHECK(status == TEMPLOGPRO_CONFIG_STATUS_OK);
        CHECK(config.interval_seconds < 32u);
        CHECK(((1u << config.interval_seconds) & ctl.expect_config) != 0u);
    }

    CHECK(templogpro_logger_init());
    CHECK(templogpro_logger_get_info(&info));
    CHECK(info.record_count >= ctl.expect_records);
    CHECK((ctl.loose != 0u) || (info.record_count <= ctl.expect_records + 3u));
    CHECK((info.logger_state == TEMPLOGPRO_LOGGER_IDLE) ||
          (info.logger_state == TEMPLOGPRO_LOGGER_LOGGING) ||
          (info.logger_state == TEMPLOGPRO_LOGGER_STOPPED));
    result = check_log_consistent(info.record_count, &samples);
    if (result != 0u) {
        return result;
    }
    CHECK(samples >= ctl.expect_samples);
    CHECK((ctl.loose != 0u) || (samples <= ctl.expect_samples + 1u));

    /* A second boot without new writes must see exactly the same log. */
    CHECK(templogpro_logger_init());
    CHECK(templogpro_logger_get_info(&again));
    CHECK(again.record_count == info.record_count);
    CHECK(again.logger_state == info.logger_state);

    /* The logger must keep working after the power loss. */
    if (info.logger_state != TEMPLOGPRO_LOGGER_LOGGING) {
        ctl.now += 10u;
        CHECK(templogpro_logger_start(10u));
    }
    for (i = 0u; i < 40u; i++) {
        ctl.now += 10u;
        CHECK(templogpro_logger_append((int32_t)(samples * 256u), TEMPLOGPRO_LOG_FLAG_VALID));
        samples++;
    }
    CHECK(templogpro_logger_init());
    CHECK(templogpro_logger_get_info(&info));
    result = check_log_consistent(info.record_count, &i);
    if (result != 0u) {
        return result;
    }
    CHECK(i == samples);
    ctl.final_records = info.record_count;
    ctl.final_state = (uint32_t)info.logger_state;
    return 0u;
}

/* Old firmware left a partly programmed slot at legacy_count (the Err bug). */
static uint32_t legacy(void)
{
    TempLogProLoggerInfo info;
    TempLogProLogRecord record;
    uint32_t i;

    CHECK(templogpro_logger_init());
    CHECK(templogpro_logger_get_info(&info));
    CHECK(info.record_count == ctl.legacy_count + 1u);
    CHECK(info.logger_state == TEMPLOGPRO_LOGGER_LOGGING);
    for (i = 0u; i < 20u; i++) {
        ctl.now += 10u;
        CHECK(templogpro_logger_append(0x0100, TEMPLOGPRO_LOG_FLAG_VALID));
    }
    CHECK(templogpro_logger_init());
    CHECK(templogpro_logger_get_info(&info));
    CHECK(info.record_count == ctl.legacy_count + 21u);
    CHECK(templogpro_logger_read_record(ctl.legacy_count - 1u, &record));
    CHECK(record.sequence == (uint16_t)ctl.legacy_count);
    CHECK(templogpro_logger_read_record(ctl.legacy_count, &record) == 0u);
    CHECK(templogpro_logger_read_record(ctl.legacy_count + 1u, &record));
    CHECK(record.temperature_x256 == 0x0100);
    ctl.final_records = info.record_count;
    ctl.final_state = (uint32_t)info.logger_state;
    return 0u;
}

uint32_t run_tests(void)
{
    switch (ctl.phase) {
    case PHASE_WORKLOAD: return workload();
    case PHASE_VERIFY:   return verify();
    case PHASE_LEGACY:   return legacy();
    default:             return 1u;
    }
}
