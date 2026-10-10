#ifndef TEMPLOGPRO_RTC_H
#define TEMPLOGPRO_RTC_H

#include <stdint.h>

typedef struct
{
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
} TempLogProDateTime;

#define TEMPLOGPRO_RTC_STATUS_OK          1u
#define TEMPLOGPRO_RTC_STATUS_LSE_ERROR   2u
#define TEMPLOGPRO_RTC_STATUS_INIT_ERROR  3u
#define TEMPLOGPRO_RTC_STATUS_INVALID     4u

#define TEMPLOGPRO_TIME_TEXT_SIZE         512u

#define TEMPLOGPRO_RTC_EVENT_NONE             0u
#define TEMPLOGPRO_RTC_EVENT_TIME_SET         1u
#define TEMPLOGPRO_RTC_EVENT_TIME_GUARD       2u

typedef void (*TempLogProRtcWaitHook)(void);
typedef void (*TempLogProRtcClockSwitchHook)(void);

void templogpro_rtc_set_wait_hook(TempLogProRtcWaitHook hook);
void templogpro_rtc_set_clock_switch_hook(TempLogProRtcClockSwitchHook hook);
uint32_t templogpro_rtc_init(void);
void templogpro_rtc_process(void);
uint32_t templogpro_rtc_is_valid(void);
uint32_t templogpro_rtc_get(TempLogProDateTime *dt);
uint32_t templogpro_rtc_set(const TempLogProDateTime *dt);
uint32_t templogpro_rtc_get_timestamp(uint32_t *timestamp);
uint32_t templogpro_rtc_set_minimum_timestamp(uint32_t timestamp);
uint32_t templogpro_rtc_monotonic_guard_active(void);
uint32_t templogpro_rtc_monotonic_floor(void);
uint32_t templogpro_rtc_take_event(void);
uint32_t templogpro_rtc_datetime_to_timestamp(const TempLogProDateTime *dt, uint32_t *timestamp);
uint32_t templogpro_rtc_timestamp_to_datetime(uint32_t timestamp, TempLogProDateTime *dt);
const char *templogpro_rtc_source_name(void);
const char *templogpro_rtc_calibration_status_name(void);
uint32_t templogpro_rtc_lsi_frequency_hz(void);
const char *templogpro_rtc_time_status_name(void);
uint32_t templogpro_rtc_time_read_text(uint32_t file_offset, uint8_t *destination, uint32_t requested_length);
uint32_t templogpro_rtc_time_write_text(const uint8_t *source, uint32_t length);

#endif
