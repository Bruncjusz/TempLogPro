#ifndef TEMPLOGPRO_POWER_H
#define TEMPLOGPRO_POWER_H

#include <stdint.h>

typedef struct
{
    uint32_t uptime_ms;
    uint32_t usb_time_ms;
    uint32_t battery_time_ms;
    uint32_t active_cpu_ms;
    uint32_t idle_sleep_ms;
    uint32_t stop_sleep_ms;
    uint32_t idle_entries;
    uint32_t stop_entries;
    uint32_t rtc_wakeups;
    uint32_t button_wakeups;
    uint32_t stop_deferred;
    uint32_t system_clock_hz;
    uint32_t usb_present;
    uint32_t cycle_counter_ok;
    uint32_t vdd_samples;
    uint16_t vdd_millivolts;
    uint16_t vdd_min_millivolts;
    uint16_t vdd_max_millivolts;
} TempLogProPowerStats;

uint32_t templogpro_power_init(void);
uint32_t templogpro_power_battery_clock_config(void);
void templogpro_power_process(void);
void templogpro_power_work_begin(void);
void templogpro_power_work_end(void);
void templogpro_power_enter_idle(void);
uint32_t templogpro_power_enter_stop(void);
uint32_t templogpro_power_stop_count(void);
void templogpro_power_system_reset(void);
uint32_t templogpro_power_read_vdd_mv(uint16_t *vdd_millivolts);
void templogpro_power_get_stats(TempLogProPowerStats *stats);

#endif
