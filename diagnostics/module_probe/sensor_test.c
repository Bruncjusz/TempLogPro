#include "sensor_test.h"
#include <stdio.h>

#define SENSOR_BASELINE 0x60A0u
#define SENSOR_SD 0x0100u
#define SENSOR_OS 0x8000u
#define SENSOR_STATE_MASK 0x7FDFu /* Ignore read-only OS/AL flags. */
static uint32_t recovery_pending;
static uint32_t recovery_last_ms;

static int baseline_family(uint16_t value)
{
    return (value & (SENSOR_STATE_MASK & ~SENSOR_SD)) ==
           (SENSOR_BASELINE & (SENSOR_STATE_MASK & ~SENSOR_SD));
}

/* Only reverse our SD change on the exact baseline configuration. No reset,
   threshold write, generic register write, or retry-until-success loop. */
int probe_sensor_recover(void)
{
    uint16_t cfg;
    for (unsigned attempt = 0; attempt < 3; ++attempt) {
        if (probe_sensor_read_word(1, &cfg) && baseline_family(cfg)) {
            if (!(cfg & SENSOR_SD)) {
                recovery_pending = 0;
                return 1;
            }
            (void)probe_sensor_write_config(SENSOR_BASELINE);
            probe_sensor_wait(20);
            if (probe_sensor_read_word(1, &cfg) && baseline_family(cfg) && !(cfg & SENSOR_SD)) {
                recovery_pending = 0;
                return 1;
            }
        }
    }
    recovery_pending = 1;
    return 0;
}

void probe_sensor_recovery_poll(void)
{
    if (recovery_pending && (uint32_t)(probe_sensor_tick() - recovery_last_ms) >= 1000u) {
        recovery_last_ms = probe_sensor_tick();
        (void)probe_sensor_recover();
    }
}

int probe_sensor_enter_sleep(void)
{
    uint16_t cfg;
    if (!probe_sensor_read_word(1, &cfg) || !baseline_family(cfg) || (cfg & SENSOR_SD)) return 0;
    if (!probe_sensor_write_config(SENSOR_BASELINE | SENSOR_SD)) return 0;
    return probe_sensor_read_word(1, &cfg) && (cfg & SENSOR_SD) != 0;
}

void probe_sensor_test(ProbeEmit emit)
{
    uint16_t original = 0, low = 0, high = 0, cfg = 0, frozen0 = 0, frozen1 = 0;
    uint16_t low_after = 0, high_after = 0;
    unsigned completed = 0, busy_seen = 0;
    int changed = 0, ok = 0, restored = 1, thresholds_ok = 0;
    char text[144];

    if (!probe_sensor_read_word(1, &original) || !baseline_family(original) ||
        (original & SENSOR_SD) || !probe_sensor_read_word(2, &low) ||
        !probe_sensor_read_word(3, &high)) {
        emit("TEST refused: baseline_not_verified; no_write\r\n");
        return;
    }
    snprintf(text, sizeof(text), "TEST original=%04X low=%04X high=%04X\r\n", original, low, high);
    emit(text);
    /* Even a NACK can follow a partial write. Cleanup is mandatory from here. */
    changed = 1;
    recovery_pending = 1;
    if (!probe_sensor_write_config(SENSOR_BASELINE | SENSOR_SD)) goto cleanup;
    probe_sensor_wait(30);
    if (!probe_sensor_read_word(1, &cfg) ||
        (cfg & SENSOR_STATE_MASK) != ((original | SENSOR_SD) & SENSOR_STATE_MASK) ||
        !(cfg & SENSOR_OS)) goto cleanup;
    snprintf(text, sizeof(text), "TEST shutdown=%04X\r\n", cfg);
    emit(text);
    if (!probe_sensor_read_word(0, &frozen0)) goto cleanup;
    probe_sensor_wait(100);
    if (!probe_sensor_read_word(0, &frozen1)) goto cleanup;
    snprintf(text, sizeof(text), "TEST frozen_before=%04X frozen_after=%04X hold_ms=100\r\n", frozen0, frozen1);
    emit(text);
    if (frozen0 != frozen1) goto cleanup;
    for (unsigned shot = 0; shot < 4; ++shot) {
        unsigned busy = 0, ready = 0, polls = 0;
        uint16_t temperature = 0;
        if (!probe_sensor_write_config(SENSOR_BASELINE | SENSOR_SD | SENSOR_OS)) goto cleanup;
        uint32_t start = probe_sensor_tick();
        while ((uint32_t)(probe_sensor_tick() - start) < 100u && polls < 40u) {
            polls++;
            if (!probe_sensor_read_word(1, &cfg) ||
                (cfg & SENSOR_STATE_MASK) != ((original | SENSOR_SD) & SENSOR_STATE_MASK)) goto cleanup;
            if (cfg & SENSOR_OS) { ready = 1; break; }
            busy = 1;
            probe_sensor_wait(1);
        }
        uint32_t elapsed = probe_sensor_tick() - start;
        if (!ready || !probe_sensor_read_word(0, &temperature)) goto cleanup;
        snprintf(text, sizeof(text), "SHOT n=%u busy=%u ready_ms=%lu polls=%u raw=%04X cfg=%04X\r\n",
                 shot + 1, busy, (unsigned long)elapsed, polls, temperature, cfg);
        emit(text);
        completed++;
        busy_seen += busy;
    }
    ok = (completed == 4u && busy_seen == 4u);
cleanup:
    if (changed) restored = probe_sensor_recover();
    int config_read = probe_sensor_read_word(1, &cfg);
    restored = restored && config_read && ((cfg & SENSOR_STATE_MASK) == (original & SENSOR_STATE_MASK));
    thresholds_ok = probe_sensor_read_word(2, &low_after) && probe_sensor_read_word(3, &high_after) &&
                    low == low_after && high == high_after;
    (void)probe_sensor_read_word(0, &frozen1);
    snprintf(text, sizeof(text), "TEST result=%s completed=%u busy_seen=%u restored=%u config=%04X thresholds_unchanged=%u\r\n",
             (ok && restored && thresholds_ok) ? "PASS" : "FAIL", completed, busy_seen,
             (unsigned)restored, cfg, (unsigned)thresholds_ok);
    emit(text);
    if (!restored) emit("ERROR SENSOR_RESTORE_FAILED; automatic_retry_active\r\n");
}
