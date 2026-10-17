#ifndef TEMPLOGPRO_APP_H
#define TEMPLOGPRO_APP_H

#include <stdint.h>

typedef enum
{
    TEMPLOGPRO_STATE_READY = 0,
    TEMPLOGPRO_STATE_RUN,
    TEMPLOGPRO_STATE_PAUSE,
    TEMPLOGPRO_STATE_STARTING,
    TEMPLOGPRO_STATE_STOPPING,
    TEMPLOGPRO_STATE_CLEARING,
    TEMPLOGPRO_STATE_ERROR
} TempLogProState;

typedef enum
{
    APP_MODE_COUNTER_TEST = 0,
    APP_MODE_LOGGER
} TempLogProAppMode;

#define TEMPLOGPRO_DEFAULT_INTERVAL_SECONDS 5u

uint32_t templogpro_app_init(TempLogProAppMode mode);
void templogpro_app_process(void);
void templogpro_app_apply_config(void);
uint32_t templogpro_app_usb_report_mode(void);
uint32_t templogpro_app_start_logging(void);
uint32_t templogpro_app_stop_logging(void);
uint32_t templogpro_app_clear_logger(void);
TempLogProState templogpro_app_get_state(void);
uint32_t templogpro_app_should_idle_sleep(void);
uint32_t templogpro_app_should_stop_sleep(void);
uint32_t templogpro_app_stop_wake_seconds(void);
/* OFF (storage) mode: armed over USB, entered when the cable is unplugged,
   left by holding both buttons for 3 s or by plugging USB in again. */
uint32_t templogpro_app_power_off_arm(void);
void templogpro_app_power_off_cancel(void);
uint32_t templogpro_app_power_off_armed(void);

#endif
