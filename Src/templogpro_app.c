#include "templogpro_app.h"
#include "templogpro_tz.h"
#include "templogpro_buttons.h"
#include "templogpro_config.h"
#include "templogpro_diag.h"
#include "templogpro_hw_bridge.h"
#include "templogpro_logger.h"
#include "templogpro_power.h"
#include "templogpro_rtc.h"
#include "templogpro_sensor.h"
#include <stdint.h>
#include <string.h>

extern uint32_t HAL_GetTick(void);

#ifndef TEMPLOGPRO_IDLE_WFI_ENABLE
#define TEMPLOGPRO_IDLE_WFI_ENABLE 1
#endif

typedef enum
{
    LCD_VIEW_TEMPERATURE = 0,
    LCD_VIEW_STATE = 1,
    LCD_VIEW_TEMP_MIN = 2,
    LCD_VIEW_TEMP_MAX = 3,
    LCD_VIEW_TEMP_AVG = 4,
    LCD_VIEW_TEMP_LOW = 5,
    LCD_VIEW_TEMP_HIGH = 6,
    LCD_VIEW_RUNTIME = 7,
    LCD_VIEW_RECORDS = 8,
    LCD_VIEW_INTERVAL = 9,
    LCD_VIEW_BATTERY = 10,
    LCD_VIEW_RESET = 11,
    LCD_VIEW_CLOCK = 12,
    LCD_VIEW_OFF = 13,  /* retired card: OFF now lives on the battery card; menu bit 13 unused */
    LCD_VIEW_CLOCK_MODE = 14,  /* retired card (now on the clock card); menu bit 14 unused */
    LCD_VIEW_LEDS = 15,
    LCD_VIEW_ECO = 16,
    LCD_VIEW_ICON_SCAN = 17
} LcdView;

typedef enum
{
    APP_ACTION_NONE = 0,
    APP_ACTION_START,
    APP_ACTION_STOP,
    APP_ACTION_CLEAR,
    APP_ACTION_POWER_ON,
    APP_ACTION_OFF,
    APP_ACTION_CARD_ON,   /* clock mode / LED card: hold START, countdown, on */
    APP_ACTION_CARD_OFF   /* clock mode / LED card: hold STOP, countdown, off */
} AppAction;

#define LCD_HOME_TIMEOUT_MS                10000u
#define LCD_AWAKE_REFRESH_MS                 250u
#define LCD_IDLE_REFRESH_MS                  600u
#define APP_BACKGROUND_USB_REFRESH_MS       1000u
#define APP_BACKGROUND_BATTERY_REFRESH_MS   5000u
#define APP_MEASURE_PULSE_MS                 600u
#define APP_MEASURE_LED_BATTERY_PULSE_MS     250u
#define APP_RED_LED_PERIOD_MS               3000u
#define APP_RED_LED_FLASH_MS                 100u
#define APP_RED_LED_GAP_MS                   150u
#define APP_RED_LED_PATTERN_MS              (2u * APP_RED_LED_FLASH_MS + APP_RED_LED_GAP_MS)
#define APP_ACTION_STEP_MS                   400u
#define APP_LOADING_STEP_MS                  250u

static TempLogProState g_app_state = TEMPLOGPRO_STATE_READY;
static TempLogProAppMode g_app_mode = APP_MODE_LOGGER;
static uint32_t g_logging_interval_seconds = TEMPLOGPRO_DEFAULT_INTERVAL_SECONDS;
static TempLogProConfig g_app_config;
static uint32_t g_app_config_generation;
static LcdView g_lcd_view = LCD_VIEW_TEMPERATURE;
static uint32_t g_lcd_message_until_ms;
static char g_lcd_message[3];
static uint32_t g_lcd_message_icons;  /* extra icons for the message, blinking */
static int32_t g_last_temperature_x256;
static uint16_t g_last_flags;
static uint32_t g_have_last_measurement;
static uint32_t g_next_measurement_timestamp;
static uint32_t g_last_lcd_update_ms;
static uint32_t g_heart_shown;
static uint32_t g_red_led_start_ms;
static uint32_t g_last_measurement_ms;
static uint32_t g_icon_scan_index;
static uint32_t g_next_icon_scan_ms;
static TempLogProLogStats g_cached_stats;
static uint32_t g_stats_cache_valid;
static uint32_t g_next_stats_refresh_ms;
static uint32_t g_last_user_activity_ms;
static uint32_t g_lcd_awake;
static uint32_t g_off_armed;   /* OFF requested over USB, waits for unplug */
static uint32_t g_off_active;  /* OFF: LCD, LEDs and RTC wake-ups disabled */
static uint32_t g_lcd_hw_off;  /* LCD controller switched off (eco or OFF) */
static uint32_t g_last_usb_active;
static uint32_t g_last_background_ms;
static AppAction g_pending_action;
static TempLogProState g_action_return_state;
static LcdView g_action_return_view;
static uint32_t g_action_active;
static uint32_t g_action_next_step_ms;
static uint32_t g_action_digit;
/* RAM-only, USB-only segment probe; expires without altering logger state. */
static volatile uint32_t g_lcd_probe_until_ms;
static volatile uint32_t g_lcd_probe_index;

static void app_handle_button_event(TempLogProButtonEvent event, uint32_t now_ms);
static void app_cycle_lcd_view(uint32_t now_ms);
static void app_cycle_lcd_view_back(uint32_t now_ms);
static void app_lcd_message(const char text[3], uint32_t now_ms, uint32_t duration_ms);
static void app_reload_config_if_changed(void);
static void app_start_action(AppAction action, uint32_t now_ms);
static void app_process_action(uint32_t now_ms);
static void app_cancel_action(uint32_t now_ms);
static uint32_t app_action_text(char text[3], uint32_t *icon_mask);
static void app_execute_action(uint32_t now_ms);
static void app_show_loading(uint32_t now_ms);
static void app_rtc_wait_loading(void);
static void app_update_lcd(void);
static void app_update_leds(uint32_t now_ms);
static void app_process_rtc_events(void);
static void app_store_measurement(int32_t temp_x256, uint32_t valid);
static uint32_t app_next_measurement_deadline(uint32_t deadline, uint32_t now_ts);
static void app_reset_logger(uint32_t now_ms);
static void app_set_card_option(LcdView view, uint32_t enable, uint32_t now_ms);
static uint32_t app_is_option_card(LcdView view);
static void app_apply_lcd_timeout(uint32_t now_ms);
static void app_refresh_stats(uint32_t now_ms);
static void app_show_stats_temperature(int32_t value_x256, uint32_t valid);
static void app_show_temperature(int32_t celsius_x256);
static uint32_t app_degree_icon(void);
static void app_show_battery_percent(void);
static void app_show_runtime(void);
static void app_show_interval(uint32_t seconds);
static void app_show_clock(uint32_t steady_colon);
static uint32_t app_clock_mode_active(void);
static uint32_t app_eco_dark(void);
static void app_enter_off(uint32_t now_ms);
static void app_exit_off(uint32_t now_ms);
static void app_battery_warning(uint32_t now_ms, uint32_t duration_ms);
static uint32_t app_local_datetime(TempLogProDateTime *local);
static uint32_t app_local_hour(void);
static uint32_t app_base_icon_mask(uint32_t now_ms);
static uint32_t app_view_icon_mask(uint32_t now_ms);
static uint32_t app_runtime_unit_icon_mask(uint32_t seconds);
static uint16_t app_runtime_display_value(uint32_t seconds);
static uint32_t app_measure_pulse_active(uint32_t now_ms);
static uint32_t app_alarm_or_error_active(void);
static uint32_t app_red_led_on(uint32_t now_ms);
static uint32_t app_red_led_busy(uint32_t now_ms);
static uint16_t app_alarm_history_flags(void);
static uint32_t app_idle_icon_mask(uint32_t now_ms);
static uint32_t app_usb_active(void);
static uint32_t app_logger_stopped(void);
static const char *app_state_text(void);
static uint32_t app_runtime_seconds(void);
static uint32_t app_battery_percent_from_vdd(uint16_t vdd_mv);
static uint32_t flags_have_alarm(uint16_t flags);
static LcdView app_sane_lcd_view(uint32_t lcd_mode);
static uint32_t app_menu_sequence_index(LcdView view);

static const LcdView LCD_MENU_SEQUENCE[] = {
    LCD_VIEW_TEMPERATURE,
    LCD_VIEW_CLOCK,
    LCD_VIEW_LEDS,
    LCD_VIEW_ECO,
    LCD_VIEW_STATE,
    /* Average first, then each measured extreme next to its alarm threshold. */
    LCD_VIEW_TEMP_AVG,
    LCD_VIEW_TEMP_MAX,
    LCD_VIEW_TEMP_HIGH,
    LCD_VIEW_TEMP_MIN,
    LCD_VIEW_TEMP_LOW,
    LCD_VIEW_RUNTIME,
    LCD_VIEW_RECORDS,
    LCD_VIEW_INTERVAL,
    LCD_VIEW_BATTERY,
    LCD_VIEW_RESET
};

#define LCD_MENU_SEQUENCE_COUNT  ((uint32_t)(sizeof(LCD_MENU_SEQUENCE) / sizeof(LCD_MENU_SEQUENCE[0])))

uint32_t templogpro_app_init(TempLogProAppMode mode)
{
    TempLogProLoggerInfo info;
    uint32_t now_ts = 0u;

    g_app_mode = mode;
    g_app_state = TEMPLOGPRO_STATE_READY;
    g_logging_interval_seconds = TEMPLOGPRO_DEFAULT_INTERVAL_SECONDS;
    templogpro_config_defaults(&g_app_config);
    g_app_config_generation = 0u;
    g_lcd_view = LCD_VIEW_TEMPERATURE;
    g_lcd_message_until_ms = 0u;
    g_lcd_message[0] = ' ';
    g_lcd_message[1] = ' ';
    g_lcd_message[2] = ' ';
    g_last_temperature_x256 = 0;
    g_last_flags = TEMPLOGPRO_LOG_FLAG_VALID;
    g_have_last_measurement = 0u;
    g_next_measurement_timestamp = 0u;
    g_last_lcd_update_ms = 0u;
    g_last_measurement_ms = 0u;
    g_icon_scan_index = 0u;
    g_next_icon_scan_ms = 0u;
    memset(&g_cached_stats, 0, sizeof(g_cached_stats));
    g_stats_cache_valid = 0u;
    g_next_stats_refresh_ms = 0u;
    g_last_user_activity_ms = 0u;
    g_lcd_awake = 1u;
    g_last_usb_active = 0u;
    g_last_background_ms = 0u;
    g_pending_action = APP_ACTION_NONE;
    g_action_return_state = TEMPLOGPRO_STATE_READY;
    g_action_return_view = LCD_VIEW_TEMPERATURE;
    g_action_active = 0u;
    g_action_next_step_ms = 0u;
    g_action_digit = 0u;

    templogpro_diag_init();
    templogpro_hw_init_leds();
    templogpro_hw_led_green_off();
    templogpro_hw_led_red_off();
    templogpro_diag_boot_stage(TEMPLOGPRO_BOOT_STAGE_GPIO);

    templogpro_hw_lcd_init();
    templogpro_diag_boot_stage(TEMPLOGPRO_BOOT_STAGE_LCD);
    app_show_loading(HAL_GetTick());
    templogpro_rtc_set_wait_hook(app_rtc_wait_loading);
    templogpro_rtc_set_clock_switch_hook(templogpro_hw_lcd_suspend);
    if (templogpro_rtc_init() != TEMPLOGPRO_RTC_STATUS_OK) {
        g_app_state = TEMPLOGPRO_STATE_ERROR;
    }
    templogpro_rtc_set_wait_hook(0);
    templogpro_rtc_set_clock_switch_hook(0);
    templogpro_diag_boot_stage(TEMPLOGPRO_BOOT_STAGE_RTC);
    templogpro_hw_lcd_init();
    app_show_loading(HAL_GetTick());

    templogpro_diag_boot_stage(TEMPLOGPRO_BOOT_STAGE_STORAGE);
    (void)templogpro_config_init();
    app_show_loading(HAL_GetTick());
    templogpro_config_get(&g_app_config);
    g_app_config_generation = templogpro_config_generation();
    g_logging_interval_seconds = g_app_config.interval_seconds;
    g_lcd_view = app_sane_lcd_view(g_app_config.lcd_mode);

    templogpro_buttons_init();
    app_show_loading(HAL_GetTick());

    if (templogpro_sensor_init() == 0u) {
        g_app_state = TEMPLOGPRO_STATE_ERROR;
    }
    app_show_loading(HAL_GetTick());
    if (templogpro_logger_init() == 0u) {
        g_app_state = TEMPLOGPRO_STATE_ERROR;
    }
    app_process_rtc_events();
    templogpro_diag_boot_stage(TEMPLOGPRO_BOOT_STAGE_LOGGER);

    if (templogpro_logger_get_info(&info) != 0u) {
        if (info.logger_state == TEMPLOGPRO_LOGGER_LOGGING) {
            g_app_state = TEMPLOGPRO_STATE_RUN;
            if (info.logging_interval_seconds != g_logging_interval_seconds) {
                (void)templogpro_logger_set_interval(g_logging_interval_seconds);
                info.logging_interval_seconds = g_logging_interval_seconds;
            }
            g_logging_interval_seconds = info.logging_interval_seconds;
            if (templogpro_rtc_get_timestamp(&now_ts) != 0u) {
                g_next_measurement_timestamp = now_ts + g_logging_interval_seconds;
            }
        } else if (info.logger_state == TEMPLOGPRO_LOGGER_STOPPED) {
            g_app_state = TEMPLOGPRO_STATE_PAUSE;
        } else if (info.logger_state == TEMPLOGPRO_LOGGER_ERROR) {
            g_app_state = TEMPLOGPRO_STATE_ERROR;
        } else if (g_app_state != TEMPLOGPRO_STATE_ERROR) {
            g_app_state = TEMPLOGPRO_STATE_READY;
        }
    }

    g_last_user_activity_ms = HAL_GetTick();
    if (templogpro_diag_interrupted_op() != TEMPLOGPRO_FLASH_OP_NONE) {
        /* The battery gave out during CLR/START: say so for 5 s after boot. */
        app_battery_warning(g_last_user_activity_ms, 5000u);
    } else {
        app_lcd_message(app_state_text(), g_last_user_activity_ms, 900u);
    }
    app_update_lcd();
    app_update_leds(HAL_GetTick());
    return (g_app_state == TEMPLOGPRO_STATE_ERROR) ? 0u : 1u;
}

void templogpro_app_process(void)
{
    uint32_t now_ms = HAL_GetTick();
    uint32_t now_ts;
    uint32_t button_mask;
    uint32_t usb_active;
    uint32_t background_refresh_ms;
    uint32_t lcd_refresh_ms;
    TempLogProButtonEvent button_event;
    int32_t temperature_x256;
    uint32_t sensor_result;

    templogpro_app_apply_config();
    templogpro_rtc_process();
    templogpro_logger_process();
    app_process_rtc_events();

    if (g_app_mode == APP_MODE_LOGGER) {
        button_event = templogpro_buttons_poll(now_ms);
        button_mask = templogpro_buttons_current_mask();

        if (button_mask != 0u) {
            g_last_user_activity_ms = now_ms;
        }

        if (g_off_active != 0u) {
            /* OFF: only both buttons held start the 3-2-1 power-on countdown. */
            if ((button_mask == 3u) && (g_action_active == 0u)) {
                templogpro_hw_lcd_init();
                g_lcd_hw_off = 0u;
                app_start_action(APP_ACTION_POWER_ON, now_ms);
            }
        } else if (button_event != BUTTON_NONE) {
            app_handle_button_event(button_event, now_ms);
        }
    }

    app_process_action(now_ms);

    {
        TempLogProDiagnostics off_diag;

        templogpro_diag_get(&off_diag);
        if ((g_off_active != 0u) && (off_diag.usb_vbus_present != 0u)) {
            app_exit_off(now_ms);
        } else if ((g_off_armed != 0u) && (off_diag.usb_vbus_present == 0u) && (g_action_active == 0u) &&
                   ((g_lcd_message_until_ms == 0u) || ((int32_t)(now_ms - g_lcd_message_until_ms) >= 0))) {
            app_enter_off(now_ms);
        }
    }

    if ((g_app_state == TEMPLOGPRO_STATE_RUN) &&
        (templogpro_app_usb_report_mode() == 0u) &&
        (templogpro_rtc_get_timestamp(&now_ts) != 0u)) {
        sensor_result = templogpro_sensor_poll(&temperature_x256);
        if (sensor_result != TEMPLOGPRO_SENSOR_WAIT) {
            app_store_measurement(sensor_result == TEMPLOGPRO_SENSOR_READY ? temperature_x256 : 0,
                                  sensor_result == TEMPLOGPRO_SENSOR_READY);
        }
        if ((g_app_state == TEMPLOGPRO_STATE_RUN) && !templogpro_sensor_pending() &&
            ((g_next_measurement_timestamp == 0u) ||
             ((int32_t)(now_ts - g_next_measurement_timestamp) >= 0))) {
            if (!templogpro_sensor_request()) app_store_measurement(0, 0u);
            g_next_measurement_timestamp =
                app_next_measurement_deadline(g_next_measurement_timestamp, now_ts);
        }
    } else {
        templogpro_sensor_cancel();
    }

    usb_active = app_usb_active();
    background_refresh_ms = (usb_active != 0u) ?
                            APP_BACKGROUND_USB_REFRESH_MS :
                            APP_BACKGROUND_BATTERY_REFRESH_MS;
    if ((now_ms - g_last_background_ms) >= background_refresh_ms) {
        g_last_background_ms = now_ms;
        app_reload_config_if_changed();
        app_refresh_stats(now_ms);
    }

    lcd_refresh_ms = ((g_lcd_awake != 0u) || (usb_active != 0u)) ?
                     LCD_AWAKE_REFRESH_MS : LCD_IDLE_REFRESH_MS;
    /* Redraw as soon as the heartbeat ends so the heart goes off with the green LED. */
    if (((now_ms - g_last_lcd_update_ms) >= lcd_refresh_ms) ||
        (g_heart_shown != app_measure_pulse_active(now_ms))) {
        g_heart_shown = app_measure_pulse_active(now_ms);
        g_last_lcd_update_ms = now_ms;
        app_apply_lcd_timeout(now_ms);
        app_update_lcd();
    }
    app_update_leds(now_ms);
}

uint32_t templogpro_app_start_logging(void)
{
    uint32_t now_ts;
    uint32_t now_ms = HAL_GetTick();

    app_cancel_action(now_ms);

    if (templogpro_app_usb_report_mode()) {
        app_lcd_message("USB", now_ms, 900u);
        app_update_lcd();
        return 0u;
    }

    if (g_app_state == TEMPLOGPRO_STATE_RUN) {
        g_lcd_view = LCD_VIEW_STATE;
        app_lcd_message("run", now_ms, 900u);
        app_update_lcd();
        return 1u;
    }
    if ((g_app_state != TEMPLOGPRO_STATE_READY) && (g_app_state != TEMPLOGPRO_STATE_PAUSE)) {
        return 0u;
    }

    templogpro_config_get(&g_app_config);
    g_logging_interval_seconds = g_app_config.interval_seconds;
    if (((g_app_state == TEMPLOGPRO_STATE_PAUSE) &&
         (templogpro_logger_resume(g_logging_interval_seconds) == 0u)) ||
        ((g_app_state == TEMPLOGPRO_STATE_READY) &&
         (templogpro_logger_start(g_logging_interval_seconds) == 0u))) {
        g_app_state = TEMPLOGPRO_STATE_ERROR;
        return 0u;
    }

    g_stats_cache_valid = 0u;
    g_app_state = TEMPLOGPRO_STATE_RUN;
    if (templogpro_rtc_get_timestamp(&now_ts) != 0u) {
        g_next_measurement_timestamp = now_ts;
    } else {
        g_next_measurement_timestamp = 0u;
    }
    g_lcd_view = LCD_VIEW_STATE;
    app_lcd_message("run", now_ms, 900u);
    app_update_lcd();
    return 1u;
}

uint32_t templogpro_app_stop_logging(void)
{
    uint32_t now_ms = HAL_GetTick();
    app_cancel_action(now_ms);
    templogpro_sensor_cancel();

    if (g_app_state != TEMPLOGPRO_STATE_RUN) {
        g_lcd_view = LCD_VIEW_STATE;
        app_lcd_message(app_state_text(), now_ms, 900u);
        app_update_lcd();
        return 1u;
    }

    if (templogpro_logger_stop() == 0u) {
        g_app_state = TEMPLOGPRO_STATE_ERROR;
        return 0u;
    }
    g_stats_cache_valid = 0u;
    g_app_state = TEMPLOGPRO_STATE_PAUSE;
    g_lcd_view = LCD_VIEW_STATE;
    app_lcd_message("StP", now_ms, 900u);
    app_update_lcd();
    return 1u;
}

uint32_t templogpro_app_clear_logger(void)
{
    uint32_t now_ms = HAL_GetTick();
    app_cancel_action(now_ms);

    if (g_app_state == TEMPLOGPRO_STATE_RUN) {
        if (templogpro_app_stop_logging() == 0u) {
            return 0u;
        }
    }

    if ((g_app_state != TEMPLOGPRO_STATE_READY) &&
        (g_app_state != TEMPLOGPRO_STATE_PAUSE)) {
        return 0u;
    }

    app_reset_logger(now_ms);
    return (g_app_state == TEMPLOGPRO_STATE_ERROR) ? 0u : 1u;
}

TempLogProState templogpro_app_get_state(void)
{
    return g_app_state;
}

uint32_t templogpro_app_should_idle_sleep(void)
{
#if (TEMPLOGPRO_IDLE_WFI_ENABLE == 0)
    return 0u;
#else
    TempLogProDiagnostics diag;

    if (templogpro_logger_flash_is_busy() != 0u) {
        return 0u;
    }

    templogpro_diag_get(&diag);
    if (diag.usb_vbus_present != 0u) {
        return 1u;
    }

    if ((g_app_config.battery_mode_enable == 0u) ||
        (g_app_state == TEMPLOGPRO_STATE_ERROR)) {
        return 0u;
    }
    return 1u;
#endif
}

uint32_t templogpro_app_should_stop_sleep(void)
{
    TempLogProDiagnostics diag;
    uint32_t now_ms = HAL_GetTick();

    if (g_off_active != 0u) {
        templogpro_diag_get(&diag);
        return ((g_action_active == 0u) && (templogpro_buttons_current_mask() == 0u) &&
                (diag.usb_vbus_present == 0u) && (templogpro_logger_flash_is_busy() == 0u)) ? 1u : 0u;
    }
    if (templogpro_app_should_idle_sleep() == 0u) {
        return 0u;
    }
    if ((g_lcd_awake != 0u) || (g_action_active != 0u) || templogpro_sensor_pending()) {
        return 0u;
    }
    if (templogpro_buttons_current_mask() != 0u) {
        return 0u;
    }
    if (templogpro_logger_stats_ready() == 0u) {
        return 0u;
    }

    templogpro_diag_get(&diag);
    if (diag.usb_vbus_present != 0u) {
        return 0u;
    }

    if (app_eco_dark() != 0u) {
        return 1u;  /* eco: no heartbeat, no LEDs - nothing to stay awake for */
    }
    if (app_measure_pulse_active(now_ms) != 0u) {
        return 0u;
    }
    if ((g_app_config.led_enable != 0u) &&
        (app_alarm_or_error_active() != 0u) &&
        (app_red_led_busy(now_ms) != 0u)) {
        return 0u;
    }
    return 1u;
}

/* RTC wake period for STOP. Measurements need a 1 s tick; a stopped logger only
   has to show the alarm LED (every 3 s) or nothing at all: buttons and USB cable
   insertion wake the MCU by interrupt, so a 60 s tick adds no visible lag. */
uint32_t templogpro_app_stop_wake_seconds(void)
{
    TempLogProDateTime now;
    uint32_t wake_s = 60u;

    if (g_off_active != 0u) {
        return 0u;
    }
    if (app_eco_dark() != 0u) {
        /* Eco: logging wakes only for the next measurement, a stopped logger once a minute. */
        uint32_t now_ts;

        if ((g_app_state == TEMPLOGPRO_STATE_RUN) && (templogpro_rtc_get_timestamp(&now_ts) != 0u)) {
            if ((g_next_measurement_timestamp == 0u) || ((int32_t)(g_next_measurement_timestamp - now_ts) <= 1)) {
                return 1u;
            }
            wake_s = g_next_measurement_timestamp - now_ts;
            return (wake_s > 60u) ? 60u : wake_s;
        }
        return ((g_app_state == TEMPLOGPRO_STATE_READY) || (g_app_state == TEMPLOGPRO_STATE_PAUSE)) ? 60u : 1u;
    }
    if ((g_app_state != TEMPLOGPRO_STATE_READY) && (g_app_state != TEMPLOGPRO_STATE_PAUSE)) {
        return 1u;
    }
    /* Clock mode: wake on the next full minute to redraw H:MM (steady colon). */
    if ((app_clock_mode_active() != 0u) && (templogpro_rtc_get(&now) != 0u) && (now.second < 60u)) {
        wake_s = 60u - now.second;
    }
    if ((g_app_config.led_enable != 0u) && (app_alarm_or_error_active() != 0u) && (wake_s > 3u)) {
        wake_s = 3u;
    }
    return wake_s;
}

/* Eco on battery with the display asleep: LCD controller off, no heart, no LEDs. */
static uint32_t app_eco_dark(void)
{
    return ((g_app_config.eco_mode != 0u) && (g_lcd_awake == 0u) && (g_off_active == 0u) &&
            (g_action_active == 0u) && (app_usb_active() == 0u)) ? 1u : 0u;
}

static uint32_t app_clock_mode_active(void)
{
    return ((g_app_config.clock_mode != 0u) && (g_app_config.eco_mode == 0u) && (g_off_active == 0u) &&
            ((g_app_state == TEMPLOGPRO_STATE_READY) || (g_app_state == TEMPLOGPRO_STATE_PAUSE) ||
             (g_app_state == TEMPLOGPRO_STATE_RUN)) &&
            (app_usb_active() == 0u)) ? 1u : 0u;
}

uint32_t templogpro_app_power_off_arm(void)
{
    if ((g_app_state == TEMPLOGPRO_STATE_RUN) && (templogpro_app_stop_logging() == 0u)) {
        return 0u;
    }
    if ((g_app_state != TEMPLOGPRO_STATE_READY) && (g_app_state != TEMPLOGPRO_STATE_PAUSE)) {
        return 0u;
    }
    g_off_armed = 1u;
    return 1u;
}

void templogpro_app_power_off_cancel(void)
{
    g_off_armed = 0u;
}

uint32_t templogpro_app_power_off_armed(void)
{
    return g_off_armed;
}

static void app_enter_off(uint32_t now_ms)
{
    g_off_armed = 0u;
    if (g_app_state == TEMPLOGPRO_STATE_RUN) {
        (void)templogpro_app_stop_logging();  /* started again after arming */
    }
    app_cancel_action(now_ms);
    templogpro_sensor_cancel();
    g_off_active = 1u;
    g_lcd_awake = 0u;
    g_lcd_message_until_ms = 0u;
    templogpro_hw_led_green_off();
    templogpro_hw_led_red_off();
    templogpro_hw_lcd_set_icon_mask(0u);
    templogpro_hw_lcd_show_text3("   ");
    templogpro_hw_lcd_suspend();
    g_lcd_hw_off = 1u;
}

static void app_exit_off(uint32_t now_ms)
{
    g_off_active = 0u;
    templogpro_hw_lcd_init();
    g_lcd_hw_off = 0u;
    g_lcd_awake = 1u;
    g_lcd_view = LCD_VIEW_TEMPERATURE;
    g_last_user_activity_ms = now_ms;
    app_lcd_message(app_state_text(), now_ms, 900u);
    app_update_lcd();
}

static void app_handle_button_event(TempLogProButtonEvent event, uint32_t now_ms)
{
    if (event == BUTTON_NONE) {
        return;
    }
    if (g_action_active != 0u) {
        return;
    }

    if ((g_lcd_awake == 0u) &&
        ((event == BUTTON_START_SHORT) || (event == BUTTON_STOP_SHORT))) {
        g_lcd_awake = 1u;
        g_last_user_activity_ms = now_ms;
        g_lcd_message_until_ms = 0u;
        app_update_lcd();
        return;
    }

    if (g_lcd_awake == 0u) {
        g_lcd_awake = 1u;
        g_lcd_message_until_ms = 0u;
    }

    g_last_user_activity_ms = now_ms;

    switch (event) {
    case BUTTON_START_SHORT:
        app_cycle_lcd_view_back(now_ms);
        break;

    case BUTTON_STOP_SHORT:
        app_cycle_lcd_view(now_ms);
        break;

    case BUTTON_START_LONG_CONFIRMED:
        if (app_is_option_card(g_lcd_view) != 0u) {
            app_start_action(APP_ACTION_CARD_ON, now_ms);  /* START = on, never START logging */
        } else if ((g_app_state == TEMPLOGPRO_STATE_READY) || (g_app_state == TEMPLOGPRO_STATE_PAUSE)) {
            app_start_action(APP_ACTION_START, now_ms);
        }
        break;

    case BUTTON_STOP_LONG_CONFIRMED:
        if (app_is_option_card(g_lcd_view) != 0u) {
            app_start_action(APP_ACTION_CARD_OFF, now_ms);  /* STOP = off, never STOP logging */
        } else if (g_app_state == TEMPLOGPRO_STATE_RUN) {
            app_start_action(APP_ACTION_STOP, now_ms);
        }
        break;

    case BUTTON_BOTH_LONG_CONFIRMED:
        if (g_lcd_view == LCD_VIEW_RESET) {
            app_start_action(APP_ACTION_CLEAR, now_ms);
        } else if (g_lcd_view == LCD_VIEW_BATTERY) {  /* power card: both buttons = OFF */
            app_start_action(APP_ACTION_OFF, now_ms);
        }
        break;

    case BUTTON_PRESS_CANCELLED:
    default:
        break;
    }
}

static void app_cycle_lcd_view(uint32_t now_ms)
{
    uint32_t index = app_menu_sequence_index(g_lcd_view);

    for (uint32_t step = 0u; step < LCD_MENU_SEQUENCE_COUNT; step++) {
        index = (index + 1u) % LCD_MENU_SEQUENCE_COUNT;
        if ((g_app_config.lcd_menu_mask & (1u << LCD_MENU_SEQUENCE[index])) != 0u) {
            break;
        }
    }
    g_lcd_view = LCD_MENU_SEQUENCE[index];
    g_lcd_message_until_ms = 0u;
    app_refresh_stats(now_ms);
    app_update_lcd();
}

static void app_cycle_lcd_view_back(uint32_t now_ms)
{
    uint32_t index = app_menu_sequence_index(g_lcd_view);

    for (uint32_t step = 0u; step < LCD_MENU_SEQUENCE_COUNT; step++) {
        index = (index + LCD_MENU_SEQUENCE_COUNT - 1u) % LCD_MENU_SEQUENCE_COUNT;
        if ((g_app_config.lcd_menu_mask & (1u << LCD_MENU_SEQUENCE[index])) != 0u) {
            break;
        }
    }
    g_lcd_view = LCD_MENU_SEQUENCE[index];
    g_lcd_message_until_ms = 0u;
    app_refresh_stats(now_ms);
    app_update_lcd();
}

static void app_lcd_message(const char text[3], uint32_t now_ms, uint32_t duration_ms)
{
    g_lcd_message_icons = 0u;
    g_lcd_message[0] = text[0];
    g_lcd_message[1] = text[1];
    g_lcd_message[2] = text[2];
    g_lcd_message_until_ms = now_ms + duration_ms;
}

static void app_start_action(AppAction action, uint32_t now_ms)
{
    if ((action == APP_ACTION_NONE) || (g_action_active != 0u)) {
        return;
    }

    g_pending_action = action;
    g_action_return_state = g_app_state;
    g_action_return_view = g_lcd_view;
    g_action_active = 1u;
    g_action_digit = (action == APP_ACTION_POWER_ON) ? 3u : 5u;
    g_action_next_step_ms = now_ms + ((action == APP_ACTION_POWER_ON) ? 1000u : APP_ACTION_STEP_MS);
    g_lcd_awake = 1u;
    g_lcd_message_until_ms = 0u;

    if (action == APP_ACTION_START) {
        g_app_state = TEMPLOGPRO_STATE_STARTING;
        g_lcd_view = LCD_VIEW_STATE;
    } else if (action == APP_ACTION_STOP) {
        g_app_state = TEMPLOGPRO_STATE_STOPPING;
        g_lcd_view = LCD_VIEW_STATE;
    } else if (action == APP_ACTION_CLEAR) {
        g_app_state = TEMPLOGPRO_STATE_CLEARING;
        g_lcd_view = LCD_VIEW_RESET;
    }

    app_update_lcd();
}

static void app_process_action(uint32_t now_ms)
{
    uint32_t required_mask;

    if (g_action_active == 0u) {
        return;
    }

    required_mask = ((g_pending_action == APP_ACTION_START) || (g_pending_action == APP_ACTION_CARD_ON)) ? 1u :
                    ((g_pending_action == APP_ACTION_STOP) || (g_pending_action == APP_ACTION_CARD_OFF)) ? 2u : 3u;
    if ((templogpro_buttons_current_mask() != required_mask) ||
        (g_app_state == TEMPLOGPRO_STATE_ERROR)) {
        app_cancel_action(now_ms);
        return;
    }

    if ((int32_t)(now_ms - g_action_next_step_ms) < 0) {
        return;
    }

    if (g_action_digit > ((g_pending_action == APP_ACTION_POWER_ON) ? 1u : 0u)) {
        g_action_digit--;
        g_action_next_step_ms = now_ms + ((g_pending_action == APP_ACTION_POWER_ON) ? 1000u : APP_ACTION_STEP_MS);
        app_update_lcd();
        return;
    }

    app_execute_action(now_ms);
}

static void app_cancel_action(uint32_t now_ms)
{
    if (g_action_active == 0u) {
        return;
    }
    g_action_active = 0u;
    g_pending_action = APP_ACTION_NONE;
    g_action_next_step_ms = 0u;
    g_action_digit = 0u;
    if (g_app_state != TEMPLOGPRO_STATE_ERROR) {
        g_app_state = g_action_return_state;
    }
    /* Stay on the card the action started from (CLR, OFF, clock mode, LEDs...). */
    g_lcd_view = (g_action_return_view <= LCD_VIEW_ECO) ? g_action_return_view : LCD_VIEW_TEMPERATURE;
    g_lcd_message_until_ms = 0u;
    g_last_user_activity_ms = now_ms;
    if (g_off_active != 0u) {
        /* Released too early: back to OFF. */
        g_lcd_awake = 0u;
        templogpro_hw_lcd_set_icon_mask(0u);
        templogpro_hw_lcd_show_text3("   ");
        templogpro_hw_lcd_suspend();
        g_lcd_hw_off = 1u;
        return;
    }
    app_update_lcd();
}

static uint32_t app_action_text(char text[3], uint32_t *icon_mask)
{
    if ((text == 0) || (icon_mask == 0) || (g_action_active == 0u)) {
        return 0u;
    }

    text[2] = (char)('0' + (g_action_digit % 10u));
    *icon_mask = 0u;

    if (g_pending_action == APP_ACTION_START) {
        text[0] = 'r';
        text[1] = ' ';
        return 1u;
    }
    if (g_pending_action == APP_ACTION_STOP) {
        text[0] = 'S';
        text[1] = ' ';
        *icon_mask = TEMPLOGPRO_LCD_ICON_STOP;
        return 1u;
    }
    if (g_pending_action == APP_ACTION_CLEAR) {
        text[0] = 'C';
        text[1] = ' ';
        return 1u;
    }
    if (g_pending_action == APP_ACTION_POWER_ON) {
        text[0] = ' ';
        text[1] = ' ';
        return 1u;
    }
    if (g_pending_action == APP_ACTION_OFF) {
        text[0] = 'o';
        text[1] = ' ';
        return 1u;
    }
    if (g_pending_action == APP_ACTION_CARD_ON) {
        text[0] = 'O';  /* "On5" ... "On0" */
        text[1] = 'n';
        *icon_mask = app_view_icon_mask(HAL_GetTick());
        return 1u;
    }
    if (g_pending_action == APP_ACTION_CARD_OFF) {
        text[0] = 'O';  /* "OF5" ... "OF0" */
        text[1] = 'F';
        *icon_mask = app_view_icon_mask(HAL_GetTick());
        return 1u;
    }

    return 0u;
}

/* Erasing a Winbond sector draws ~15-25 mA; a weak CR3450 then sags below the
   reset threshold and the MCU restarts half-way. On battery, refuse CLR/START
   below 2.7 V (also the W25X40 minimum) and blink the battery icon with X. */
#define APP_FLASH_MIN_VDD_MV  2700u

static uint32_t app_flash_power_ok(void)
{
    TempLogProDiagnostics diag;
    uint16_t vdd_mv = 0u;

    templogpro_diag_get(&diag);
    if (diag.usb_vbus_present != 0u) {
        return 1u;
    }
    if (templogpro_power_read_vdd_mv(&vdd_mv) == 0u) {
        return 1u;  /* no reading: do not block the user */
    }
    return (vdd_mv >= APP_FLASH_MIN_VDD_MV) ? 1u : 0u;
}

static void app_battery_warning(uint32_t now_ms, uint32_t duration_ms)
{
    app_lcd_message("bAt", now_ms, duration_ms);
    g_lcd_message_icons = TEMPLOGPRO_LCD_ICON_BATTERY | TEMPLOGPRO_LCD_ICON_X;
}

static void app_execute_action(uint32_t now_ms)
{
    AppAction action = g_pending_action;

    g_action_active = 0u;
    g_pending_action = APP_ACTION_NONE;
    g_action_next_step_ms = 0u;
    g_action_digit = 0u;

    if ((action == APP_ACTION_CARD_ON) || (action == APP_ACTION_CARD_OFF)) {
        g_app_state = g_action_return_state;
        app_set_card_option(g_action_return_view, (action == APP_ACTION_CARD_ON) ? 1u : 0u, now_ms);
        return;
    }

    if (action == APP_ACTION_OFF) {
        /* Same as the panel's POWER OFF: on battery the device goes dark after
           the "OFF" message, on USB it waits for the cable to be unplugged. */
        g_app_state = g_action_return_state;
        (void)templogpro_app_power_off_arm();
        app_lcd_message("OFF", now_ms, 1200u);
        app_update_lcd();
        return;
    }

    if (action == APP_ACTION_POWER_ON) {
        g_app_state = g_action_return_state;
        g_off_active = 0u;
        g_lcd_awake = 1u;
        g_lcd_view = LCD_VIEW_TEMPERATURE;
        g_last_user_activity_ms = now_ms;
        app_lcd_message(app_state_text(), now_ms, 1500u);
        app_update_lcd();
        return;
    }

    if (((action == APP_ACTION_START) || (action == APP_ACTION_CLEAR)) && (app_flash_power_ok() == 0u)) {
        g_app_state = g_action_return_state;
        app_battery_warning(now_ms, 3000u);
        return;
    }

    if (action == APP_ACTION_START) {
        g_app_state = g_action_return_state;
        templogpro_diag_flash_op_begin(TEMPLOGPRO_FLASH_OP_START);
        (void)templogpro_app_start_logging();
        templogpro_diag_flash_op_end();
        return;
    }

    if (action == APP_ACTION_STOP) {
        g_app_state = g_action_return_state;
        (void)templogpro_app_stop_logging();
        return;
    }

    if (action == APP_ACTION_CLEAR) {
        if (g_action_return_state == TEMPLOGPRO_STATE_RUN) {
            g_app_state = TEMPLOGPRO_STATE_RUN;
            if (templogpro_app_stop_logging() == 0u) {
                return;
            }
        } else {
            g_app_state = g_action_return_state;
        }
        templogpro_diag_flash_op_begin(TEMPLOGPRO_FLASH_OP_CLEAR);
        app_reset_logger(now_ms);
        templogpro_diag_flash_op_end();
        if (g_app_state != TEMPLOGPRO_STATE_ERROR) {
            app_lcd_message("don", now_ms, 900u);
        }
    }
}

uint32_t templogpro_app_usb_report_mode(void)
{
    TempLogProDiagnostics diag;
    TempLogProConfig config;
    templogpro_diag_get(&diag);
    templogpro_config_get(&config);
    return diag.usb_vbus_present && !config.usb_logging_enable;
}

void templogpro_app_apply_config(void)
{
    app_reload_config_if_changed();
    if (templogpro_app_usb_report_mode()) {
        /* Keep STOP in the log so reports never count USB service time as
           measurement time. Restart requires an explicit START command. */
        if (g_action_active) {
            app_cancel_action(HAL_GetTick());
        }
        if (g_app_state == TEMPLOGPRO_STATE_RUN) (void)templogpro_app_stop_logging();
        templogpro_sensor_cancel();
    }
}

static void app_reload_config_if_changed(void)
{
    TempLogProConfig next_config;
    uint32_t generation = templogpro_config_generation();
    uint32_t now_ts;

    if (generation == g_app_config_generation) {
        return;
    }

    templogpro_config_get(&next_config);
    g_app_config = next_config;
    g_app_config_generation = generation;
    g_lcd_view = app_sane_lcd_view(g_app_config.lcd_mode);
    if (g_lcd_view == LCD_VIEW_ICON_SCAN) {
        g_icon_scan_index = 0u;
        g_next_icon_scan_ms = HAL_GetTick() + 900u;
    }

    if (g_logging_interval_seconds != g_app_config.interval_seconds) {
        g_logging_interval_seconds = g_app_config.interval_seconds;
        if (g_app_state == TEMPLOGPRO_STATE_RUN) {
            (void)templogpro_logger_set_interval(g_logging_interval_seconds);
            if (templogpro_rtc_get_timestamp(&now_ts) != 0u) {
                g_next_measurement_timestamp = now_ts + g_logging_interval_seconds;
            }
        }
    }
}

static void app_show_loading(uint32_t now_ms)
{
    char loading_text[3] = {' ', ' ', ' '};
    uint32_t loading_step = (now_ms / APP_LOADING_STEP_MS) % 4u;

    if (loading_step >= 1u) {
        loading_text[0] = '-';
    }
    if (loading_step >= 2u) {
        loading_text[1] = '-';
    }
    if (loading_step >= 3u) {
        loading_text[2] = '-';
    }
    templogpro_hw_lcd_set_icon_mask(TEMPLOGPRO_LCD_ICON_HOURGLASS);
    templogpro_hw_lcd_show_text3(loading_text);
}

static void app_rtc_wait_loading(void)
{
    app_show_loading(HAL_GetTick());
}

static void app_update_lcd(void)
{
    TempLogProLoggerInfo info;
    uint32_t now_ms = HAL_GetTick();
    uint32_t value = 0u;
    uint32_t icon_mask;
    char action_text[3];
    uint32_t action_icon_mask;

    if ((g_off_active != 0u) && (g_action_active == 0u)) {
        return;  /* LCD peripheral is off */
    }

    if (app_eco_dark() != 0u) {
        if (g_lcd_hw_off == 0u) {
            templogpro_hw_lcd_set_icon_mask(0u);
            templogpro_hw_lcd_show_text3("   ");
            templogpro_hw_lcd_suspend();
            g_lcd_hw_off = 1u;
        }
        return;
    }
    if (g_lcd_hw_off != 0u) {
        templogpro_hw_lcd_init();  /* a button (or USB) woke the display */
        g_lcd_hw_off = 0u;
    }

    if ((g_lcd_awake == 0u) && (app_clock_mode_active() != 0u)) {
        /* A plain clock face: TIME, the afternoon sun and, while logging, the
           heart that blinks with the green LED at each measurement. */
        templogpro_hw_lcd_set_icon_mask(TEMPLOGPRO_LCD_ICON_TIME |
                                       ((app_local_hour() >= 12u) ? TEMPLOGPRO_LCD_ICON_SUN : 0u) |
                                       ((app_measure_pulse_active(now_ms) != 0u) ? TEMPLOGPRO_LCD_ICON_HEART : 0u));
        app_show_clock(1u);
        return;
    }

    if (g_lcd_awake == 0u) {
        templogpro_hw_lcd_set_icon_mask(app_idle_icon_mask(now_ms));
        templogpro_hw_lcd_show_text3("   ");
        return;
    }

    if (templogpro_logger_stats_ready() == 0u) {
        app_show_loading(now_ms);
        return;
    }

    if ((g_lcd_probe_until_ms != 0u) &&
        ((int32_t)(now_ms - g_lcd_probe_until_ms) < 0) &&
        (app_usb_active() != 0u) &&
        (g_lcd_probe_index < templogpro_hw_lcd_icon_scan_count())) {
        templogpro_hw_lcd_show_icon_scan(g_lcd_probe_index);
        return;
    }

    if (app_action_text(action_text, &action_icon_mask) != 0u) {
        templogpro_hw_lcd_set_icon_mask(app_base_icon_mask(now_ms) | action_icon_mask);
        templogpro_hw_lcd_show_text3(action_text);
        return;
    }

    if ((g_lcd_message_until_ms != 0u) &&
        ((int32_t)(now_ms - g_lcd_message_until_ms) < 0)) {
        /* A message with its own icons (battery warning, clock mode change) shows only
           those, blinking - no check mark or alarm icons next to them. */
        templogpro_hw_lcd_set_icon_mask((g_lcd_message_icons != 0u) ?
                                       ((((now_ms / 400u) & 1u) == 0u) ? g_lcd_message_icons : 0u) :
                                       app_base_icon_mask(now_ms));
        templogpro_hw_lcd_show_text3(g_lcd_message);
        return;
    }

    if ((app_usb_active() != 0u) && (g_lcd_view == LCD_VIEW_TEMPERATURE)) {
        templogpro_hw_lcd_set_icon_mask(TEMPLOGPRO_LCD_ICON_BIG_ARROW |
                                      ((app_measure_pulse_active(now_ms) != 0u) ? TEMPLOGPRO_LCD_ICON_HEART : 0u));
        templogpro_hw_lcd_show_text3("USB");
        return;
    }

    if (g_app_state == TEMPLOGPRO_STATE_ERROR) {
        templogpro_hw_lcd_set_icon_mask(app_base_icon_mask(now_ms));
        templogpro_hw_lcd_show_text3("Err");
        return;
    }

    if ((g_lcd_view == LCD_VIEW_BATTERY) && (app_usb_active() != 0u)) {
        /* VDD on USB is not a battery charge measurement. */
        templogpro_hw_lcd_set_icon_mask(app_base_icon_mask(now_ms) | TEMPLOGPRO_LCD_ICON_BIG_ARROW);
        templogpro_hw_lcd_show_text3("USB");
        return;
    }

    icon_mask = app_base_icon_mask(now_ms) | app_view_icon_mask(now_ms);
    templogpro_hw_lcd_set_icon_mask(icon_mask);

    if (g_lcd_view == LCD_VIEW_TEMPERATURE) {
        if (g_have_last_measurement != 0u) {
            app_show_temperature(g_last_temperature_x256);
        } else {
            templogpro_hw_lcd_show_text3("---");
        }
        return;
    }

    if (g_lcd_view == LCD_VIEW_TEMP_MIN) {
        app_show_stats_temperature(g_cached_stats.min_temperature_x256, g_cached_stats.sample_count != 0u);
        return;
    }

    if (g_lcd_view == LCD_VIEW_TEMP_MAX) {
        app_show_stats_temperature(g_cached_stats.max_temperature_x256, g_cached_stats.sample_count != 0u);
        return;
    }

    if (g_lcd_view == LCD_VIEW_TEMP_AVG) {
        app_show_stats_temperature(g_cached_stats.avg_temperature_x256, g_cached_stats.sample_count != 0u);
        return;
    }

    if (g_lcd_view == LCD_VIEW_TEMP_LOW) {
        app_show_temperature(g_app_config.temperature_low_x256);
        return;
    }

    if (g_lcd_view == LCD_VIEW_TEMP_HIGH) {
        app_show_temperature(g_app_config.temperature_high_x256);
        return;
    }

    if (g_lcd_view == LCD_VIEW_BATTERY) {
        app_show_battery_percent();
        return;
    }

    if (g_lcd_view == LCD_VIEW_RUNTIME) {
        app_show_runtime();
        return;
    }

    if (g_lcd_view == LCD_VIEW_STATE) {
        templogpro_hw_lcd_show_text3(app_state_text());
        return;
    }

    if (g_lcd_view == LCD_VIEW_CLOCK) {
        app_show_clock(0u);
        return;
    }

    if (g_lcd_view == LCD_VIEW_INTERVAL) {
        app_show_interval(g_logging_interval_seconds);
        return;
    }

    if (g_lcd_view == LCD_VIEW_ECO) {
        /* BATTERY + "Eco" alternating with On/OFF; hold START = on, hold STOP = off. */
        templogpro_hw_lcd_show_text3((((now_ms / 1000u) & 1u) == 0u) ? "Eco" :
                                     ((g_app_config.eco_mode != 0u) ? "On " : "OFF"));
        return;
    }

    if (g_lcd_view == LCD_VIEW_LEDS) {
        /* SUN + PROBE + On/OFF: LEDs enabled; hold START = on, hold STOP = off. */
        templogpro_hw_lcd_show_text3(g_app_config.led_enable ? "On " : "OFF");
        return;
    }

    if (g_lcd_view == LCD_VIEW_ICON_SCAN) {
        uint32_t count = templogpro_hw_lcd_icon_scan_count();

        if (count == 0u) {
            templogpro_hw_lcd_show_text3("non");
            return;
        }
        if ((int32_t)(now_ms - g_next_icon_scan_ms) >= 0) {
            g_next_icon_scan_ms = now_ms + 900u;
            g_icon_scan_index++;
            if (g_icon_scan_index >= count) {
                g_icon_scan_index = 0u;
            }
        }
        templogpro_hw_lcd_show_icon_scan(g_icon_scan_index);
        return;
    }

    if (g_lcd_view == LCD_VIEW_RESET) {
        templogpro_hw_lcd_show_text3("CLr");
        return;
    }

    if (g_lcd_view == LCD_VIEW_RECORDS) {
        if (templogpro_logger_get_info(&info) != 0u) {
            value = info.record_count;
        }
        if (value > 999u) {
            value = 999u;
            templogpro_hw_lcd_set_icon_mask(icon_mask | TEMPLOGPRO_LCD_ICON_BIG_ARROW);
        }
        templogpro_hw_lcd_show_value((uint16_t)value);
        return;
    }

    templogpro_hw_lcd_show_value((uint16_t)value);
}

static void app_update_leds(uint32_t now_ms)
{
    if ((g_app_config.led_enable == 0u) || (g_off_active != 0u) || (app_eco_dark() != 0u)) {
        templogpro_hw_led_green_off();
        templogpro_hw_led_red_off();
        return;
    }

    /* Red: two short flashes every 3 s while an alarm is latched; green is the measurement heartbeat. */
    if ((app_alarm_or_error_active() != 0u) &&
        (app_red_led_on(now_ms) != 0u)) {
        templogpro_hw_led_red_on();
    } else {
        templogpro_hw_led_red_off();
    }

    if ((g_app_state == TEMPLOGPRO_STATE_RUN) &&
        (app_measure_pulse_active(now_ms) != 0u)) {
        templogpro_hw_led_green_on();
    } else {
        templogpro_hw_led_green_off();
    }
}

static void app_process_rtc_events(void)
{
    uint32_t event;
    uint16_t flags;
    uint32_t count = 0u;

    while (count < 2u) {
        event = templogpro_rtc_take_event();
        if (event == TEMPLOGPRO_RTC_EVENT_NONE) {
            return;
        }
        flags = TEMPLOGPRO_LOG_FLAG_VALID;
        if (event == TEMPLOGPRO_RTC_EVENT_TIME_SET) {
            flags |= TEMPLOGPRO_LOG_FLAG_TIME_SET;
        } else if (event == TEMPLOGPRO_RTC_EVENT_TIME_GUARD) {
            flags |= TEMPLOGPRO_LOG_FLAG_TIME_GUARD;
        } else {
            count++;
            continue;
        }
        if (templogpro_logger_append(0, flags) == 0u) {
            return;
        }
        g_stats_cache_valid = 0u;
        count++;
    }
}

static void app_store_measurement(int32_t temp_x256, uint32_t valid)
{
    uint16_t flags = TEMPLOGPRO_LOG_FLAG_VALID;
    uint32_t now_ms = HAL_GetTick();

    templogpro_config_get(&g_app_config);

    if (valid == 0u) {
        flags = TEMPLOGPRO_LOG_FLAG_SENSOR_ERR;
        temp_x256 = 0;
    } else {
        if (temp_x256 > g_app_config.temperature_high_x256) {
            flags |= TEMPLOGPRO_LOG_FLAG_TEMP_HIGH;
        }
        if (temp_x256 < g_app_config.temperature_low_x256) {
            flags |= TEMPLOGPRO_LOG_FLAG_TEMP_LOW;
        }
    }
    if (templogpro_logger_append(temp_x256, flags) == 0u) {
        g_app_state = TEMPLOGPRO_STATE_ERROR;
        app_lcd_message("Err", now_ms, 1200u);
    } else {
        g_stats_cache_valid = 0u;
        g_last_temperature_x256 = temp_x256;
        g_last_flags = flags;
        g_have_last_measurement = 1u;
        g_last_measurement_ms = now_ms;
        app_update_lcd();
        g_last_lcd_update_ms = now_ms;
        g_heart_shown = 1u;
        app_update_leds(now_ms);
    }
}

/*
 * Keeps measurements on the grid set at START, so loop and wake-up latency do
 * not accumulate as drift. Slots missed while busy are skipped rather than
 * caught up. The RTC never runs backwards (monotonic floor), so the deadline
 * passed in has always been reached.
 */
static uint32_t app_next_measurement_deadline(uint32_t deadline, uint32_t now_ts)
{
    uint32_t interval = (g_logging_interval_seconds != 0u) ? g_logging_interval_seconds : 1u;

    if (deadline == 0u) {
        return now_ts + interval;
    }
    if ((int32_t)(now_ts - deadline) >= 0) {
        deadline += (((now_ts - deadline) / interval) + 1u) * interval;
    }
    return deadline;
}

static void app_reset_logger(uint32_t now_ms)
{
    if (templogpro_logger_format() == 0u) {
        g_app_state = TEMPLOGPRO_STATE_ERROR;
        app_lcd_message("Err", now_ms, 1200u);
        return;
    }

    g_app_state = TEMPLOGPRO_STATE_READY;
    g_have_last_measurement = 0u;
    g_last_temperature_x256 = 0;
    g_last_flags = TEMPLOGPRO_LOG_FLAG_VALID;
    g_last_measurement_ms = 0u;
    g_next_measurement_timestamp = 0u;
    g_lcd_view = LCD_VIEW_TEMPERATURE;
    memset(&g_cached_stats, 0, sizeof(g_cached_stats));
    g_stats_cache_valid = 0u;
    g_next_stats_refresh_ms = 0u;
    app_lcd_message("CLr", now_ms, 1200u);
    app_update_lcd();
}

static uint32_t app_is_option_card(LcdView view)
{
    /* The clock card toggles clock mode (the idle display shows the time). */
    return ((view == LCD_VIEW_CLOCK) || (view == LCD_VIEW_LEDS) || (view == LCD_VIEW_ECO)) ? 1u : 0u;
}

/* Clock mode / LED cards: hold START = on, hold STOP = off (after the countdown).
   Saving the config erases a Winbond sector, so on a weak battery it is refused
   like CLR/START. */
static void app_set_card_option(LcdView view, uint32_t enable, uint32_t now_ms)
{
    TempLogProConfig next_config = g_app_config;
    uint32_t *option = (view == LCD_VIEW_LEDS) ? &next_config.led_enable :
                       (view == LCD_VIEW_ECO) ? &next_config.eco_mode : &next_config.clock_mode;

    g_lcd_view = view;
    if (*option != enable) {
        if (app_flash_power_ok() == 0u) {
            app_battery_warning(now_ms, 3000u);
            app_update_lcd();
            return;
        }
        *option = enable;
        if (templogpro_config_set(&next_config) == 0u) {
            app_lcd_message("Err", now_ms, 1200u);
            app_update_lcd();
            return;
        }
        g_app_config = next_config;
        g_app_config_generation = templogpro_config_generation();
    }
    app_lcd_message((enable != 0u) ? "On " : "OFF", now_ms, 900u);
    g_lcd_message_icons = app_view_icon_mask(now_ms);  /* the card's icons blink to confirm */
    app_update_leds(now_ms);
    app_update_lcd();
}

static void app_apply_lcd_timeout(uint32_t now_ms)
{
    uint32_t usb_active = app_usb_active();

    if ((g_action_active != 0u) || (g_off_active != 0u)) {
        return;
    }

    if (templogpro_logger_stats_ready() == 0u) {
        g_lcd_awake = 1u;
        g_last_user_activity_ms = now_ms;
        return;
    }

    if (usb_active != 0u) {
        g_last_usb_active = 1u;
        if (g_lcd_awake == 0u) {
            g_lcd_awake = 1u;
            g_lcd_view = LCD_VIEW_TEMPERATURE;
            g_lcd_message_until_ms = 0u;
        }
    } else if (g_last_usb_active != 0u) {
        g_last_usb_active = 0u;
        g_last_user_activity_ms = now_ms;
        g_lcd_awake = 1u;
        g_lcd_view = LCD_VIEW_TEMPERATURE;
        g_lcd_message_until_ms = 0u;
    }

    if (g_lcd_awake == 0u) {
        return;
    }

    if ((usb_active == 0u) &&
        ((now_ms - g_last_user_activity_ms) >= LCD_HOME_TIMEOUT_MS)) {
        g_lcd_awake = 0u;
        g_lcd_message_until_ms = 0u;
        app_update_lcd();
        return;
    }
    if (g_lcd_view == LCD_VIEW_TEMPERATURE) {
        return;
    }
    if ((now_ms - g_last_user_activity_ms) < LCD_HOME_TIMEOUT_MS) {
        return;
    }
    g_lcd_view = LCD_VIEW_TEMPERATURE;
    g_lcd_message_until_ms = 0u;
}

/* Local wall clock (RTC in UTC + time zone), 12-hour "H:MM": the digit field
   is "1 8:88", so hours 13-23 cannot be shown. The colon blinks every second. */
static uint32_t app_local_datetime(TempLogProDateTime *local)
{
    TempLogProDateTime now;
    uint32_t utc;

    if ((templogpro_rtc_get(&now) == 0u) || (templogpro_rtc_datetime_to_timestamp(&now, &utc) == 0u)) {
        return 0u;
    }
    return templogpro_rtc_timestamp_to_datetime(
        (uint32_t)((int32_t)utc + (templogpro_tz_offset_minutes(&g_app_config, utc) * 60)), local);
}

static uint32_t app_local_hour(void)
{
    TempLogProDateTime local;

    return (app_local_datetime(&local) != 0u) ? local.hour : 0u;
}

static void app_show_clock(uint32_t steady_colon)
{
    TempLogProDateTime now;
    uint32_t hour;

    if (app_local_datetime(&now) == 0u) {
        templogpro_hw_lcd_show_text3("---");
        return;
    }
    hour = now.hour % 12u;
    if (hour == 0u) {
        hour = 12u;
    }
    templogpro_hw_lcd_show_clock12(hour, now.minute,
                                  ((steady_colon != 0u) || ((now.second & 1u) == 0u)) ? 1u : 0u);
}

static uint32_t app_base_icon_mask(uint32_t now_ms)
{
    uint32_t mask = (app_logger_stopped() != 0u) ? TEMPLOGPRO_LCD_ICON_STOP : 0u;

    /* Heartbeat on every measurement, also while an alarm is latched. */
    if (app_measure_pulse_active(now_ms) != 0u) {
        mask |= TEMPLOGPRO_LCD_ICON_HEART;
    }
    if (app_alarm_or_error_active() != 0u) {
        /* Steady X plus the small triangle(s) next to it for the alarm direction. */
        uint16_t alarm_flags = app_alarm_history_flags();

        mask |= TEMPLOGPRO_LCD_ICON_X;
        if ((alarm_flags & TEMPLOGPRO_LOG_FLAG_TEMP_HIGH) != 0u) {
            mask |= TEMPLOGPRO_LCD_ICON_UP;
        }
        if ((alarm_flags & TEMPLOGPRO_LOG_FLAG_TEMP_LOW) != 0u) {
            mask |= TEMPLOGPRO_LCD_ICON_DOWN;
        }
        return mask;
    }

    mask |= TEMPLOGPRO_LCD_ICON_CHECK;
    return mask;
}

static uint32_t app_view_icon_mask(uint32_t now_ms)
{
    uint32_t seconds;

    (void)now_ms;

    switch (g_lcd_view) {
    case LCD_VIEW_TEMPERATURE:
        return TEMPLOGPRO_LCD_ICON_TEMP | app_degree_icon();
    case LCD_VIEW_RECORDS:
        return TEMPLOGPRO_LCD_ICON_PROBE;
    case LCD_VIEW_TEMP_MIN:
        return TEMPLOGPRO_LCD_ICON_TEMP | TEMPLOGPRO_LCD_ICON_MIN | app_degree_icon();
    case LCD_VIEW_TEMP_MAX:
        return TEMPLOGPRO_LCD_ICON_TEMP | TEMPLOGPRO_LCD_ICON_MAX | app_degree_icon();
    case LCD_VIEW_TEMP_AVG:
        return TEMPLOGPRO_LCD_ICON_TEMP | TEMPLOGPRO_LCD_ICON_AVG | app_degree_icon();
    case LCD_VIEW_TEMP_LOW:
        /* Alarm thresholds add PROBE to the TEMP MIN/MAX of the measured
           extremes shown on the neighbouring card. */
        return TEMPLOGPRO_LCD_ICON_PROBE | TEMPLOGPRO_LCD_ICON_TEMP | TEMPLOGPRO_LCD_ICON_MIN |
               app_degree_icon();
    case LCD_VIEW_TEMP_HIGH:
        return TEMPLOGPRO_LCD_ICON_PROBE | TEMPLOGPRO_LCD_ICON_TEMP | TEMPLOGPRO_LCD_ICON_MAX |
               app_degree_icon();
    case LCD_VIEW_BATTERY:
        return TEMPLOGPRO_LCD_ICON_BATTERY | TEMPLOGPRO_LCD_ICON_PERCENT;
    case LCD_VIEW_RUNTIME:
        seconds = app_runtime_seconds();
        return TEMPLOGPRO_LCD_ICON_HOURGLASS | app_runtime_unit_icon_mask(seconds);
    case LCD_VIEW_STATE:
        if ((g_app_state == TEMPLOGPRO_STATE_PAUSE) || (g_app_state == TEMPLOGPRO_STATE_STOPPING)) {
            return TEMPLOGPRO_LCD_ICON_PROBE | TEMPLOGPRO_LCD_ICON_STOP;
        }
        return TEMPLOGPRO_LCD_ICON_PROBE;
    case LCD_VIEW_INTERVAL:
        /* Plain seconds (the interval is at most 600 s), so no unit icon. */
        return TEMPLOGPRO_LCD_ICON_PROBE | TEMPLOGPRO_LCD_ICON_TIME;
    case LCD_VIEW_RESET:
        return 0u;
    case LCD_VIEW_CLOCK:
        /* The sun marks afternoon/evening (12:00-23:59) on the 12-hour clock. */
        return TEMPLOGPRO_LCD_ICON_TIME | ((app_local_hour() >= 12u) ? TEMPLOGPRO_LCD_ICON_SUN : 0u);
    case LCD_VIEW_LEDS:
        return TEMPLOGPRO_LCD_ICON_SUN | TEMPLOGPRO_LCD_ICON_PROBE;
    case LCD_VIEW_ECO:
        return TEMPLOGPRO_LCD_ICON_BATTERY;
    default:
        return 0u;
    }
}

static void app_refresh_stats(uint32_t now_ms)
{
    if ((g_stats_cache_valid != 0u) &&
        ((int32_t)(now_ms - g_next_stats_refresh_ms) < 0)) {
        return;
    }

    if (templogpro_logger_get_stats(&g_cached_stats) != 0u) {
        g_stats_cache_valid = 1u;
        g_next_stats_refresh_ms = now_ms + 5000u;
    } else {
        memset(&g_cached_stats, 0, sizeof(g_cached_stats));
        g_stats_cache_valid = 0u;
        g_next_stats_refresh_ms = now_ms + 1000u;
    }
}

static void app_show_stats_temperature(int32_t value_x256, uint32_t valid)
{
    if (valid == 0u) {
        templogpro_hw_lcd_show_text3("---");
        return;
    }
    app_show_temperature(value_x256);
}

/* Everything is stored in Celsius x256; only the LCD follows the unit setting. */
static void app_show_temperature(int32_t celsius_x256)
{
    int32_t value = celsius_x256;

    if (g_app_config.temperature_unit == TEMPLOGPRO_UNIT_FAHRENHEIT) {
        int32_t scaled = celsius_x256 * 9;
        value = ((scaled >= 0) ? (scaled + 2) : (scaled - 2)) / 5 + (32 * 256);
    }
    templogpro_hw_lcd_show_temperature_x256(value, 0u);
}

static uint32_t app_degree_icon(void)
{
    return (g_app_config.temperature_unit == TEMPLOGPRO_UNIT_FAHRENHEIT) ?
           TEMPLOGPRO_LCD_ICON_DEGREE_F : TEMPLOGPRO_LCD_ICON_DEGREE_C;
}

static void app_show_battery_percent(void)
{
    uint16_t vdd_mv;
    uint32_t percent;

    if (templogpro_power_read_vdd_mv(&vdd_mv) == 0u) {
        templogpro_hw_lcd_show_text3("bAd");
        return;
    }

    percent = app_battery_percent_from_vdd(vdd_mv);
    templogpro_hw_lcd_show_value((uint16_t)percent);
}

static void app_show_runtime(void)
{
    uint32_t seconds = app_runtime_seconds();
    templogpro_hw_lcd_show_value(app_runtime_display_value(seconds));
}

static void app_show_interval(uint32_t seconds)
{
    if (seconds > 999u) {
        seconds = 999u;
    }
    templogpro_hw_lcd_show_value((uint16_t)seconds);
}

static uint32_t app_runtime_unit_icon_mask(uint32_t seconds)
{
    uint32_t hours;

    if (seconds < 60u) {
        return 0u;
    }
    if (seconds < 3600u) {
        return TEMPLOGPRO_LCD_ICON_MINS;
    }
    hours = seconds / 3600u;
    if (hours < 1000u) {
        return TEMPLOGPRO_LCD_ICON_HRS;
    }
    return TEMPLOGPRO_LCD_ICON_DAYS;
}

static uint16_t app_runtime_display_value(uint32_t seconds)
{
    uint32_t value;
    uint32_t hours;

    if (seconds < 60u) {
        value = seconds;
    } else if (seconds < 3600u) {
        value = seconds / 60u;
    } else {
        hours = seconds / 3600u;
        if (hours < 1000u) {
            value = hours;
        } else {
            value = seconds / 86400u;
        }
    }
    if (value > 999u) {
        value = 999u;
    }
    return (uint16_t)value;
}

/* One heartbeat per measurement, shared by the LCD heart and the green LED. */
static uint32_t app_measure_pulse_active(uint32_t now_ms)
{
    uint32_t pulse_ms = (app_usb_active() != 0u) ?
                        APP_MEASURE_PULSE_MS :
                        APP_MEASURE_LED_BATTERY_PULSE_MS;

    if ((g_have_last_measurement == 0u) || (g_last_measurement_ms == 0u)) {
        return 0u;
    }
    return ((now_ms - g_last_measurement_ms) < pulse_ms) ? 1u : 0u;
}

/* The 3 s double-flash cycle restarts from the first loop pass after the period,
   so the pattern stays regular when the MCU only wakes once a second on battery. */
static uint32_t app_red_led_on(uint32_t now_ms)
{
    uint32_t t = now_ms - g_red_led_start_ms;

    if (t >= APP_RED_LED_PERIOD_MS) {
        g_red_led_start_ms = now_ms;
        t = 0u;
    }
    return ((t < APP_RED_LED_FLASH_MS) ||
            ((t >= (APP_RED_LED_FLASH_MS + APP_RED_LED_GAP_MS)) && (t < APP_RED_LED_PATTERN_MS))) ? 1u : 0u;
}

/* Keep the MCU awake while a double flash runs or is due to start. */
static uint32_t app_red_led_busy(uint32_t now_ms)
{
    uint32_t t = now_ms - g_red_led_start_ms;

    return ((t < APP_RED_LED_PATTERN_MS) || (t >= APP_RED_LED_PERIOD_MS)) ? 1u : 0u;
}

static uint32_t app_alarm_or_error_active(void)
{
    if (g_app_state == TEMPLOGPRO_STATE_ERROR) {
        return 1u;
    }
    if (flags_have_alarm(app_alarm_history_flags()) != 0u) {
        return 1u;
    }
    return 0u;
}

static uint16_t app_alarm_history_flags(void)
{
    uint16_t flags = g_cached_stats.alarm_flags;

    if (g_have_last_measurement != 0u) {
        flags |= g_last_flags;
    }
    return flags;
}

static uint32_t app_idle_icon_mask(uint32_t now_ms)
{
    uint16_t alarm_flags = app_alarm_history_flags();
    uint32_t mask = 0u;

    if ((g_app_state == TEMPLOGPRO_STATE_ERROR) ||
        (flags_have_alarm(alarm_flags) != 0u)) {
        mask |= TEMPLOGPRO_LCD_ICON_X;
        if ((alarm_flags & TEMPLOGPRO_LOG_FLAG_TEMP_HIGH) != 0u) {
            mask |= TEMPLOGPRO_LCD_ICON_UP;
        }
        if ((alarm_flags & TEMPLOGPRO_LOG_FLAG_TEMP_LOW) != 0u) {
            mask |= TEMPLOGPRO_LCD_ICON_DOWN;
        }
    } else {
        mask |= TEMPLOGPRO_LCD_ICON_CHECK;
    }

    if ((g_app_state == TEMPLOGPRO_STATE_RUN) &&
        (app_measure_pulse_active(now_ms) != 0u)) {
        mask |= TEMPLOGPRO_LCD_ICON_HEART;
    }
    if (app_logger_stopped() != 0u) {
        mask |= TEMPLOGPRO_LCD_ICON_STOP;
    }

    return mask;
}

static uint32_t app_usb_active(void)
{
    TempLogProDiagnostics diag;

    templogpro_diag_get(&diag);
    return ((diag.usb_vbus_present != 0u) && (diag.usb_ok != 0u)) ? 1u : 0u;
}

static uint32_t app_logger_stopped(void)
{
    return ((g_app_state == TEMPLOGPRO_STATE_RUN) ||
            (g_app_state == TEMPLOGPRO_STATE_STARTING)) ? 0u : 1u;
}

static const char *app_state_text(void)
{
    switch (g_app_state) {
    case TEMPLOGPRO_STATE_RUN: return "run";
    case TEMPLOGPRO_STATE_PAUSE: return "StP";
    case TEMPLOGPRO_STATE_STARTING: return "rdy";
    case TEMPLOGPRO_STATE_STOPPING: return "StP";
    case TEMPLOGPRO_STATE_CLEARING: return "CLr";
    case TEMPLOGPRO_STATE_ERROR: return "Err";
    case TEMPLOGPRO_STATE_READY: return "rdY";
    default: return "IdL";
    }
}

static uint32_t app_runtime_seconds(void)
{
    TempLogProLoggerInfo info;

    if (templogpro_logger_get_info(&info) == 0u) {
        return 0u;
    }
    if (info.active_seconds != 0u) {
        return info.active_seconds;
    }
    return g_cached_stats.active_seconds;
}

static uint32_t app_battery_percent_from_vdd(uint16_t vdd_mv)
{
    uint32_t mv = vdd_mv;

    if (mv >= 3000u) {
        return 100u;
    }
    if (mv <= 2000u) {
        return 0u;
    }
    return ((mv - 2000u) * 100u + 500u) / 1000u;
}

static uint32_t flags_have_alarm(uint16_t flags)
{
    return ((flags & (TEMPLOGPRO_LOG_FLAG_SENSOR_ERR |
                      TEMPLOGPRO_LOG_FLAG_TEMP_HIGH |
                      TEMPLOGPRO_LOG_FLAG_TEMP_LOW)) != 0u) ? 1u : 0u;
}

static LcdView app_sane_lcd_view(uint32_t lcd_mode)
{
    if ((lcd_mode <= (uint32_t)LCD_VIEW_CLOCK) &&
        ((g_app_config.lcd_menu_mask & (1u << lcd_mode)) != 0u)) {
        return (LcdView)lcd_mode;
    }
    return LCD_VIEW_TEMPERATURE;
}

static uint32_t app_menu_sequence_index(LcdView view)
{
    uint32_t index;

    for (index = 0u; index < LCD_MENU_SEQUENCE_COUNT; index++) {
        if (LCD_MENU_SEQUENCE[index] == view) {
            return index;
        }
    }
    return 0u;
}
