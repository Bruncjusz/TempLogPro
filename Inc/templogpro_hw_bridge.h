#ifndef TEMPLOGPRO_HW_BRIDGE_H
#define TEMPLOGPRO_HW_BRIDGE_H

#include <stdint.h>

#define TEMPLOGPRO_LCD_ICON_HEART       0x00000001u
#define TEMPLOGPRO_LCD_ICON_DEGREE_C    0x00000002u
#define TEMPLOGPRO_LCD_ICON_PERCENT     0x00000004u
#define TEMPLOGPRO_LCD_ICON_RH          0x00000008u
#define TEMPLOGPRO_LCD_ICON_HRS         0x00000010u
#define TEMPLOGPRO_LCD_ICON_MINS        0x00000020u
#define TEMPLOGPRO_LCD_ICON_DAYS        0x00000040u
#define TEMPLOGPRO_LCD_ICON_TEMP        0x00000080u
#define TEMPLOGPRO_LCD_ICON_PROBE       0x00000100u
#define TEMPLOGPRO_LCD_ICON_AVG         0x00000200u
#define TEMPLOGPRO_LCD_ICON_MAX         0x00000400u
#define TEMPLOGPRO_LCD_ICON_MIN         0x00000800u
#define TEMPLOGPRO_LCD_ICON_TIME        0x00001000u
#define TEMPLOGPRO_LCD_ICON_BATTERY     0x00002000u
#define TEMPLOGPRO_LCD_ICON_HOURGLASS   0x00004000u
#define TEMPLOGPRO_LCD_ICON_STOP        0x00008000u
#define TEMPLOGPRO_LCD_ICON_CHECK       0x00010000u
#define TEMPLOGPRO_LCD_ICON_BIG_ARROW   0x00020000u
#define TEMPLOGPRO_LCD_ICON_UP          0x00040000u
#define TEMPLOGPRO_LCD_ICON_DOWN        0x00080000u
#define TEMPLOGPRO_LCD_ICON_X           0x00100000u
#define TEMPLOGPRO_LCD_ICON_SUN         0x00200000u
#define TEMPLOGPRO_LCD_ICON_DEGREE_F    0x00400000u
#define TEMPLOGPRO_ADC_SCAN_MAX_ITEMS   9u

typedef struct
{
    char pin[4];
    uint8_t channel;
    uint16_t raw;
    uint16_t millivolts;
} TempLogProAdcScanItem;

typedef struct
{
    uint32_t valid;
    uint16_t vrefint_raw;
    uint16_t vdd_millivolts;
    uint32_t item_count;
    TempLogProAdcScanItem items[TEMPLOGPRO_ADC_SCAN_MAX_ITEMS];
} TempLogProAdcScan;

void templogpro_hw_init_leds(void);
void templogpro_hw_led_green_on(void);
void templogpro_hw_led_green_off(void);
void templogpro_hw_led_red_on(void);
void templogpro_hw_led_red_off(void);
void templogpro_hw_lcd_init(void);
void templogpro_hw_lcd_suspend(void);
void templogpro_hw_lcd_show_value(uint16_t value);
void templogpro_hw_lcd_show_text3(const char text[3]);
void templogpro_hw_lcd_show_temperature_x256(int32_t temperature_x256, uint32_t heart_on);
void templogpro_hw_lcd_show_vdd_mv(uint16_t millivolts);
void templogpro_hw_lcd_set_icon_mask(uint32_t icon_mask);
void templogpro_hw_lcd_show_icon_scan(uint32_t index);
uint32_t templogpro_hw_lcd_icon_scan_count(void);

uint32_t templogpro_hw_flash_init(void);
uint32_t templogpro_hw_flash_read(uint32_t address, uint8_t *buf, uint32_t len);
uint32_t templogpro_hw_flash_write(uint32_t address, const uint8_t *buf, uint32_t len);
uint32_t templogpro_hw_flash_erase_sector(uint32_t address);
uint32_t templogpro_hw_flash_is_busy(void);

uint32_t templogpro_hw_sensor_init(void);
uint32_t templogpro_hw_sensor_read_x256(int32_t *temperature_x256);
uint32_t templogpro_hw_sensor_read_register(uint8_t reg, uint16_t *value);
uint32_t templogpro_hw_sensor_write_config(uint16_t value);
uint32_t templogpro_hw_adc_read_vdd_mv(uint16_t *vdd_millivolts);
uint32_t templogpro_hw_adc_scan(TempLogProAdcScan *scan);

#endif
