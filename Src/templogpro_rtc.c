#include "templogpro_rtc.h"
#include "templogpro_version.h"
#include "templogpro_diag.h"
#include <stdint.h>

uint32_t HAL_GetTick(void);

#define REG32(a)                    (*(volatile uint32_t *)(a))

#define RCC_CSR_REG                 REG32(0x40023834u)
#define RCC_APB1ENR_REG             REG32(0x40023824u)
#define RCC_APB2ENR_REG             REG32(0x40023820u)
#define PWR_CR_REG                  REG32(0x40007000u)

#define RTC_BASE                    0x40002800u
#define RTC_TR_REG                  REG32(RTC_BASE + 0x00u)
#define RTC_DR_REG                  REG32(RTC_BASE + 0x04u)
#define RTC_ISR_REG                 REG32(RTC_BASE + 0x0Cu)
#define RTC_PRER_REG                REG32(RTC_BASE + 0x10u)
#define RTC_WPR_REG                 REG32(RTC_BASE + 0x24u)
#define RTC_CALR_REG                REG32(RTC_BASE + 0x3Cu)
#define RTC_BKP0R_REG               REG32(RTC_BASE + 0x50u)
#define RTC_BKP7R_REG               REG32(RTC_BASE + 0x6Cu)
#define RTC_BKP8R_REG               REG32(RTC_BASE + 0x70u)
#define RTC_BKP9R_REG               REG32(RTC_BASE + 0x74u)
#define RTC_BKP10R_REG              REG32(RTC_BASE + 0x78u)
#define RTC_BKP11R_REG              REG32(RTC_BASE + 0x7Cu)

#define TIM10_BASE                  0x40010C00u
#define TIM10_CR1_REG               REG32(TIM10_BASE + 0x00u)
#define TIM10_SMCR_REG              REG32(TIM10_BASE + 0x08u)
#define TIM10_EGR_REG               REG32(TIM10_BASE + 0x14u)
#define TIM10_CNT_REG               REG32(TIM10_BASE + 0x24u)
#define TIM10_PSC_REG               REG32(TIM10_BASE + 0x28u)
#define TIM10_ARR_REG               REG32(TIM10_BASE + 0x2Cu)
#define TIM10_OR_REG                REG32(TIM10_BASE + 0x50u)

#define RCC_APB1ENR_PWREN_BIT       (1u << 28)
#define RCC_APB2ENR_TIM10EN_BIT     (1u << 3)
#define PWR_CR_DBP_BIT              (1u << 8)
#define RCC_CSR_LSEON_BIT           (1u << 8)
#define RCC_CSR_LSERDY_BIT          (1u << 9)
#define RCC_CSR_LSION_BIT           (1u << 0)
#define RCC_CSR_LSIRDY_BIT          (1u << 1)
#define RCC_CSR_RTCSEL_MASK         (3u << 16)
#define RCC_CSR_RTCSEL_LSE          (1u << 16)
#define RCC_CSR_RTCSEL_LSI          (2u << 16)
#define RCC_CSR_RTCEN_BIT           (1u << 22)
#define RCC_CSR_RTCRST_BIT          (1u << 23)

#define RTC_ISR_INIT_BIT            (1u << 7)
#define RTC_ISR_INITF_BIT           (1u << 6)
#define RTC_ISR_INITS_BIT           (1u << 4)
#define RTC_ISR_RECALPF_BIT         (1u << 16)

#define RTC_CALR_CALP_BIT           (1u << 15)
#define RTC_CALR_CALM_MASK          0x1FFu

#define TIM_CR1_CEN_BIT             (1u << 0)
#define TIM_SMCR_ECE_BIT            (1u << 14)
#define TIM_EGR_UG_BIT              (1u << 0)
#define TIM_OR_ETR_RMP_BIT          (1u << 2)

#define RTC_MAGIC                   0x54525443u
#define RTC_CALIB_MAGIC             0x5443414Cu
#define RTC_CALIB_CHECK_VALUE       0xA55A0FF0u
#define RTC_LSI_TIMEOUT             100000u
#define RTC_INIT_TIMEOUT            100000u
#define RTC_LSE_FREQUENCY_HZ        32768u
#define RTC_CALIBRATION_INTERVALS   16u
#define RTC_CALIBRATION_MIN_TICKS   18000u
#define RTC_CALIBRATION_MAX_TICKS   52000u
#define RTC_CALIBRATION_MIN_LSI_HZ  25000u
#define RTC_CALIBRATION_MAX_LSI_HZ  50000u
#define RTC_CALIBRATION_SCALE       1048576u

#define RTC_SOURCE_NONE             0u
#define RTC_SOURCE_LSE              1u
#define RTC_SOURCE_LSI              2u

#define RTC_CALIB_STATE_IDLE        0u
#define RTC_CALIB_STATE_WAIT_LSE    1u
#define RTC_CALIB_STATE_MEASURE     2u
#define RTC_CALIB_STATE_APPLY       3u
#define RTC_CALIB_STATE_CLEAR       4u
#define RTC_CALIB_STATE_RESTORE     5u

#define RTC_CALIB_STATUS_NOT_NEEDED 0u
#define RTC_CALIB_STATUS_WAIT_LSE   1u
#define RTC_CALIB_STATUS_MEASURING  2u
#define RTC_CALIB_STATUS_APPLYING   3u
#define RTC_CALIB_STATUS_OK         4u
#define RTC_CALIB_STATUS_RESTORED   5u
#define RTC_CALIB_STATUS_ERROR      6u

#define TIME_STATUS_IDLE            0u
#define TIME_STATUS_SET_OK          1u
#define TIME_STATUS_SET_FALSE       2u
#define TIME_STATUS_BAD_TEXT        3u
#define TIME_STATUS_BAD_DATE        4u
#define TIME_STATUS_RTC_ERROR       5u
#define TIME_STATUS_ROLLBACK_BLOCKED 6u

static uint32_t bcd_to_bin(uint32_t value);
static uint32_t bin_to_bcd(uint32_t value);
static uint32_t is_leap_year(uint32_t year);
static uint32_t days_in_month(uint32_t year, uint32_t month);
static uint32_t date_is_valid(const TempLogProDateTime *dt);
static uint32_t rtc_calendar_read(TempLogProDateTime *dt);
static uint32_t rtc_calendar_write(const TempLogProDateTime *dt);
static uint32_t rtc_raw_timestamp(uint32_t *timestamp);
static uint32_t rtc_enter_init_mode(void);
static void rtc_exit_init_mode(void);
static void rtc_write_unlock(void);
static void rtc_write_lock(void);
static uint32_t compile_month(const char *month);
static void compile_time_get(TempLogProDateTime *dt);
static uint32_t weekday_from_date(const TempLogProDateTime *dt);
static void rtc_set_prescaler_for_source(uint32_t source);
static uint32_t rtc_select_clock(uint32_t *source);
static void rtc_force_clock_source(uint32_t source);
static void rtc_switch_to_lse(void);
static uint32_t rtc_calibration_backup_valid(void);
static uint32_t rtc_calibration_backup_checksum(uint32_t prer, uint32_t calr, uint32_t lsi_hz);
static void rtc_calibration_init(void);
static void rtc_calibration_timer_start(void);
static void rtc_calibration_timer_stop(void);
static void rtc_calibration_lse_stop(void);
static uint32_t rtc_calibration_compute(uint32_t lsi_hz, uint32_t *prer, uint32_t *calr);
static uint32_t rtc_calibration_apply(void);
static uint32_t rtc_prescaler_product(uint32_t prer);
static uint32_t rtc_second_read(void);
static uint32_t append_str(char *buf, uint32_t pos, uint32_t max, const char *text);
static uint32_t append_u32_fixed(char *buf, uint32_t pos, uint32_t max, uint32_t value, uint32_t digits);
static void time_text_build(char *buf, uint32_t len);
static uint32_t time_text_parse(const uint8_t *source, uint32_t length, TempLogProDateTime *dt, uint32_t *set_requested);
static uint32_t parse_date_value(const uint8_t *value, uint32_t len, TempLogProDateTime *dt);
static uint32_t parse_time_value(const uint8_t *value, uint32_t len, TempLogProDateTime *dt);
static uint32_t key_equals(const uint8_t *text, uint32_t len, const char *key);
static uint32_t token_equals(const uint8_t *text, uint32_t len, const char *token);
static uint32_t is_space(uint8_t ch);

static uint32_t g_rtc_source = RTC_SOURCE_NONE;
static uint32_t g_time_status = TIME_STATUS_IDLE;
static uint32_t g_calibration_state = RTC_CALIB_STATE_IDLE;
static uint32_t g_calibration_status = RTC_CALIB_STATUS_NOT_NEEDED;
static uint32_t g_measured_lsi_hz;
static uint32_t g_calibration_tick_total;
static uint32_t g_calibration_interval_count;
static uint32_t g_calibration_last_second;
static uint32_t g_calibration_previous_counter;
static uint32_t g_calibration_have_edge;
static uint32_t g_calibration_pending_prer;
static uint32_t g_calibration_pending_calr;
static uint32_t g_monotonic_floor;
static uint32_t g_data_floor;      /* newest logged record: setting the clock earlier is refused */
static uint32_t g_last_raw_timestamp;
static uint32_t g_timestamp_offset;
static uint32_t g_monotonic_guard_active;
static uint32_t g_pending_rtc_events;
static uint32_t g_clock_switched;
#define RTC_BKP_SAVED_COUNT         20u   /* backup registers kept across the RTC domain reset */
static TempLogProRtcWaitHook g_rtc_wait_hook;
static TempLogProRtcClockSwitchHook g_rtc_clock_switch_hook;

void templogpro_rtc_set_wait_hook(TempLogProRtcWaitHook hook)
{
    g_rtc_wait_hook = hook;
}

void templogpro_rtc_set_clock_switch_hook(TempLogProRtcClockSwitchHook hook)
{
    g_rtc_clock_switch_hook = hook;
}

uint32_t templogpro_rtc_init(void)
{
    TempLogProDateTime saved_calendar;
    TempLogProDateTime current_calendar;
    uint32_t configured;
    uint32_t source;
    uint32_t current_source;
    uint32_t saved_calendar_valid;

    g_monotonic_floor = 0u;
    g_data_floor = 0u;
    g_last_raw_timestamp = 0u;
    g_timestamp_offset = 0u;
    g_monotonic_guard_active = 0u;
    g_pending_rtc_events = TEMPLOGPRO_RTC_EVENT_NONE;

    RCC_APB1ENR_REG |= RCC_APB1ENR_PWREN_BIT;
    PWR_CR_REG |= PWR_CR_DBP_BIT;

    saved_calendar_valid = rtc_calendar_read(&saved_calendar);

    current_source = RCC_CSR_REG & RCC_CSR_RTCSEL_MASK;
    if (((RCC_CSR_REG & RCC_CSR_RTCEN_BIT) != 0u) &&
        (current_source == RCC_CSR_RTCSEL_LSE) &&
        ((RCC_CSR_REG & RCC_CSR_LSERDY_BIT) == 0u)) {
        RCC_CSR_REG &= ~RCC_CSR_RTCEN_BIT;
        RCC_CSR_REG &= ~RCC_CSR_RTCSEL_MASK;
    }

    if (rtc_select_clock(&source) == 0u) {
        g_rtc_source = RTC_SOURCE_NONE;
        g_calibration_state = RTC_CALIB_STATE_IDLE;
        g_calibration_status = RTC_CALIB_STATUS_ERROR;
        templogpro_diag_set_rtc(0u);
        templogpro_diag_set_error(TEMPLOGPRO_ERROR_RTC_LSE);
        return TEMPLOGPRO_RTC_STATUS_INIT_ERROR;
    }
    g_rtc_source = source;

    configured = (((RCC_CSR_REG & RCC_CSR_RTCSEL_MASK) ==
                   ((source == RTC_SOURCE_LSE) ? RCC_CSR_RTCSEL_LSE : RCC_CSR_RTCSEL_LSI)) &&
                  ((RCC_CSR_REG & RCC_CSR_RTCEN_BIT) != 0u));

    if (configured == 0u) {
        if (g_rtc_clock_switch_hook != 0) {
            g_rtc_clock_switch_hook();
        }
        rtc_force_clock_source(source);
    }

    rtc_write_unlock();
    if (rtc_enter_init_mode() == 0u) {
        rtc_write_lock();
        templogpro_diag_set_rtc(0u);
        templogpro_diag_set_error(TEMPLOGPRO_ERROR_RTC_INVALID);
        return TEMPLOGPRO_RTC_STATUS_INIT_ERROR;
    }

    rtc_set_prescaler_for_source(source);
    rtc_exit_init_mode();
    rtc_write_lock();

    if ((configured == 0u) && (saved_calendar_valid != 0u)) {
        if (rtc_calendar_write(&saved_calendar) == 0u) {
            saved_calendar_valid = 0u;
        }
    }

    if (rtc_calendar_read(&current_calendar) != 0u) {
        RTC_BKP0R_REG = RTC_MAGIC;
    } else {
        TempLogProDateTime dt;
        compile_time_get(&dt);
        if (rtc_calendar_write(&dt) == 0u) {
            templogpro_diag_set_rtc(0u);
            templogpro_diag_set_error(TEMPLOGPRO_ERROR_RTC_INVALID);
            return TEMPLOGPRO_RTC_STATUS_INVALID;
        }
        RTC_BKP0R_REG = RTC_MAGIC;
    }

    rtc_calibration_init();
    templogpro_diag_set_rtc(1u);
    templogpro_diag_set_error(TEMPLOGPRO_ERROR_NONE);
    return TEMPLOGPRO_RTC_STATUS_OK;
}

void templogpro_rtc_process(void)
{
    uint32_t second;
    uint32_t counter;
    uint32_t expected_second;
    uint32_t delta;
    uint32_t product;
    uint64_t numerator;

    if (g_rtc_source != RTC_SOURCE_LSI) {
        return;
    }

    /* The internal LSI drifts with supply voltage and temperature (tens of %
       seen behind the battery diode). Once the 32.768 kHz crystal is running,
       the RTC moves onto it for good; LSI stays only as a start-up fallback. */
    if ((RCC_CSR_REG & RCC_CSR_LSERDY_BIT) != 0u) {
        rtc_switch_to_lse();
        return;
    }
    if ((RCC_CSR_REG & RCC_CSR_LSEON_BIT) == 0u) {
        RCC_CSR_REG |= RCC_CSR_LSEON_BIT;  /* keep the crystal starting */
    }

    if (g_calibration_state == RTC_CALIB_STATE_RESTORE) {
        if ((RTC_ISR_REG & RTC_ISR_RECALPF_BIT) != 0u) {
            return;
        }
        rtc_write_unlock();
        RTC_CALR_REG = RTC_BKP9R_REG;
        rtc_write_lock();
        g_calibration_state = RTC_CALIB_STATE_IDLE;
        g_calibration_status = RTC_CALIB_STATUS_RESTORED;
        rtc_calibration_lse_stop();
        return;
    }

    if (g_calibration_state == RTC_CALIB_STATE_CLEAR) {
        if ((RTC_ISR_REG & RTC_ISR_RECALPF_BIT) != 0u) {
            return;
        }
        if (RTC_CALR_REG != 0u) {
            rtc_write_unlock();
            RTC_CALR_REG = 0u;
            rtc_write_lock();
            return;
        }
        g_calibration_state = RTC_CALIB_STATE_WAIT_LSE;
        g_calibration_status = RTC_CALIB_STATUS_WAIT_LSE;
    }

    if (g_calibration_state == RTC_CALIB_STATE_WAIT_LSE) {
        if ((RCC_CSR_REG & RCC_CSR_LSERDY_BIT) == 0u) {
            return;
        }
        rtc_calibration_timer_start();
        g_calibration_last_second = rtc_second_read();
        g_calibration_tick_total = 0u;
        g_calibration_interval_count = 0u;
        g_calibration_have_edge = 0u;
        g_calibration_state = RTC_CALIB_STATE_MEASURE;
        g_calibration_status = RTC_CALIB_STATUS_MEASURING;
        return;
    }

    if (g_calibration_state == RTC_CALIB_STATE_MEASURE) {
        second = rtc_second_read();
        if ((second > 59u) || (second == g_calibration_last_second)) {
            return;
        }

        counter = TIM10_CNT_REG & 0xFFFFu;
        expected_second = (g_calibration_last_second + 1u) % 60u;
        g_calibration_last_second = second;

        if (second != expected_second) {
            g_calibration_previous_counter = counter;
            g_calibration_tick_total = 0u;
            g_calibration_interval_count = 0u;
            g_calibration_have_edge = 1u;
            return;
        }

        if (g_calibration_have_edge == 0u) {
            g_calibration_previous_counter = counter;
            g_calibration_have_edge = 1u;
            return;
        }

        delta = (uint16_t)(counter - g_calibration_previous_counter);
        g_calibration_previous_counter = counter;
        if ((delta < RTC_CALIBRATION_MIN_TICKS) ||
            (delta > RTC_CALIBRATION_MAX_TICKS)) {
            g_calibration_tick_total = 0u;
            g_calibration_interval_count = 0u;
            return;
        }

        g_calibration_tick_total += delta;
        g_calibration_interval_count++;
        if (g_calibration_interval_count < RTC_CALIBRATION_INTERVALS) {
            return;
        }

        rtc_calibration_timer_stop();
        product = rtc_prescaler_product(RTC_PRER_REG);
        numerator = (uint64_t)RTC_LSE_FREQUENCY_HZ *
                    (uint64_t)product *
                    (uint64_t)g_calibration_interval_count;
        g_measured_lsi_hz = (uint32_t)((numerator +
                              ((uint64_t)g_calibration_tick_total / 2u)) /
                             (uint64_t)g_calibration_tick_total);
        if ((g_measured_lsi_hz < RTC_CALIBRATION_MIN_LSI_HZ) ||
            (g_measured_lsi_hz > RTC_CALIBRATION_MAX_LSI_HZ) ||
            (rtc_calibration_compute(g_measured_lsi_hz,
                                     &g_calibration_pending_prer,
                                     &g_calibration_pending_calr) == 0u)) {
            g_calibration_state = RTC_CALIB_STATE_IDLE;
            g_calibration_status = RTC_CALIB_STATUS_ERROR;
            rtc_calibration_lse_stop();
            return;
        }
        g_calibration_state = RTC_CALIB_STATE_APPLY;
        g_calibration_status = RTC_CALIB_STATUS_APPLYING;
    }

    if (g_calibration_state == RTC_CALIB_STATE_APPLY) {
        if ((RTC_ISR_REG & RTC_ISR_RECALPF_BIT) != 0u) {
            return;
        }
        if (rtc_calibration_apply() == 0u) {
            g_calibration_state = RTC_CALIB_STATE_IDLE;
            g_calibration_status = RTC_CALIB_STATUS_ERROR;
            rtc_calibration_lse_stop();
            return;
        }
        g_calibration_state = RTC_CALIB_STATE_IDLE;
        g_calibration_status = RTC_CALIB_STATUS_OK;
        rtc_calibration_lse_stop();
    }
}

uint32_t templogpro_rtc_is_valid(void)
{
    TempLogProDateTime dt;

    if (RTC_BKP0R_REG != RTC_MAGIC) {
        return 0u;
    }
    return rtc_calendar_read(&dt);
}

uint32_t templogpro_rtc_get(TempLogProDateTime *dt)
{
    uint32_t timestamp;

    if (dt == 0) {
        return 0u;
    }
    if (templogpro_rtc_get_timestamp(&timestamp) == 0u) {
        return 0u;
    }
    return templogpro_rtc_timestamp_to_datetime(timestamp, dt);
}

uint32_t templogpro_rtc_set(const TempLogProDateTime *dt)
{
    uint32_t timestamp;

    if (templogpro_rtc_datetime_to_timestamp(dt, &timestamp) == 0u) {
        g_time_status = TIME_STATUS_BAD_DATE;
        return 0u;
    }
    /* Only the log has to stay in order: a fast clock may be corrected back to
       any time after the newest record (and freely once the log is cleared). */
    if ((g_data_floor != 0u) && (timestamp < g_data_floor)) {
        g_time_status = TIME_STATUS_ROLLBACK_BLOCKED;
        return 0u;
    }
    if (rtc_calendar_write(dt) == 0u) {
        g_time_status = TIME_STATUS_RTC_ERROR;
        return 0u;
    }

    g_timestamp_offset = 0u;
    g_last_raw_timestamp = timestamp;
    g_monotonic_floor = (timestamp > g_data_floor) ? timestamp : g_data_floor;
    g_monotonic_guard_active = 0u;
    g_pending_rtc_events |= TEMPLOGPRO_RTC_EVENT_TIME_SET;
    g_time_status = TIME_STATUS_SET_OK;
    templogpro_diag_set_rtc(1u);
    return 1u;
}

uint32_t templogpro_rtc_get_timestamp(uint32_t *timestamp)
{
    uint32_t raw_timestamp;
    uint32_t corrected_timestamp;
    uint32_t rollback;

    if (timestamp == 0) {
        return 0u;
    }
    if (templogpro_rtc_is_valid() == 0u) {
        return 0u;
    }
    if (rtc_raw_timestamp(&raw_timestamp) == 0u) {
        return 0u;
    }

    if ((g_last_raw_timestamp != 0u) && (raw_timestamp < g_last_raw_timestamp)) {
        rollback = g_last_raw_timestamp - raw_timestamp;
        if (g_timestamp_offset <= (0xFFFFFFFFu - rollback)) {
            g_timestamp_offset += rollback;
        } else {
            g_timestamp_offset = 0xFFFFFFFFu - raw_timestamp;
        }
        g_monotonic_guard_active = 1u;
        g_pending_rtc_events |= TEMPLOGPRO_RTC_EVENT_TIME_GUARD;
    }

    if (g_timestamp_offset > (0xFFFFFFFFu - raw_timestamp)) {
        corrected_timestamp = 0xFFFFFFFFu;
    } else {
        corrected_timestamp = raw_timestamp + g_timestamp_offset;
    }
    if (corrected_timestamp < g_monotonic_floor) {
        uint32_t correction = g_monotonic_floor - corrected_timestamp;

        if (g_timestamp_offset <= (0xFFFFFFFFu - correction)) {
            g_timestamp_offset += correction;
        } else {
            g_timestamp_offset = 0xFFFFFFFFu - raw_timestamp;
        }
        corrected_timestamp = g_monotonic_floor;
        g_monotonic_guard_active = 1u;
        g_pending_rtc_events |= TEMPLOGPRO_RTC_EVENT_TIME_GUARD;
    }

    g_last_raw_timestamp = raw_timestamp;
    if (corrected_timestamp > g_monotonic_floor) {
        g_monotonic_floor = corrected_timestamp;
    }
    *timestamp = corrected_timestamp;
    return 1u;
}

void templogpro_rtc_clear_minimum_timestamp(void)
{
    g_data_floor = 0u;
    g_monotonic_floor = 0u;
    g_timestamp_offset = 0u;
    g_monotonic_guard_active = 0u;
}

uint32_t templogpro_rtc_set_minimum_timestamp(uint32_t timestamp)
{
    uint32_t current_timestamp;

    if (timestamp == 0u) {
        return 1u;
    }
    if (timestamp > g_data_floor) {
        g_data_floor = timestamp;
    }
    if (templogpro_rtc_get_timestamp(&current_timestamp) == 0u) {
        return 0u;
    }
    if (current_timestamp < timestamp) {
        uint32_t correction = timestamp - current_timestamp;

        if (g_timestamp_offset <= (0xFFFFFFFFu - correction)) {
            g_timestamp_offset += correction;
        } else {
            g_timestamp_offset = 0xFFFFFFFFu;
        }
        g_monotonic_guard_active = 1u;
        g_pending_rtc_events |= TEMPLOGPRO_RTC_EVENT_TIME_GUARD;
    }
    if (timestamp > g_monotonic_floor) {
        g_monotonic_floor = timestamp;
    }
    return 1u;
}

uint32_t templogpro_rtc_monotonic_guard_active(void)
{
    return g_monotonic_guard_active;
}

uint32_t templogpro_rtc_monotonic_floor(void)
{
    return g_monotonic_floor;
}

uint32_t templogpro_rtc_take_event(void)
{
    uint32_t event;

    if ((g_pending_rtc_events & TEMPLOGPRO_RTC_EVENT_TIME_GUARD) != 0u) {
        event = TEMPLOGPRO_RTC_EVENT_TIME_GUARD;
    } else if ((g_pending_rtc_events & TEMPLOGPRO_RTC_EVENT_TIME_SET) != 0u) {
        event = TEMPLOGPRO_RTC_EVENT_TIME_SET;
    } else {
        return TEMPLOGPRO_RTC_EVENT_NONE;
    }
    g_pending_rtc_events &= ~event;
    return event;
}

uint32_t templogpro_rtc_datetime_to_timestamp(const TempLogProDateTime *dt, uint32_t *timestamp)
{
    uint32_t days = 0u;
    uint32_t year;
    uint32_t month;

    if ((timestamp == 0) || (date_is_valid(dt) == 0u)) {
        return 0u;
    }

    for (year = 1970u; year < dt->year; year++) {
        days += is_leap_year(year) ? 366u : 365u;
    }
    for (month = 1u; month < dt->month; month++) {
        days += days_in_month(dt->year, month);
    }
    days += (uint32_t)dt->day - 1u;

    *timestamp = (days * 86400u) +
                 ((uint32_t)dt->hour * 3600u) +
                 ((uint32_t)dt->minute * 60u) +
                 dt->second;
    return 1u;
}

uint32_t templogpro_rtc_timestamp_to_datetime(uint32_t timestamp, TempLogProDateTime *dt)
{
    uint32_t days;
    uint32_t seconds;
    uint32_t year;
    uint32_t month;
    uint32_t dim;

    if (dt == 0) {
        return 0u;
    }

    days = timestamp / 86400u;
    seconds = timestamp % 86400u;

    dt->hour = (uint8_t)(seconds / 3600u);
    seconds %= 3600u;
    dt->minute = (uint8_t)(seconds / 60u);
    dt->second = (uint8_t)(seconds % 60u);

    year = 1970u;
    while (1) {
        uint32_t diy = is_leap_year(year) ? 366u : 365u;
        if (days < diy) {
            break;
        }
        days -= diy;
        year++;
    }

    month = 1u;
    while (month <= 12u) {
        dim = days_in_month(year, month);
        if (days < dim) {
            break;
        }
        days -= dim;
        month++;
    }

    dt->year = (uint16_t)year;
    dt->month = (uint8_t)month;
    dt->day = (uint8_t)(days + 1u);
    return date_is_valid(dt);
}

const char *templogpro_rtc_source_name(void)
{
    if (g_rtc_source == RTC_SOURCE_LSE) {
        return "LSE";
    }
    if (g_rtc_source == RTC_SOURCE_LSI) {
        return "LSI_FALLBACK";
    }
    return "NONE";
}

const char *templogpro_rtc_calibration_status_name(void)
{
    switch (g_calibration_status) {
    case RTC_CALIB_STATUS_NOT_NEEDED: return "NOT_NEEDED";
    case RTC_CALIB_STATUS_WAIT_LSE: return "WAIT_LSE";
    case RTC_CALIB_STATUS_MEASURING: return "MEASURING";
    case RTC_CALIB_STATUS_APPLYING: return "APPLYING";
    case RTC_CALIB_STATUS_OK: return "OK";
    case RTC_CALIB_STATUS_RESTORED: return "RESTORED";
    case RTC_CALIB_STATUS_ERROR: return "ERROR";
    default: return "UNKNOWN";
    }
}

uint32_t templogpro_rtc_lsi_frequency_hz(void)
{
    return g_measured_lsi_hz;
}

const char *templogpro_rtc_time_status_name(void)
{
    switch (g_time_status) {
    case TIME_STATUS_IDLE: return "IDLE";
    case TIME_STATUS_SET_OK: return "SET_OK";
    case TIME_STATUS_SET_FALSE: return "SET_FALSE";
    case TIME_STATUS_BAD_TEXT: return "BAD_TEXT";
    case TIME_STATUS_BAD_DATE: return "BAD_DATE";
    case TIME_STATUS_RTC_ERROR: return "RTC_ERROR";
    case TIME_STATUS_ROLLBACK_BLOCKED: return "ROLLBACK_BLOCKED";
    default: return "UNKNOWN";
    }
}

uint32_t templogpro_rtc_time_read_text(uint32_t file_offset, uint8_t *destination, uint32_t requested_length)
{
    static char text[TEMPLOGPRO_TIME_TEXT_SIZE];
    uint32_t available;

    if (destination == 0) {
        return 0u;
    }

    time_text_build(text, sizeof(text));
    if (file_offset >= TEMPLOGPRO_TIME_TEXT_SIZE) {
        return 0u;
    }
    available = TEMPLOGPRO_TIME_TEXT_SIZE - file_offset;
    if (requested_length > available) {
        requested_length = available;
    }
    for (uint32_t i = 0u; i < requested_length; i++) {
        destination[i] = (uint8_t)text[file_offset + i];
    }
    return requested_length;
}

uint32_t templogpro_rtc_time_write_text(const uint8_t *source, uint32_t length)
{
    TempLogProDateTime dt;
    uint32_t set_requested = 0u;

    if ((source == 0) || (length == 0u)) {
        g_time_status = TIME_STATUS_BAD_TEXT;
        return 0u;
    }
    if (time_text_parse(source, length, &dt, &set_requested) == 0u) {
        g_time_status = TIME_STATUS_BAD_TEXT;
        return 0u;
    }
    if (set_requested == 0u) {
        g_time_status = TIME_STATUS_SET_FALSE;
        return 1u;
    }
    if (date_is_valid(&dt) == 0u) {
        g_time_status = TIME_STATUS_BAD_DATE;
        return 0u;
    }
    if (templogpro_rtc_set(&dt) == 0u) {
        g_time_status = TIME_STATUS_RTC_ERROR;
        return 0u;
    }
    g_time_status = TIME_STATUS_SET_OK;
    return 1u;
}

static uint32_t rtc_calibration_backup_checksum(uint32_t prer, uint32_t calr, uint32_t lsi_hz)
{
    return RTC_CALIB_MAGIC ^ RTC_CALIB_CHECK_VALUE ^ prer ^ calr ^ lsi_hz;
}

static uint32_t rtc_prescaler_product(uint32_t prer)
{
    uint32_t asynchronous_divider = ((prer >> 16) & 0x7Fu) + 1u;
    uint32_t synchronous_divider = (prer & 0x7FFFu) + 1u;

    return asynchronous_divider * synchronous_divider;
}

static uint32_t rtc_calibration_backup_valid(void)
{
    uint32_t prer;
    uint32_t calr;
    uint32_t lsi_hz;
    uint32_t product;

    if (RTC_BKP7R_REG != RTC_CALIB_MAGIC) {
        return 0u;
    }

    prer = RTC_BKP8R_REG;
    calr = RTC_BKP9R_REG;
    lsi_hz = RTC_BKP10R_REG;
    product = rtc_prescaler_product(prer);
    if ((lsi_hz < RTC_CALIBRATION_MIN_LSI_HZ) ||
        (lsi_hz > RTC_CALIBRATION_MAX_LSI_HZ) ||
        (product < RTC_CALIBRATION_MIN_LSI_HZ) ||
        (product > RTC_CALIBRATION_MAX_LSI_HZ) ||
        ((calr & ~(RTC_CALR_CALP_BIT | RTC_CALR_CALM_MASK)) != 0u)) {
        return 0u;
    }
    return (RTC_BKP11R_REG == rtc_calibration_backup_checksum(prer, calr, lsi_hz)) ? 1u : 0u;
}

static void rtc_calibration_init(void)
{
    rtc_calibration_timer_stop();
    g_calibration_tick_total = 0u;
    g_calibration_interval_count = 0u;
    g_calibration_have_edge = 0u;
    g_calibration_pending_prer = 0u;
    g_calibration_pending_calr = 0u;

    if (g_rtc_source != RTC_SOURCE_LSI) {
        g_measured_lsi_hz = 0u;
        g_calibration_state = RTC_CALIB_STATE_IDLE;
        g_calibration_status = RTC_CALIB_STATUS_NOT_NEEDED;
        return;
    }

    if (rtc_calibration_backup_valid() != 0u) {
        g_measured_lsi_hz = RTC_BKP10R_REG;
        if (RTC_CALR_REG == RTC_BKP9R_REG) {
            g_calibration_state = RTC_CALIB_STATE_IDLE;
            g_calibration_status = RTC_CALIB_STATUS_RESTORED;
        } else if ((RTC_ISR_REG & RTC_ISR_RECALPF_BIT) == 0u) {
            rtc_write_unlock();
            RTC_CALR_REG = RTC_BKP9R_REG;
            rtc_write_lock();
            g_calibration_state = RTC_CALIB_STATE_IDLE;
            g_calibration_status = RTC_CALIB_STATUS_RESTORED;
        } else {
            g_calibration_state = RTC_CALIB_STATE_RESTORE;
            g_calibration_status = RTC_CALIB_STATUS_APPLYING;
        }
        if (g_calibration_state == RTC_CALIB_STATE_IDLE) {
            rtc_calibration_lse_stop();
        }
        return;
    }

    g_measured_lsi_hz = 0u;
    if ((RTC_CALR_REG != 0u) || ((RTC_ISR_REG & RTC_ISR_RECALPF_BIT) != 0u)) {
        g_calibration_state = RTC_CALIB_STATE_CLEAR;
        g_calibration_status = RTC_CALIB_STATUS_APPLYING;
    } else {
        g_calibration_state = RTC_CALIB_STATE_WAIT_LSE;
        g_calibration_status = RTC_CALIB_STATUS_WAIT_LSE;
    }
}

static void rtc_calibration_timer_start(void)
{
    RCC_APB2ENR_REG |= RCC_APB2ENR_TIM10EN_BIT;
    TIM10_CR1_REG = 0u;
    TIM10_SMCR_REG = 0u;
    TIM10_OR_REG &= ~TIM_OR_ETR_RMP_BIT;
    TIM10_PSC_REG = 0u;
    TIM10_ARR_REG = 0xFFFFu;
    TIM10_CNT_REG = 0u;
    TIM10_EGR_REG = TIM_EGR_UG_BIT;
    TIM10_SMCR_REG = TIM_SMCR_ECE_BIT;
    TIM10_CR1_REG = TIM_CR1_CEN_BIT;
}

static void rtc_calibration_timer_stop(void)
{
    if ((RCC_APB2ENR_REG & RCC_APB2ENR_TIM10EN_BIT) != 0u) {
        TIM10_CR1_REG = 0u;
        TIM10_SMCR_REG = 0u;
        RCC_APB2ENR_REG &= ~RCC_APB2ENR_TIM10EN_BIT;
    }
}

static void rtc_calibration_lse_stop(void)
{
    /* The crystal is no longer stopped after calibrating LSI: the RTC switches
       to it as soon as it is ready (rtc_switch_to_lse). */
}

/* Changing RTCSEL needs an RTC domain reset, which clears the calendar and the
   backup registers: keep both, reset, select LSE, restore. The LCD runs from
   the same clock, so it is suspended first and re-initialised by the app. */
static void rtc_switch_to_lse(void)
{
    TempLogProDateTime now;
    uint32_t have_time;
    uint32_t saved[RTC_BKP_SAVED_COUNT];
    uint32_t i;
    uint32_t start_ms;

    rtc_calibration_timer_stop();
    have_time = rtc_calendar_read(&now);
    for (i = 0u; i < RTC_BKP_SAVED_COUNT; i++) {
        saved[i] = REG32(RTC_BASE + 0x50u + (i * 4u));
    }
    if (g_rtc_clock_switch_hook != 0) {
        g_rtc_clock_switch_hook();
    }
    RCC_APB1ENR_REG |= RCC_APB1ENR_PWREN_BIT;
    PWR_CR_REG |= PWR_CR_DBP_BIT;
    start_ms = HAL_GetTick();
    rtc_force_clock_source(RTC_SOURCE_LSE);
    /* RTCRST also clears LSEON: restart the crystal and wait (1-2 s) before the
       calendar can be written again; the time is advanced by the wait. */
    RCC_CSR_REG |= RCC_CSR_LSEON_BIT;
    while (((RCC_CSR_REG & RCC_CSR_LSERDY_BIT) == 0u) && ((HAL_GetTick() - start_ms) < 3000u)) {
        if (g_rtc_wait_hook != 0) {
            g_rtc_wait_hook();
        }
    }
    for (i = 0u; i < RTC_BKP_SAVED_COUNT; i++) {
        REG32(RTC_BASE + 0x50u + (i * 4u)) = saved[i];
    }
    if (have_time != 0u) {
        uint32_t ts;

        if (templogpro_rtc_datetime_to_timestamp(&now, &ts) != 0u) {
            (void)templogpro_rtc_timestamp_to_datetime(ts + ((HAL_GetTick() - start_ms + 500u) / 1000u), &now);
        }
    }
    if ((RCC_CSR_REG & RCC_CSR_LSERDY_BIT) == 0u) {
        /* The crystal did not come back: stay on LSI with the time kept. */
        rtc_force_clock_source(RTC_SOURCE_LSI);
        for (i = 0u; i < RTC_BKP_SAVED_COUNT; i++) {
            REG32(RTC_BASE + 0x50u + (i * 4u)) = saved[i];
        }
        rtc_write_unlock();
        if (rtc_enter_init_mode() != 0u) {
            rtc_set_prescaler_for_source(RTC_SOURCE_LSI);
            rtc_exit_init_mode();
        }
        rtc_write_lock();
        if (have_time != 0u) {
            (void)rtc_calendar_write(&now);
        }
        g_clock_switched = 1u;
        return;
    }
    g_rtc_source = RTC_SOURCE_LSE;
    g_measured_lsi_hz = 0u;
    g_calibration_state = RTC_CALIB_STATE_IDLE;
    g_calibration_status = RTC_CALIB_STATUS_NOT_NEEDED;
    rtc_write_unlock();
    if (rtc_enter_init_mode() != 0u) {
        rtc_set_prescaler_for_source(RTC_SOURCE_LSE);  /* 128 x 256 = 32768 */
        rtc_exit_init_mode();
    }
    rtc_write_lock();
    if (have_time != 0u) {
        (void)rtc_calendar_write(&now);
    }
    g_clock_switched = 1u;
}

uint32_t templogpro_rtc_take_clock_switched(void)
{
    uint32_t switched = g_clock_switched;

    g_clock_switched = 0u;
    return switched;
}

static uint32_t rtc_calibration_compute(uint32_t lsi_hz, uint32_t *prer, uint32_t *calr)
{
    uint32_t asynchronous_divider;

    if ((prer == 0) || (calr == 0) ||
        (lsi_hz < RTC_CALIBRATION_MIN_LSI_HZ) ||
        (lsi_hz > RTC_CALIBRATION_MAX_LSI_HZ)) {
        return 0u;
    }

    for (asynchronous_divider = 128u; asynchronous_divider != 0u; asynchronous_divider--) {
        uint32_t synchronous_divider;
        uint32_t product;
        int64_t correction_numerator;
        int64_t correction;

        synchronous_divider = (lsi_hz + (asynchronous_divider / 2u)) / asynchronous_divider;
        if ((synchronous_divider == 0u) || (synchronous_divider > 32768u)) {
            continue;
        }
        product = asynchronous_divider * synchronous_divider;
        correction_numerator = ((int64_t)product - (int64_t)lsi_hz) *
                               (int64_t)RTC_CALIBRATION_SCALE;
        if (correction_numerator >= 0) {
            correction = (correction_numerator + ((int64_t)lsi_hz / 2)) /
                         (int64_t)lsi_hz;
        } else {
            correction = -(((-correction_numerator) + ((int64_t)lsi_hz / 2)) /
                           (int64_t)lsi_hz);
        }
        if ((correction < -511) || (correction > 512)) {
            continue;
        }

        *prer = ((asynchronous_divider - 1u) << 16) |
                (synchronous_divider - 1u);
        if (correction > 0) {
            *calr = RTC_CALR_CALP_BIT | (uint32_t)(512 - correction);
        } else if (correction < 0) {
            *calr = (uint32_t)(-correction);
        } else {
            *calr = 0u;
        }
        return 1u;
    }

    return 0u;
}

static uint32_t rtc_calibration_apply(void)
{
    uint32_t checksum;

    rtc_write_unlock();
    if (rtc_enter_init_mode() == 0u) {
        rtc_write_lock();
        return 0u;
    }
    RTC_PRER_REG = g_calibration_pending_prer;
    rtc_exit_init_mode();
    RTC_CALR_REG = g_calibration_pending_calr;
    rtc_write_lock();

    if ((RTC_PRER_REG != g_calibration_pending_prer) ||
        ((RTC_CALR_REG & (RTC_CALR_CALP_BIT | RTC_CALR_CALM_MASK)) !=
         g_calibration_pending_calr)) {
        return 0u;
    }

    checksum = rtc_calibration_backup_checksum(g_calibration_pending_prer,
                                               g_calibration_pending_calr,
                                               g_measured_lsi_hz);
    RTC_BKP8R_REG = g_calibration_pending_prer;
    RTC_BKP9R_REG = g_calibration_pending_calr;
    RTC_BKP10R_REG = g_measured_lsi_hz;
    RTC_BKP11R_REG = checksum;
    RTC_BKP7R_REG = RTC_CALIB_MAGIC;
    return 1u;
}

static uint32_t rtc_second_read(void)
{
    uint32_t second = bcd_to_bin(RTC_TR_REG & 0x7Fu);

    return (second <= 59u) ? second : 0xFFFFFFFFu;
}

static uint32_t rtc_select_clock(uint32_t *source)
{
    volatile uint32_t timeout;
    uint32_t current_source;

    if (source == 0) {
        return 0u;
    }

    current_source = RCC_CSR_REG & RCC_CSR_RTCSEL_MASK;
    if (((RCC_CSR_REG & RCC_CSR_RTCEN_BIT) != 0u) &&
        (current_source == RCC_CSR_RTCSEL_LSE) &&
        ((RCC_CSR_REG & RCC_CSR_LSERDY_BIT) != 0u)) {
        *source = RTC_SOURCE_LSE;
        return 1u;
    }

    RCC_CSR_REG |= RCC_CSR_LSION_BIT;
    timeout = RTC_LSI_TIMEOUT;
    while (((RCC_CSR_REG & RCC_CSR_LSIRDY_BIT) == 0u) && (timeout != 0u)) {
        timeout--;
    }
    if ((RCC_CSR_REG & RCC_CSR_LSIRDY_BIT) != 0u) {
        RCC_CSR_REG |= RCC_CSR_LSEON_BIT;
        *source = RTC_SOURCE_LSI;
        return 1u;
    }

    return 0u;
}

static void rtc_force_clock_source(uint32_t source)
{
    volatile uint32_t timeout;

    if (source == RTC_SOURCE_LSE) {
        RCC_CSR_REG |= RCC_CSR_LSEON_BIT;
    } else {
        RCC_CSR_REG |= RCC_CSR_LSION_BIT;
        timeout = RTC_LSI_TIMEOUT;
        while (((RCC_CSR_REG & RCC_CSR_LSIRDY_BIT) == 0u) && (timeout != 0u)) {
            timeout--;
        }
    }

    RCC_CSR_REG &= ~RCC_CSR_RTCEN_BIT;
    RCC_CSR_REG |= RCC_CSR_RTCRST_BIT;
    RCC_CSR_REG &= ~RCC_CSR_RTCRST_BIT;
    RCC_CSR_REG &= ~RCC_CSR_RTCSEL_MASK;
    RCC_CSR_REG |= (source == RTC_SOURCE_LSE) ? RCC_CSR_RTCSEL_LSE : RCC_CSR_RTCSEL_LSI;
    RCC_CSR_REG |= RCC_CSR_RTCEN_BIT;
}

static void rtc_set_prescaler_for_source(uint32_t source)
{
    if (source == RTC_SOURCE_LSI) {
        RTC_PRER_REG = (rtc_calibration_backup_valid() != 0u) ?
                       RTC_BKP8R_REG : ((124u << 16) | 295u);
    } else {
        RTC_PRER_REG = (127u << 16) | 255u;
    }
}

static void time_text_build(char *buf, uint32_t len)
{
    TempLogProDateTime dt;
    uint32_t pos = 0u;
    uint32_t rtc_ok;

    if ((buf == 0) || (len == 0u)) {
        return;
    }

    for (uint32_t i = 0u; i < len; i++) {
        buf[i] = ' ';
    }

    rtc_ok = templogpro_rtc_get(&dt);
    if (rtc_ok == 0u) {
        compile_time_get(&dt);
    }

    pos = append_str(buf, pos, len, "# " TEMPLOGPRO_PRODUCT_NAME " TIME.TXT\r\n");
    pos = append_str(buf, pos, len, "# Set SET_TIME=TRUE, DATE=YYYY-MM-DD and TIME=HH:MM:SS, then eject USB.\r\n");
    pos = append_str(buf, pos, len, "SET_TIME=FALSE\r\nDATE=");
    pos = append_u32_fixed(buf, pos, len, dt.year, 4u);
    pos = append_str(buf, pos, len, "-");
    pos = append_u32_fixed(buf, pos, len, dt.month, 2u);
    pos = append_str(buf, pos, len, "-");
    pos = append_u32_fixed(buf, pos, len, dt.day, 2u);
    pos = append_str(buf, pos, len, "\r\nTIME=");
    pos = append_u32_fixed(buf, pos, len, dt.hour, 2u);
    pos = append_str(buf, pos, len, ":");
    pos = append_u32_fixed(buf, pos, len, dt.minute, 2u);
    pos = append_str(buf, pos, len, ":");
    pos = append_u32_fixed(buf, pos, len, dt.second, 2u);
    pos = append_str(buf, pos, len, "\r\nRTC_SOURCE=");
    pos = append_str(buf, pos, len, templogpro_rtc_source_name());
    pos = append_str(buf, pos, len, "\r\nRTC_CALIBRATION=");
    pos = append_str(buf, pos, len, templogpro_rtc_calibration_status_name());
    pos = append_str(buf, pos, len, "\r\nRTC_LSI_HZ=");
    if (g_measured_lsi_hz != 0u) {
        pos = append_u32_fixed(buf, pos, len, g_measured_lsi_hz, 5u);
    } else {
        pos = append_str(buf, pos, len, "UNKNOWN");
    }
    pos = append_str(buf, pos, len, "\r\nTIME_STATUS=");
    pos = append_str(buf, pos, len, templogpro_rtc_time_status_name());
    pos = append_str(buf, pos, len, "\r\nMONOTONIC_GUARD=");
    pos = append_str(buf, pos, len, g_monotonic_guard_active ? "ACTIVE" : "IDLE");
    pos = append_str(buf, pos, len, "\r\nMONOTONIC_FLOOR=");
    pos = append_u32_fixed(buf, pos, len, g_monotonic_floor, 10u);
    pos = append_str(buf, pos, len, "\r\nRTC_VALID=");
    pos = append_str(buf, pos, len, rtc_ok ? "YES" : "NO");
    pos = append_str(buf, pos, len, "\r\n");
    (void)pos;
}

static uint32_t time_text_parse(const uint8_t *source, uint32_t length, TempLogProDateTime *dt, uint32_t *set_requested)
{
    TempLogProDateTime parsed;
    uint32_t pos = 0u;
    uint32_t have_set = 0u;
    uint32_t set_value = 0u;
    uint32_t have_date = 0u;
    uint32_t have_time = 0u;

    if ((source == 0) || (dt == 0) || (set_requested == 0)) {
        return 0u;
    }

    parsed.year = 0u;
    parsed.month = 0u;
    parsed.day = 0u;
    parsed.hour = 0u;
    parsed.minute = 0u;
    parsed.second = 0u;
    *set_requested = 0u;

    while (pos < length) {
        uint32_t line_start = pos;
        uint32_t line_len;
        uint32_t key_start;
        uint32_t key_len;
        uint32_t value_start;
        uint32_t value_len;
        uint32_t equals;

        while ((pos < length) &&
               (source[pos] != '\r') &&
               (source[pos] != '\n') &&
               (source[pos] != 0u)) {
            pos++;
        }
        line_len = pos - line_start;
        while ((pos < length) &&
               ((source[pos] == '\r') || (source[pos] == '\n') || (source[pos] == 0u))) {
            pos++;
        }

        while ((line_len != 0u) && (is_space(source[line_start]) != 0u)) {
            line_start++;
            line_len--;
        }
        while ((line_len != 0u) && (is_space(source[line_start + line_len - 1u]) != 0u)) {
            line_len--;
        }
        if ((line_len == 0u) || (source[line_start] == '#')) {
            continue;
        }

        equals = 0xFFFFFFFFu;
        for (uint32_t i = 0u; i < line_len; i++) {
            if (source[line_start + i] == '=') {
                equals = i;
                break;
            }
        }
        if (equals == 0xFFFFFFFFu) {
            continue;
        }

        key_start = line_start;
        key_len = equals;
        value_start = line_start + equals + 1u;
        value_len = line_len - equals - 1u;

        while ((key_len != 0u) && (is_space(source[key_start]) != 0u)) {
            key_start++;
            key_len--;
        }
        while ((key_len != 0u) && (is_space(source[key_start + key_len - 1u]) != 0u)) {
            key_len--;
        }
        while ((value_len != 0u) && (is_space(source[value_start]) != 0u)) {
            value_start++;
            value_len--;
        }
        while ((value_len != 0u) && (is_space(source[value_start + value_len - 1u]) != 0u)) {
            value_len--;
        }

        if (key_equals(&source[key_start], key_len, "SET_TIME") != 0u) {
            have_set = 1u;
            if ((token_equals(&source[value_start], value_len, "TRUE") != 0u) ||
                (token_equals(&source[value_start], value_len, "YES") != 0u) ||
                (token_equals(&source[value_start], value_len, "ON") != 0u) ||
                ((value_len == 1u) && (source[value_start] == '1'))) {
                set_value = 1u;
            } else if ((token_equals(&source[value_start], value_len, "FALSE") != 0u) ||
                       (token_equals(&source[value_start], value_len, "NO") != 0u) ||
                       (token_equals(&source[value_start], value_len, "OFF") != 0u) ||
                       ((value_len == 1u) && (source[value_start] == '0'))) {
                set_value = 0u;
            } else {
                return 0u;
            }
        } else if (key_equals(&source[key_start], key_len, "DATE") != 0u) {
            if (parse_date_value(&source[value_start], value_len, &parsed) == 0u) {
                return 0u;
            }
            have_date = 1u;
        } else if (key_equals(&source[key_start], key_len, "TIME") != 0u) {
            if (parse_time_value(&source[value_start], value_len, &parsed) == 0u) {
                return 0u;
            }
            have_time = 1u;
        }
    }

    if (have_set == 0u) {
        return 0u;
    }
    if (set_value == 0u) {
        *set_requested = 0u;
        return 1u;
    }
    if ((have_date == 0u) || (have_time == 0u) || (date_is_valid(&parsed) == 0u)) {
        return 0u;
    }

    *dt = parsed;
    *set_requested = 1u;
    return 1u;
}

static uint32_t parse_date_value(const uint8_t *value, uint32_t len, TempLogProDateTime *dt)
{
    uint32_t year;
    uint32_t month;
    uint32_t day;

    if ((value == 0) || (dt == 0) || (len != 10u)) {
        return 0u;
    }
    if ((value[4] != '-') || (value[7] != '-')) {
        return 0u;
    }
    for (uint32_t i = 0u; i < len; i++) {
        if ((i == 4u) || (i == 7u)) {
            continue;
        }
        if ((value[i] < '0') || (value[i] > '9')) {
            return 0u;
        }
    }

    year = ((uint32_t)(value[0] - '0') * 1000u) +
           ((uint32_t)(value[1] - '0') * 100u) +
           ((uint32_t)(value[2] - '0') * 10u) +
           (uint32_t)(value[3] - '0');
    month = ((uint32_t)(value[5] - '0') * 10u) + (uint32_t)(value[6] - '0');
    day = ((uint32_t)(value[8] - '0') * 10u) + (uint32_t)(value[9] - '0');

    dt->year = (uint16_t)year;
    dt->month = (uint8_t)month;
    dt->day = (uint8_t)day;
    return 1u;
}

static uint32_t parse_time_value(const uint8_t *value, uint32_t len, TempLogProDateTime *dt)
{
    uint32_t hour;
    uint32_t minute;
    uint32_t second;

    if ((value == 0) || (dt == 0) || (len != 8u)) {
        return 0u;
    }
    if ((value[2] != ':') || (value[5] != ':')) {
        return 0u;
    }
    for (uint32_t i = 0u; i < len; i++) {
        if ((i == 2u) || (i == 5u)) {
            continue;
        }
        if ((value[i] < '0') || (value[i] > '9')) {
            return 0u;
        }
    }

    hour = ((uint32_t)(value[0] - '0') * 10u) + (uint32_t)(value[1] - '0');
    minute = ((uint32_t)(value[3] - '0') * 10u) + (uint32_t)(value[4] - '0');
    second = ((uint32_t)(value[6] - '0') * 10u) + (uint32_t)(value[7] - '0');

    if ((hour > 23u) || (minute > 59u) || (second > 59u)) {
        return 0u;
    }
    dt->hour = (uint8_t)hour;
    dt->minute = (uint8_t)minute;
    dt->second = (uint8_t)second;
    return 1u;
}

static uint32_t append_str(char *buf, uint32_t pos, uint32_t max, const char *text)
{
    while ((pos < max) && (*text != '\0')) {
        buf[pos++] = *text++;
    }
    return pos;
}

static uint32_t append_u32_fixed(char *buf, uint32_t pos, uint32_t max, uint32_t value, uint32_t digits)
{
    uint32_t div = 1u;

    for (uint32_t i = 1u; i < digits; i++) {
        div *= 10u;
    }
    while ((digits != 0u) && (pos < max)) {
        buf[pos++] = (char)('0' + ((value / div) % 10u));
        value %= div;
        div /= 10u;
        digits--;
    }
    return pos;
}

static uint32_t key_equals(const uint8_t *text, uint32_t len, const char *key)
{
    uint32_t i = 0u;

    if ((text == 0) || (key == 0)) {
        return 0u;
    }
    while (key[i] != '\0') {
        uint8_t a;
        uint8_t b;

        if (i >= len) {
            return 0u;
        }
        a = text[i];
        b = (uint8_t)key[i];
        if ((a >= 'a') && (a <= 'z')) {
            a = (uint8_t)(a - ('a' - 'A'));
        }
        if ((b >= 'a') && (b <= 'z')) {
            b = (uint8_t)(b - ('a' - 'A'));
        }
        if (a != b) {
            return 0u;
        }
        i++;
    }
    return (i == len) ? 1u : 0u;
}

static uint32_t token_equals(const uint8_t *text, uint32_t len, const char *token)
{
    return key_equals(text, len, token);
}

static uint32_t is_space(uint8_t ch)
{
    return ((ch == ' ') || (ch == '\t')) ? 1u : 0u;
}

static uint32_t bcd_to_bin(uint32_t value)
{
    return ((value >> 4) * 10u) + (value & 0x0Fu);
}

static uint32_t bin_to_bcd(uint32_t value)
{
    return ((value / 10u) << 4) | (value % 10u);
}

static uint32_t is_leap_year(uint32_t year)
{
    if ((year % 400u) == 0u) {
        return 1u;
    }
    if ((year % 100u) == 0u) {
        return 0u;
    }
    return ((year % 4u) == 0u) ? 1u : 0u;
}

static uint32_t days_in_month(uint32_t year, uint32_t month)
{
    static const uint8_t dim[12] = {
        31u, 28u, 31u, 30u, 31u, 30u, 31u, 31u, 30u, 31u, 30u, 31u
    };

    if ((month == 0u) || (month > 12u)) {
        return 0u;
    }
    if ((month == 2u) && (is_leap_year(year) != 0u)) {
        return 29u;
    }
    return dim[month - 1u];
}

static uint32_t date_is_valid(const TempLogProDateTime *dt)
{
    if (dt == 0) {
        return 0u;
    }
    if ((dt->year < 2000u) || (dt->year > 2099u)) {
        return 0u;
    }
    if ((dt->month < 1u) || (dt->month > 12u)) {
        return 0u;
    }
    if ((dt->day < 1u) || (dt->day > days_in_month(dt->year, dt->month))) {
        return 0u;
    }
    if ((dt->hour > 23u) || (dt->minute > 59u) || (dt->second > 59u)) {
        return 0u;
    }
    return 1u;
}

static uint32_t rtc_calendar_read(TempLogProDateTime *dt)
{
    uint32_t rtcsel;
    uint32_t tr;
    uint32_t dr;
    uint32_t year;

    if (dt == 0) {
        return 0u;
    }
    rtcsel = RCC_CSR_REG & RCC_CSR_RTCSEL_MASK;
    if (((RCC_CSR_REG & RCC_CSR_RTCEN_BIT) == 0u) ||
        ((rtcsel != RCC_CSR_RTCSEL_LSE) && (rtcsel != RCC_CSR_RTCSEL_LSI)) ||
        ((RTC_ISR_REG & RTC_ISR_INITS_BIT) == 0u)) {
        return 0u;
    }

    tr = RTC_TR_REG;
    dr = RTC_DR_REG;
    dt->second = (uint8_t)bcd_to_bin(tr & 0x7Fu);
    dt->minute = (uint8_t)bcd_to_bin((tr >> 8) & 0x7Fu);
    dt->hour = (uint8_t)bcd_to_bin((tr >> 16) & 0x3Fu);
    dt->day = (uint8_t)bcd_to_bin(dr & 0x3Fu);
    dt->month = (uint8_t)bcd_to_bin((dr >> 8) & 0x1Fu);
    year = bcd_to_bin((dr >> 16) & 0xFFu);
    dt->year = (uint16_t)(2000u + year);
    return date_is_valid(dt);
}

static uint32_t rtc_calendar_write(const TempLogProDateTime *dt)
{
    uint32_t tr;
    uint32_t dr;

    if (date_is_valid(dt) == 0u) {
        return 0u;
    }

    rtc_write_unlock();
    if (rtc_enter_init_mode() == 0u) {
        rtc_write_lock();
        return 0u;
    }

    tr = ((bin_to_bcd(dt->hour) & 0x3Fu) << 16) |
         ((bin_to_bcd(dt->minute) & 0x7Fu) << 8) |
         (bin_to_bcd(dt->second) & 0x7Fu);
    dr = ((bin_to_bcd((uint32_t)(dt->year - 2000u)) & 0xFFu) << 16) |
         ((weekday_from_date(dt) & 0x7u) << 13) |
         ((bin_to_bcd(dt->month) & 0x1Fu) << 8) |
         (bin_to_bcd(dt->day) & 0x3Fu);

    RTC_TR_REG = tr;
    RTC_DR_REG = dr;
    rtc_set_prescaler_for_source(g_rtc_source);
    rtc_exit_init_mode();
    rtc_write_lock();
    RTC_BKP0R_REG = RTC_MAGIC;
    return 1u;
}

static uint32_t rtc_raw_timestamp(uint32_t *timestamp)
{
    TempLogProDateTime dt;

    if ((timestamp == 0) || (rtc_calendar_read(&dt) == 0u)) {
        return 0u;
    }
    return templogpro_rtc_datetime_to_timestamp(&dt, timestamp);
}

static uint32_t rtc_enter_init_mode(void)
{
    volatile uint32_t timeout = RTC_INIT_TIMEOUT;

    RTC_ISR_REG = 0xFFFFFFFFu;
    while (((RTC_ISR_REG & RTC_ISR_INITF_BIT) == 0u) && (timeout != 0u)) {
        if (((timeout & 0x0FFFu) == 0u) && (g_rtc_wait_hook != 0)) {
            g_rtc_wait_hook();
        }
        timeout--;
    }
    return ((RTC_ISR_REG & RTC_ISR_INITF_BIT) != 0u) ? 1u : 0u;
}

static void rtc_exit_init_mode(void)
{
    volatile uint32_t timeout = RTC_INIT_TIMEOUT;

    RTC_ISR_REG = ~RTC_ISR_INIT_BIT;
    while (((RTC_ISR_REG & RTC_ISR_INIT_BIT) != 0u) && (timeout != 0u)) {
        if (((timeout & 0x0FFFu) == 0u) && (g_rtc_wait_hook != 0)) {
            g_rtc_wait_hook();
        }
        timeout--;
    }
}

static void rtc_write_unlock(void)
{
    RTC_WPR_REG = 0xCAu;
    RTC_WPR_REG = 0x53u;
}

static void rtc_write_lock(void)
{
    RTC_WPR_REG = 0xFFu;
}

static uint32_t compile_month(const char *month)
{
    static const char names[12][3] = {
        {'J','a','n'}, {'F','e','b'}, {'M','a','r'}, {'A','p','r'},
        {'M','a','y'}, {'J','u','n'}, {'J','u','l'}, {'A','u','g'},
        {'S','e','p'}, {'O','c','t'}, {'N','o','v'}, {'D','e','c'}
    };
    uint32_t i;

    for (i = 0u; i < 12u; i++) {
        if ((month[0] == names[i][0]) && (month[1] == names[i][1]) && (month[2] == names[i][2])) {
            return i + 1u;
        }
    }
    return 1u;
}

static void compile_time_get(TempLogProDateTime *dt)
{
    const char *date = __DATE__;
    const char *time = __TIME__;

    dt->year = (uint16_t)(((uint32_t)(date[7] - '0') * 1000u) +
                          ((uint32_t)(date[8] - '0') * 100u) +
                          ((uint32_t)(date[9] - '0') * 10u) +
                          (uint32_t)(date[10] - '0'));
    dt->month = (uint8_t)compile_month(date);
    dt->day = (uint8_t)((date[4] == ' ') ? (uint32_t)(date[5] - '0') :
              (((uint32_t)(date[4] - '0') * 10u) + (uint32_t)(date[5] - '0')));
    dt->hour = (uint8_t)(((uint32_t)(time[0] - '0') * 10u) + (uint32_t)(time[1] - '0'));
    dt->minute = (uint8_t)(((uint32_t)(time[3] - '0') * 10u) + (uint32_t)(time[4] - '0'));
    dt->second = (uint8_t)(((uint32_t)(time[6] - '0') * 10u) + (uint32_t)(time[7] - '0'));

    if (date_is_valid(dt) == 0u) {
        dt->year = 2026u;
        dt->month = 7u;
        dt->day = 25u;
        dt->hour = 0u;
        dt->minute = 0u;
        dt->second = 0u;
    }
}

static uint32_t weekday_from_date(const TempLogProDateTime *dt)
{
    uint32_t timestamp;
    uint32_t days;

    if (templogpro_rtc_datetime_to_timestamp(dt, &timestamp) == 0u) {
        return 1u;
    }
    days = timestamp / 86400u;
    return ((days + 4u) % 7u) + 1u;
}
