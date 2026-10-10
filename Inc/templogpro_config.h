#ifndef TEMPLOGPRO_CONFIG_H
#define TEMPLOGPRO_CONFIG_H

#include <stdint.h>

#define TEMPLOGPRO_CONFIG_STATUS_OK          1u
#define TEMPLOGPRO_CONFIG_STATUS_DEFAULTS    2u
#define TEMPLOGPRO_CONFIG_STATUS_ERROR       3u

#define TEMPLOGPRO_CONFIG_TEXT_SIZE          512u
#define TEMPLOGPRO_LCD_MENU_ALL              0x0FFFu
/* The LCD shows the interval as plain seconds on three digits. */
#define TEMPLOGPRO_CONFIG_MAX_INTERVAL_S     600u

/* Unit of every temperature on the LCD; stored values stay in Celsius x256. */
#define TEMPLOGPRO_UNIT_CELSIUS              0u
#define TEMPLOGPRO_UNIT_FAHRENHEIT           1u

typedef struct
{
    uint32_t interval_seconds;
    int32_t temperature_low_x256;
    int32_t temperature_high_x256;
    uint32_t battery_mode_enable;
    uint32_t lcd_mode;
    uint32_t pdf_enable;
    uint32_t led_enable;
    uint32_t usb_logging_enable;
    uint32_t lcd_menu_mask;
    uint32_t temperature_unit;
} TempLogProConfig;

uint32_t templogpro_config_init(void);
void templogpro_config_get(TempLogProConfig *config);
uint32_t templogpro_config_set(const TempLogProConfig *config);
void templogpro_config_defaults(TempLogProConfig *config);
uint32_t templogpro_config_text_size(void);
uint32_t templogpro_config_read_text(uint32_t file_offset, uint8_t *destination, uint32_t requested_length);
uint32_t templogpro_config_write_text(const uint8_t *source, uint32_t length);
uint32_t templogpro_config_generation(void);

#endif
