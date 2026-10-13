#ifndef TEMPLOGPRO_DIAG_H
#define TEMPLOGPRO_DIAG_H

#include <stdint.h>

typedef enum
{
    TEMPLOGPRO_ERROR_NONE = 0,
    TEMPLOGPRO_ERROR_RTC_LSE,
    TEMPLOGPRO_ERROR_RTC_INVALID,
    TEMPLOGPRO_ERROR_SENSOR,
    TEMPLOGPRO_ERROR_FLASH_READ,
    TEMPLOGPRO_ERROR_FLASH_WRITE,
    TEMPLOGPRO_ERROR_FLASH_CRC,
    TEMPLOGPRO_ERROR_LOG_FULL,
    TEMPLOGPRO_ERROR_LOG_METADATA,
    TEMPLOGPRO_ERROR_LOG_RECORD
} TempLogProError;

typedef struct
{
    uint32_t rtc_ok;
    uint32_t sensor_ok;
    uint32_t flash_ok;
    uint32_t usb_ok;
    uint32_t boot_stage;
    uint32_t last_boot_stage;
    uint32_t reset_flags;
    uint32_t usb_vbus_present;
    TempLogProError last_error;
    uint32_t logger_state;
    uint32_t record_count;
} TempLogProDiagnostics;

#define TEMPLOGPRO_BOOT_STAGE_RESET      1u
#define TEMPLOGPRO_BOOT_STAGE_HAL        2u
#define TEMPLOGPRO_BOOT_STAGE_CLOCK      3u
#define TEMPLOGPRO_BOOT_STAGE_RTC        4u
#define TEMPLOGPRO_BOOT_STAGE_GPIO       5u
#define TEMPLOGPRO_BOOT_STAGE_STORAGE    6u
#define TEMPLOGPRO_BOOT_STAGE_LOGGER     7u
#define TEMPLOGPRO_BOOT_STAGE_LCD        8u
#define TEMPLOGPRO_BOOT_STAGE_USB        9u
#define TEMPLOGPRO_BOOT_STAGE_MAIN_LOOP  10u

void templogpro_diag_init(void);
void templogpro_diag_boot_capture_reset(void);
void templogpro_diag_boot_stage(uint32_t stage);
void templogpro_diag_set_rtc(uint32_t ok);
void templogpro_diag_set_sensor(uint32_t ok);
void templogpro_diag_set_flash(uint32_t ok);
void templogpro_diag_set_usb(uint32_t ok);
void templogpro_diag_set_usb_vbus(uint32_t present);
void templogpro_diag_set_error(TempLogProError error);
void templogpro_diag_set_logger(uint32_t state, uint32_t record_count);
void templogpro_diag_get(TempLogProDiagnostics *diag);
const char *templogpro_diag_error_name(TempLogProError error);
const char *templogpro_diag_reset_flags_name(uint32_t flags);

/* Memory-erasing operations started from the buttons. A marker in an RTC
   backup register survives a brown-out reset, so the next boot can tell that
   the battery gave out in the middle of CLR or START. */
#define TEMPLOGPRO_FLASH_OP_NONE   0u
#define TEMPLOGPRO_FLASH_OP_CLEAR  1u
#define TEMPLOGPRO_FLASH_OP_START  2u
void templogpro_diag_flash_op_begin(uint32_t op);
void templogpro_diag_flash_op_end(void);
uint32_t templogpro_diag_interrupted_op(void);   /* operation cut short by the last reset */
const char *templogpro_diag_reset_cause(void);   /* POWER_DROP_CLEAR, POWER_ON, ... */

#endif
