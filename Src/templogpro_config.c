#include "templogpro_config.h"
#include "templogpro_version.h"
#include "templogpro_diag.h"
#include "templogpro_hw_bridge.h"
#include "templogpro_storage_layout.h"
#include <string.h>

#define CONFIG_MAGIC                0x54434647u
#define CONFIG_VERSION              7u
#define CONFIG_TZ_VERSION           6u
#define CONFIG_UNIT_VERSION         5u
#define CONFIG_MENU_VERSION         4u
#define CONFIG_USB_VERSION          3u
#define CONFIG_LED_VERSION          2u
#define CONFIG_LEGACY_VERSION       1u
#define CONFIG_ENCODED_SIZE         96u
#define CONFIG_V5_CRC_OFFSET        46u
#define CONFIG_LEGACY_CRC_OFFSET    38u
#define CONFIG_V4_CRC_OFFSET        42u
#define CONFIG_CRC_OFFSET           94u
#define CONFIG_TZ_NAME_OFFSET       62u
#define CONFIG_DEFAULT_TEMP_LOW     (-40 * 256)
#define CONFIG_DEFAULT_TEMP_HIGH    (60 * 256)
#define CONFIG_LEGACY_TEMP_LOW      (2 * 256)
#define CONFIG_LEGACY_TEMP_HIGH     (8 * 256)

typedef struct
{
    uint32_t magic;
    uint16_t version;
    uint32_t generation;
    TempLogProConfig value;
} StoredConfig;

/*
 * Two copies (A, B) in separate sectors. A save always erases and rewrites the
 * copy that does not hold the current configuration, so an interrupted save
 * leaves the previous configuration intact. The newest valid copy wins.
 */
static const uint32_t g_config_addresses[2] = {
    TEMPLOGPRO_CONFIG_ADDRESS,
    TEMPLOGPRO_CONFIG_B_ADDRESS
};

static TempLogProConfig g_config;
static uint32_t g_config_generation;
static uint32_t g_config_valid;
static uint32_t g_config_copy;  /* index of the copy holding g_config, 2 = none */

static uint16_t crc16_ccitt_local(const uint8_t *data, uint32_t len);
static void put_u16(uint8_t *buf, uint32_t offset, uint16_t value);
static void put_u32(uint8_t *buf, uint32_t offset, uint32_t value);
static uint16_t get_u16(const uint8_t *buf, uint32_t offset);
static uint32_t get_u32(const uint8_t *buf, uint32_t offset);
static void encode_config(const StoredConfig *config, uint8_t *raw);
static void encode_rule(uint8_t *raw, const TempLogProDstRule *rule);
static void decode_rule(const uint8_t *raw, TempLogProDstRule *rule);
static uint32_t tz_rule_sane(const TempLogProDstRule *rule);
static uint32_t append_i32(char *buf, uint32_t pos, uint32_t max, int32_t value);
static uint32_t append_rule(char *buf, uint32_t pos, uint32_t max, const TempLogProDstRule *rule);
static uint32_t parse_i32_value(const uint8_t *text, uint32_t len, int32_t *value);
static uint32_t parse_rule_value(const uint8_t *text, uint32_t len, TempLogProDstRule *rule);
static uint32_t parse_name_value(const uint8_t *text, uint32_t len, char *name);
static uint32_t decode_config(const uint8_t *raw, StoredConfig *config);
static void migrate_legacy_defaults(StoredConfig *config);
static uint32_t config_is_sane(const TempLogProConfig *config);
static uint32_t append_str(char *buf, uint32_t pos, uint32_t max, const char *text);
static uint32_t append_u32(char *buf, uint32_t pos, uint32_t max, uint32_t value);
static uint32_t append_temp(char *buf, uint32_t pos, uint32_t max, int32_t temp_x256);
static void build_config_text(char *buf, uint32_t len);
static uint32_t parse_config_text(const uint8_t *source, uint32_t length, TempLogProConfig *config);
static uint32_t parse_u32_value(const uint8_t *text, uint32_t len, uint32_t *value);
static uint32_t parse_bool_value(const uint8_t *text, uint32_t len, uint32_t *value);
static uint32_t parse_temp_value(const uint8_t *text, uint32_t len, int32_t *value_x256);
static uint32_t key_equals(const uint8_t *text, uint32_t len, const char *key);
static uint32_t token_equals(const uint8_t *text, uint32_t len, const char *token);
static uint32_t is_space(uint8_t ch);

uint32_t templogpro_config_init(void)
{
    uint8_t raw[CONFIG_ENCODED_SIZE];
    StoredConfig stored;
    StoredConfig best;
    uint32_t copy;

    memset(&best, 0, sizeof(best));
    g_config_copy = 2u;
    if (templogpro_hw_flash_init() == 0u) {
        templogpro_config_defaults(&g_config);
        g_config_generation = 0u;
        g_config_valid = 0u;
        templogpro_diag_set_error(TEMPLOGPRO_ERROR_FLASH_READ);
        return TEMPLOGPRO_CONFIG_STATUS_ERROR;
    }

    for (copy = 0u; copy < 2u; copy++) {
        if ((templogpro_hw_flash_read(g_config_addresses[copy], raw, sizeof(raw)) == 0u) ||
            (decode_config(raw, &stored) == 0u)) {
            continue;
        }
        if ((g_config_copy == 2u) ||
            ((int32_t)(stored.generation - best.generation) > 0)) {
            best = stored;
            g_config_copy = copy;
        }
    }

    if (g_config_copy != 2u) {
        migrate_legacy_defaults(&best);
        g_config = best.value;
        g_config_generation = best.generation;
        g_config_valid = 1u;
        return TEMPLOGPRO_CONFIG_STATUS_OK;
    }

    templogpro_config_defaults(&g_config);
    g_config_generation = 0u;
    g_config_valid = 1u;
    return TEMPLOGPRO_CONFIG_STATUS_DEFAULTS;
}

void templogpro_config_get(TempLogProConfig *config)
{
    if (config == 0) {
        return;
    }
    if (g_config_valid == 0u) {
        (void)templogpro_config_init();
    }
    *config = g_config;
}

uint32_t templogpro_config_set(const TempLogProConfig *config)
{
    StoredConfig stored;
    uint8_t raw[CONFIG_ENCODED_SIZE];
    uint32_t target;

    if ((config == 0) || (config_is_sane(config) == 0u)) {
        return 0u;
    }
    if (g_config_valid == 0u) {
        (void)templogpro_config_init();
    }

    stored.magic = CONFIG_MAGIC;
    stored.version = CONFIG_VERSION;
    stored.generation = g_config_generation + 1u;
    stored.value = *config;
    encode_config(&stored, raw);

    target = (g_config_copy == 0u) ? 1u : 0u;
    if (templogpro_hw_flash_erase_sector(g_config_addresses[target]) == 0u) {
        templogpro_diag_set_error(TEMPLOGPRO_ERROR_FLASH_WRITE);
        return 0u;
    }
    if (templogpro_hw_flash_write(g_config_addresses[target], raw, sizeof(raw)) == 0u) {
        templogpro_diag_set_error(TEMPLOGPRO_ERROR_FLASH_WRITE);
        return 0u;
    }

    g_config = *config;
    g_config_generation = stored.generation;
    g_config_copy = target;
    g_config_valid = 1u;
    return 1u;
}

void templogpro_config_defaults(TempLogProConfig *config)
{
    if (config == 0) {
        return;
    }
    config->interval_seconds = 5u;
    config->temperature_low_x256 = CONFIG_DEFAULT_TEMP_LOW;
    config->temperature_high_x256 = CONFIG_DEFAULT_TEMP_HIGH;
    config->battery_mode_enable = 1u;
    config->lcd_mode = 0u;
    config->pdf_enable = 1u;
    config->led_enable = 1u;
    config->usb_logging_enable = 1u;
    config->lcd_menu_mask = TEMPLOGPRO_LCD_MENU_ALL;
    config->temperature_unit = TEMPLOGPRO_UNIT_CELSIUS;
    config->tz_std_offset_min = 0;
    config->tz_dst_delta_min = 0;
    memset(&config->tz_dst_start, 0, sizeof(config->tz_dst_start));
    memset(&config->tz_dst_end, 0, sizeof(config->tz_dst_end));
    memset(config->tz_name, 0, sizeof(config->tz_name));
}

uint32_t templogpro_config_text_size(void)
{
    return TEMPLOGPRO_CONFIG_TEXT_SIZE;
}

uint32_t templogpro_config_read_text(uint32_t file_offset, uint8_t *destination, uint32_t requested_length)
{
    static char text[TEMPLOGPRO_CONFIG_TEXT_SIZE];
    uint32_t available;

    if (destination == 0) {
        return 0u;
    }
    build_config_text(text, sizeof(text));
    if (file_offset >= TEMPLOGPRO_CONFIG_TEXT_SIZE) {
        return 0u;
    }
    available = TEMPLOGPRO_CONFIG_TEXT_SIZE - file_offset;
    if (requested_length > available) {
        requested_length = available;
    }
    memcpy(destination, &text[file_offset], requested_length);
    return requested_length;
}

uint32_t templogpro_config_write_text(const uint8_t *source, uint32_t length)
{
    TempLogProConfig next;

    if ((source == 0) || (length == 0u)) {
        return 0u;
    }
    if (g_config_valid == 0u) {
        (void)templogpro_config_init();
    }
    next = g_config;
    if (parse_config_text(source, length, &next) == 0u) {
        return 0u;
    }
    return templogpro_config_set(&next);
}

uint32_t templogpro_config_generation(void)
{
    if (g_config_valid == 0u) {
        (void)templogpro_config_init();
    }
    return g_config_generation;
}

static uint16_t crc16_ccitt_local(const uint8_t *data, uint32_t len)
{
    uint16_t crc = 0xFFFFu;
    uint32_t i;
    uint32_t bit;

    for (i = 0u; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (bit = 0u; bit < 8u; bit++) {
            if ((crc & 0x8000u) != 0u) {
                crc = (uint16_t)((crc << 1) ^ 0x1021u);
            } else {
                crc = (uint16_t)(crc << 1);
            }
        }
    }
    return crc;
}

static void put_u16(uint8_t *buf, uint32_t offset, uint16_t value)
{
    buf[offset] = (uint8_t)value;
    buf[offset + 1u] = (uint8_t)(value >> 8);
}

static void put_u32(uint8_t *buf, uint32_t offset, uint32_t value)
{
    buf[offset] = (uint8_t)value;
    buf[offset + 1u] = (uint8_t)(value >> 8);
    buf[offset + 2u] = (uint8_t)(value >> 16);
    buf[offset + 3u] = (uint8_t)(value >> 24);
}

static uint16_t get_u16(const uint8_t *buf, uint32_t offset)
{
    return (uint16_t)(buf[offset] | ((uint16_t)buf[offset + 1u] << 8));
}

static uint32_t get_u32(const uint8_t *buf, uint32_t offset)
{
    return (uint32_t)buf[offset] |
           ((uint32_t)buf[offset + 1u] << 8) |
           ((uint32_t)buf[offset + 2u] << 16) |
           ((uint32_t)buf[offset + 3u] << 24);
}

static void encode_config(const StoredConfig *config, uint8_t *raw)
{
    uint16_t crc;

    memset(raw, 0, CONFIG_ENCODED_SIZE);
    put_u32(raw, 0u, config->magic);
    put_u16(raw, 4u, config->version);
    put_u16(raw, 6u, (uint16_t)config->value.lcd_menu_mask);
    put_u32(raw, 8u, config->generation);
    put_u32(raw, 12u, config->value.interval_seconds);
    put_u32(raw, 16u, (uint32_t)config->value.temperature_low_x256);
    put_u32(raw, 20u, (uint32_t)config->value.temperature_high_x256);
    put_u32(raw, 24u, config->value.battery_mode_enable);
    put_u32(raw, 28u, config->value.lcd_mode);
    put_u32(raw, 32u, config->value.pdf_enable);
    put_u32(raw, 36u, config->value.led_enable);
    put_u16(raw, 40u, (uint16_t)config->value.usb_logging_enable);
    put_u16(raw, 42u, (uint16_t)config->value.temperature_unit);
    put_u16(raw, 44u, (uint16_t)(int16_t)config->value.tz_std_offset_min);
    put_u16(raw, 46u, (uint16_t)(int16_t)config->value.tz_dst_delta_min);
    encode_rule(&raw[48], &config->value.tz_dst_start);
    encode_rule(&raw[54], &config->value.tz_dst_end);
    memcpy(&raw[CONFIG_TZ_NAME_OFFSET], config->value.tz_name, TEMPLOGPRO_TZ_NAME_SIZE);
    raw[CONFIG_TZ_NAME_OFFSET + TEMPLOGPRO_TZ_NAME_SIZE - 1u] = 0u;
    crc = crc16_ccitt_local(raw, CONFIG_CRC_OFFSET);
    put_u16(raw, CONFIG_CRC_OFFSET, crc);
}

static void encode_rule(uint8_t *raw, const TempLogProDstRule *rule)
{
    raw[0] = rule->month;
    raw[1] = rule->week;
    raw[2] = rule->weekday;
    raw[3] = 0u;
    put_u16(raw, 4u, rule->minute);
}

static void decode_rule(const uint8_t *raw, TempLogProDstRule *rule)
{
    rule->month = raw[0];
    rule->week = raw[1];
    rule->weekday = raw[2];
    rule->minute = get_u16(raw, 4u);
}

static uint32_t tz_rule_sane(const TempLogProDstRule *rule)
{
    return ((rule->month >= 1u) && (rule->month <= 12u) && (rule->week >= 1u) && (rule->week <= 5u) &&
            (rule->weekday <= 6u) && (rule->minute < 1440u)) ? 1u : 0u;
}

static uint32_t decode_config(const uint8_t *raw, StoredConfig *config)
{
    uint16_t version;
    uint16_t stored_crc;
    uint16_t calculated_crc;

    config->magic = get_u32(raw, 0u);
    version = get_u16(raw, 4u);
    if (config->magic != CONFIG_MAGIC) {
        return 0u;
    }

    if (version == CONFIG_LEGACY_VERSION) {
        stored_crc = get_u16(raw, CONFIG_LEGACY_CRC_OFFSET);
        calculated_crc = crc16_ccitt_local(raw, CONFIG_LEGACY_CRC_OFFSET);
    } else if ((version == CONFIG_LED_VERSION) || (version == CONFIG_USB_VERSION) ||
               (version == CONFIG_MENU_VERSION)) {
        stored_crc = get_u16(raw, CONFIG_V4_CRC_OFFSET);
        calculated_crc = crc16_ccitt_local(raw, CONFIG_V4_CRC_OFFSET);
    } else if (version == CONFIG_UNIT_VERSION) {
        stored_crc = get_u16(raw, CONFIG_V5_CRC_OFFSET);
        calculated_crc = crc16_ccitt_local(raw, CONFIG_V5_CRC_OFFSET);
    } else if ((version == CONFIG_TZ_VERSION) || (version == CONFIG_VERSION)) {
        stored_crc = get_u16(raw, CONFIG_CRC_OFFSET);
        calculated_crc = crc16_ccitt_local(raw, CONFIG_CRC_OFFSET);
    } else {
        return 0u;
    }
    if (stored_crc != calculated_crc) {
        return 0u;
    }

    config->version = version;
    config->generation = get_u32(raw, 8u);
    config->value.interval_seconds = get_u32(raw, 12u);
    config->value.temperature_low_x256 = (int32_t)get_u32(raw, 16u);
    config->value.temperature_high_x256 = (int32_t)get_u32(raw, 20u);
    config->value.battery_mode_enable = get_u32(raw, 24u);
    config->value.lcd_mode = get_u32(raw, 28u);
    config->value.pdf_enable = get_u32(raw, 32u);
    if (version >= CONFIG_LED_VERSION) {
        config->value.led_enable = get_u32(raw, 36u);
    } else {
        config->value.led_enable = 1u;
    }
    /* Preserve USB logging when loading configurations from earlier firmware. */
    config->value.usb_logging_enable = (version >= CONFIG_USB_VERSION) ? get_u16(raw, 40u) : 1u;
    config->value.lcd_menu_mask = (version >= CONFIG_MENU_VERSION) ? get_u16(raw, 6u) : TEMPLOGPRO_LCD_MENU_ALL;
    config->value.temperature_unit = (version >= CONFIG_UNIT_VERSION) ? get_u16(raw, 42u) : TEMPLOGPRO_UNIT_CELSIUS;
    /* v1-v5 had no time zone: UTC without daylight saving, as the clock was set. */
    memset(&config->value.tz_dst_start, 0, sizeof(config->value.tz_dst_start));
    memset(&config->value.tz_dst_end, 0, sizeof(config->value.tz_dst_end));
    memset(config->value.tz_name, 0, sizeof(config->value.tz_name));
    config->value.tz_std_offset_min = 0;
    config->value.tz_dst_delta_min = 0;
    /* v7 added the clock card (bit 12): show it on configurations saved earlier. */
    if (version < CONFIG_VERSION) {
        config->value.lcd_menu_mask |= (1u << 12);
    }
    if (version >= CONFIG_TZ_VERSION) {
        config->value.tz_std_offset_min = (int16_t)get_u16(raw, 44u);
        config->value.tz_dst_delta_min = (int16_t)get_u16(raw, 46u);
        decode_rule(&raw[48], &config->value.tz_dst_start);
        decode_rule(&raw[54], &config->value.tz_dst_end);
        memcpy(config->value.tz_name, &raw[CONFIG_TZ_NAME_OFFSET], TEMPLOGPRO_TZ_NAME_SIZE - 1u);
    }
    /* Older firmware allowed up to 86400 s; keep such a config usable. */
    if (config->value.interval_seconds > TEMPLOGPRO_CONFIG_MAX_INTERVAL_S) {
        config->value.interval_seconds = TEMPLOGPRO_CONFIG_MAX_INTERVAL_S;
    }

    return config_is_sane(&config->value);
}

static void migrate_legacy_defaults(StoredConfig *config)
{
    if (config == 0) {
        return;
    }
    if ((config->generation <= 1u) &&
        (config->value.interval_seconds == 5u) &&
        (config->value.temperature_low_x256 == CONFIG_LEGACY_TEMP_LOW) &&
        (config->value.temperature_high_x256 == CONFIG_LEGACY_TEMP_HIGH)) {
        config->value.temperature_low_x256 = CONFIG_DEFAULT_TEMP_LOW;
        config->value.temperature_high_x256 = CONFIG_DEFAULT_TEMP_HIGH;
    }
}

static uint32_t config_is_sane(const TempLogProConfig *config)
{
    if (config->interval_seconds == 0u || config->interval_seconds > TEMPLOGPRO_CONFIG_MAX_INTERVAL_S) {
        return 0u;
    }
    if (config->temperature_low_x256 >= config->temperature_high_x256) {
        return 0u;
    }
    if ((config->temperature_low_x256 < (-100 * 256)) ||
        (config->temperature_high_x256 > (150 * 256))) {
        return 0u;
    }
    if (config->battery_mode_enable > 1u ||
        config->pdf_enable > 1u ||
        config->led_enable > 1u ||
        config->usb_logging_enable > 1u ||
        config->temperature_unit > TEMPLOGPRO_UNIT_FAHRENHEIT) {
        return 0u;
    }
    if (config->lcd_mode > 12u) {
        return 0u;
    }
    /* UTC-12:00 .. UTC+14:00 in 15 min steps; summer shift up to 2 h. */
    if ((config->tz_std_offset_min < -720) || (config->tz_std_offset_min > 840) ||
        ((config->tz_std_offset_min % 15) != 0) ||
        (config->tz_dst_delta_min < 0) || (config->tz_dst_delta_min > 120)) {
        return 0u;
    }
    if ((config->tz_dst_delta_min != 0) &&
        ((tz_rule_sane(&config->tz_dst_start) == 0u) || (tz_rule_sane(&config->tz_dst_end) == 0u))) {
        return 0u;
    }
    /* Home must remain reachable, even when every optional card is hidden. */
    if (((config->lcd_menu_mask & 1u) == 0u) ||
        ((config->lcd_menu_mask & ~TEMPLOGPRO_LCD_MENU_ALL) != 0u)) {
        return 0u;
    }
    return 1u;
}

static uint32_t append_str(char *buf, uint32_t pos, uint32_t max, const char *text)
{
    while ((pos < max) && (*text != '\0')) {
        buf[pos++] = *text++;
    }
    return pos;
}

static uint32_t append_u32(char *buf, uint32_t pos, uint32_t max, uint32_t value)
{
    char tmp[10];
    uint32_t count = 0u;

    if (value == 0u) {
        if (pos < max) {
            buf[pos++] = '0';
        }
        return pos;
    }
    while ((value != 0u) && (count < sizeof(tmp))) {
        tmp[count++] = (char)('0' + (value % 10u));
        value /= 10u;
    }
    while ((count != 0u) && (pos < max)) {
        buf[pos++] = tmp[--count];
    }
    return pos;
}

static uint32_t append_temp(char *buf, uint32_t pos, uint32_t max, int32_t temp_x256)
{
    int32_t value = temp_x256;
    uint32_t milli;
    uint32_t integer;
    uint32_t fraction;

    if (value < 0) {
        if (pos < max) {
            buf[pos++] = '-';
        }
        value = -value;
    }
    milli = ((uint32_t)value * 1000u) / 256u;
    integer = milli / 1000u;
    fraction = milli % 1000u;
    pos = append_u32(buf, pos, max, integer);
    pos = append_str(buf, pos, max, ".");
    if (fraction < 100u) {
        pos = append_str(buf, pos, max, "0");
    }
    if (fraction < 10u) {
        pos = append_str(buf, pos, max, "0");
    }
    return append_u32(buf, pos, max, fraction);
}

static void build_config_text(char *buf, uint32_t len)
{
    uint32_t pos = 0u;

    if (g_config_valid == 0u) {
        (void)templogpro_config_init();
    }
    memset(buf, ' ', len);

    pos = append_str(buf, pos, len, "# " TEMPLOGPRO_PRODUCT_NAME " CONFIG.TXT\r\n");
    pos = append_str(buf, pos, len, "interval_seconds=");
    pos = append_u32(buf, pos, len, g_config.interval_seconds);
    pos = append_str(buf, pos, len, "\r\ntemperature_low_c=");
    pos = append_temp(buf, pos, len, g_config.temperature_low_x256);
    pos = append_str(buf, pos, len, "\r\ntemperature_high_c=");
    pos = append_temp(buf, pos, len, g_config.temperature_high_x256);
    pos = append_str(buf, pos, len, "\r\nbattery_mode=");
    pos = append_u32(buf, pos, len, g_config.battery_mode_enable);
    pos = append_str(buf, pos, len, "\r\nusb_logging=");
    pos = append_u32(buf, pos, len, g_config.usb_logging_enable);
    pos = append_str(buf, pos, len, "\r\nlcd_mode=");
    pos = append_u32(buf, pos, len, g_config.lcd_mode);
    pos = append_str(buf, pos, len, "\r\nlcd_menu_mask=");
    pos = append_u32(buf, pos, len, g_config.lcd_menu_mask);
    pos = append_str(buf, pos, len, "\r\npdf_enable=");
    pos = append_u32(buf, pos, len, g_config.pdf_enable);
    pos = append_str(buf, pos, len, "\r\nled_enable=");
    pos = append_u32(buf, pos, len, g_config.led_enable);
    pos = append_str(buf, pos, len, "\r\ntemperature_unit=");
    pos = append_str(buf, pos, len,
                     (g_config.temperature_unit == TEMPLOGPRO_UNIT_FAHRENHEIT) ? "F" : "C");
    pos = append_str(buf, pos, len, "\r\ntz_name=");
    pos = append_str(buf, pos, len, g_config.tz_name);
    pos = append_str(buf, pos, len, "\r\ntz_offset_min=");
    pos = append_i32(buf, pos, len, g_config.tz_std_offset_min);
    pos = append_str(buf, pos, len, "\r\ntz_dst_min=");
    pos = append_i32(buf, pos, len, g_config.tz_dst_delta_min);
    pos = append_str(buf, pos, len, "\r\ntz_dst_start=");
    pos = append_rule(buf, pos, len, &g_config.tz_dst_start);
    pos = append_str(buf, pos, len, "\r\ntz_dst_end=");
    pos = append_rule(buf, pos, len, &g_config.tz_dst_end);
    pos = append_str(buf, pos, len, "\r\nconfig_generation=");
    pos = append_u32(buf, pos, len, g_config_generation);
    pos = append_str(buf, pos, len, "\r\n# LCD: 0 temp, 1 state, 2 min, 3 max, 4 avg, 5 low, 6 high\r\n");
    pos = append_str(buf, pos, len, "# LCD: 7 runtime, 8 records, 9 interval, 10 battery, 11 clear, 12 clock\r\n");
    pos = append_str(buf, pos, len, "# tz_dst_*: month.week(5=last).weekday(0=Sun).minute; clock is UTC\r\n");

    (void)pos;
}

static uint32_t parse_config_text(const uint8_t *source, uint32_t length, TempLogProConfig *config)
{
    uint32_t pos = 0u;
    uint32_t parsed = 0u;

    if ((length >= 3u) && (source[0] == 0xEFu) && (source[1] == 0xBBu) && (source[2] == 0xBFu)) {
        pos = 3u;
    }

    while (pos < length) {
        uint32_t key_start;
        uint32_t key_end;
        uint32_t value_start;
        uint32_t value_end;
        uint32_t line_end;
        uint32_t value_u32;
        int32_t value_temp;

        while ((pos < length) && ((source[pos] == '\r') || (source[pos] == '\n') ||
                                  (source[pos] == ' ') || (source[pos] == '\t'))) {
            pos++;
        }
        if (pos >= length) {
            break;
        }
        if (source[pos] == '#') {
            while ((pos < length) && (source[pos] != '\r') && (source[pos] != '\n') && (source[pos] != '\0')) {
                pos++;
            }
            continue;
        }
        if (source[pos] == '\0') {
            break;
        }

        key_start = pos;
        while ((pos < length) && (source[pos] != '=') &&
               (source[pos] != '\r') && (source[pos] != '\n') && (source[pos] != '\0')) {
            pos++;
        }
        if ((pos >= length) || (source[pos] != '=')) {
            while ((pos < length) && (source[pos] != '\r') && (source[pos] != '\n') && (source[pos] != '\0')) {
                pos++;
            }
            continue;
        }
        key_end = pos;
        while ((key_end > key_start) && is_space(source[key_end - 1u])) {
            key_end--;
        }

        pos++;
        while ((pos < length) && is_space(source[pos])) {
            pos++;
        }
        value_start = pos;
        while ((pos < length) && (source[pos] != '\r') && (source[pos] != '\n') && (source[pos] != '\0')) {
            pos++;
        }
        line_end = pos;
        value_end = line_end;
        while ((value_end > value_start) && is_space(source[value_end - 1u])) {
            value_end--;
        }

        if (key_equals(&source[key_start], key_end - key_start, "interval_seconds") != 0u) {
            if (parse_u32_value(&source[value_start], value_end - value_start, &value_u32) == 0u) {
                return 0u;
            }
            config->interval_seconds = value_u32;
            parsed = 1u;
        } else if (key_equals(&source[key_start], key_end - key_start, "temperature_low_c") != 0u) {
            if (parse_temp_value(&source[value_start], value_end - value_start, &value_temp) == 0u) {
                return 0u;
            }
            config->temperature_low_x256 = value_temp;
            parsed = 1u;
        } else if (key_equals(&source[key_start], key_end - key_start, "temperature_high_c") != 0u) {
            if (parse_temp_value(&source[value_start], value_end - value_start, &value_temp) == 0u) {
                return 0u;
            }
            config->temperature_high_x256 = value_temp;
            parsed = 1u;
        } else if (key_equals(&source[key_start], key_end - key_start, "battery_mode") != 0u) {
            if (parse_bool_value(&source[value_start], value_end - value_start, &value_u32) == 0u) {
                return 0u;
            }
            config->battery_mode_enable = value_u32;
            parsed = 1u;
        } else if (key_equals(&source[key_start], key_end - key_start, "usb_logging") != 0u) {
            if (parse_bool_value(&source[value_start], value_end - value_start, &value_u32) == 0u) {
                return 0u;
            }
            config->usb_logging_enable = value_u32;
            parsed = 1u;
        } else if (key_equals(&source[key_start], key_end - key_start, "lcd_mode") != 0u) {
            if (parse_u32_value(&source[value_start], value_end - value_start, &value_u32) == 0u) {
                return 0u;
            }
            config->lcd_mode = value_u32;
            parsed = 1u;
        } else if (key_equals(&source[key_start], key_end - key_start, "lcd_menu_mask") != 0u) {
            if (parse_u32_value(&source[value_start], value_end - value_start, &value_u32) == 0u) {
                return 0u;
            }
            config->lcd_menu_mask = value_u32;
            parsed = 1u;
        } else if (key_equals(&source[key_start], key_end - key_start, "pdf_enable") != 0u) {
            if (parse_bool_value(&source[value_start], value_end - value_start, &value_u32) == 0u) {
                return 0u;
            }
            config->pdf_enable = value_u32;
            parsed = 1u;
        } else if (key_equals(&source[key_start], key_end - key_start, "led_enable") != 0u) {
            if (parse_bool_value(&source[value_start], value_end - value_start, &value_u32) == 0u) {
                return 0u;
            }
            config->led_enable = value_u32;
            parsed = 1u;
        } else if (key_equals(&source[key_start], key_end - key_start, "tz_name") != 0u) {
            if (parse_name_value(&source[value_start], value_end - value_start, config->tz_name) == 0u) {
                return 0u;
            }
            parsed = 1u;
        } else if (key_equals(&source[key_start], key_end - key_start, "tz_offset_min") != 0u) {
            if (parse_i32_value(&source[value_start], value_end - value_start, &value_temp) == 0u) {
                return 0u;
            }
            config->tz_std_offset_min = value_temp;
            parsed = 1u;
        } else if (key_equals(&source[key_start], key_end - key_start, "tz_dst_min") != 0u) {
            if (parse_i32_value(&source[value_start], value_end - value_start, &value_temp) == 0u) {
                return 0u;
            }
            config->tz_dst_delta_min = value_temp;
            parsed = 1u;
        } else if (key_equals(&source[key_start], key_end - key_start, "tz_dst_start") != 0u) {
            if (parse_rule_value(&source[value_start], value_end - value_start, &config->tz_dst_start) == 0u) {
                return 0u;
            }
            parsed = 1u;
        } else if (key_equals(&source[key_start], key_end - key_start, "tz_dst_end") != 0u) {
            if (parse_rule_value(&source[value_start], value_end - value_start, &config->tz_dst_end) == 0u) {
                return 0u;
            }
            parsed = 1u;
        } else if (key_equals(&source[key_start], key_end - key_start, "temperature_unit") != 0u) {
            if ((token_equals(&source[value_start], value_end - value_start, "c") != 0u) ||
                (token_equals(&source[value_start], value_end - value_start, "0") != 0u)) {
                config->temperature_unit = TEMPLOGPRO_UNIT_CELSIUS;
            } else if ((token_equals(&source[value_start], value_end - value_start, "f") != 0u) ||
                       (token_equals(&source[value_start], value_end - value_start, "1") != 0u)) {
                config->temperature_unit = TEMPLOGPRO_UNIT_FAHRENHEIT;
            } else {
                return 0u;
            }
            parsed = 1u;
        }

        pos = line_end;
    }

    if (parsed == 0u) {
        return 0u;
    }
    return config_is_sane(config);
}

static uint32_t parse_u32_value(const uint8_t *text, uint32_t len, uint32_t *value)
{
    uint32_t pos = 0u;
    uint32_t out = 0u;
    uint32_t digits = 0u;

    while ((pos < len) && is_space(text[pos])) {
        pos++;
    }
    while ((pos < len) && (text[pos] >= '0') && (text[pos] <= '9')) {
        out = (out * 10u) + (uint32_t)(text[pos] - '0');
        pos++;
        digits++;
    }
    while ((pos < len) && is_space(text[pos])) {
        pos++;
    }
    if ((digits == 0u) || (pos != len)) {
        return 0u;
    }
    *value = out;
    return 1u;
}

static uint32_t parse_bool_value(const uint8_t *text, uint32_t len, uint32_t *value)
{
    uint32_t number;

    if (parse_u32_value(text, len, &number) != 0u) {
        if (number > 1u) {
            return 0u;
        }
        *value = number;
        return 1u;
    }
    if ((token_equals(text, len, "on") != 0u) ||
        (token_equals(text, len, "true") != 0u) ||
        (token_equals(text, len, "yes") != 0u)) {
        *value = 1u;
        return 1u;
    }
    if ((token_equals(text, len, "off") != 0u) ||
        (token_equals(text, len, "false") != 0u) ||
        (token_equals(text, len, "no") != 0u)) {
        *value = 0u;
        return 1u;
    }
    return 0u;
}

static uint32_t parse_temp_value(const uint8_t *text, uint32_t len, int32_t *value_x256)
{
    uint32_t pos = 0u;
    uint32_t digits = 0u;
    uint32_t integer = 0u;
    uint32_t fraction = 0u;
    uint32_t fraction_scale = 1u;
    int32_t sign = 1;
    int32_t scaled;

    while ((pos < len) && is_space(text[pos])) {
        pos++;
    }
    if ((pos < len) && ((text[pos] == '-') || (text[pos] == '+'))) {
        if (text[pos] == '-') {
            sign = -1;
        }
        pos++;
    }
    while ((pos < len) && (text[pos] >= '0') && (text[pos] <= '9')) {
        integer = (integer * 10u) + (uint32_t)(text[pos] - '0');
        pos++;
        digits++;
    }
    if ((pos < len) && ((text[pos] == '.') || (text[pos] == ','))) {
        pos++;
        while ((pos < len) && (text[pos] >= '0') && (text[pos] <= '9') && (fraction_scale < 1000u)) {
            fraction = (fraction * 10u) + (uint32_t)(text[pos] - '0');
            fraction_scale *= 10u;
            pos++;
        }
        while ((pos < len) && (text[pos] >= '0') && (text[pos] <= '9')) {
            pos++;
        }
    }
    while ((pos < len) && is_space(text[pos])) {
        pos++;
    }
    if ((digits == 0u) || (pos != len)) {
        return 0u;
    }
    while (fraction_scale < 1000u) {
        fraction *= 10u;
        fraction_scale *= 10u;
    }

    scaled = (int32_t)((integer * 256u) + ((fraction * 256u + 500u) / 1000u));
    *value_x256 = scaled * sign;
    return 1u;
}

static uint32_t key_equals(const uint8_t *text, uint32_t len, const char *key)
{
    uint32_t pos = 0u;

    while ((pos < len) && (key[pos] != '\0')) {
        if (text[pos] != (uint8_t)key[pos]) {
            return 0u;
        }
        pos++;
    }
    return ((pos == len) && (key[pos] == '\0')) ? 1u : 0u;
}

static uint32_t token_equals(const uint8_t *text, uint32_t len, const char *token)
{
    uint32_t start = 0u;
    uint32_t end = len;
    uint32_t pos = 0u;

    while ((start < end) && is_space(text[start])) {
        start++;
    }
    while ((end > start) && is_space(text[end - 1u])) {
        end--;
    }
    while ((start + pos) < end) {
        uint8_t a = text[start + pos];
        uint8_t b = (uint8_t)token[pos];

        if ((a >= 'A') && (a <= 'Z')) {
            a = (uint8_t)(a + ('a' - 'A'));
        }
        if (a != b) {
            return 0u;
        }
        pos++;
    }
    return (token[pos] == '\0') ? 1u : 0u;
}

static uint32_t is_space(uint8_t ch)
{
    return ((ch == ' ') || (ch == '\t')) ? 1u : 0u;
}

static uint32_t append_i32(char *buf, uint32_t pos, uint32_t max, int32_t value)
{
    if (value < 0) {
        pos = append_str(buf, pos, max, "-");
        return append_u32(buf, pos, max, (uint32_t)(-value));
    }
    return append_u32(buf, pos, max, (uint32_t)value);
}

static uint32_t append_rule(char *buf, uint32_t pos, uint32_t max, const TempLogProDstRule *rule)
{
    pos = append_u32(buf, pos, max, rule->month);
    pos = append_str(buf, pos, max, ".");
    pos = append_u32(buf, pos, max, rule->week);
    pos = append_str(buf, pos, max, ".");
    pos = append_u32(buf, pos, max, rule->weekday);
    pos = append_str(buf, pos, max, ".");
    return append_u32(buf, pos, max, rule->minute);
}

static uint32_t parse_i32_value(const uint8_t *text, uint32_t len, int32_t *value)
{
    uint32_t magnitude;
    uint32_t negative = 0u;

    if ((len > 1u) && ((text[0] == '-') || (text[0] == '+'))) {
        negative = (text[0] == '-') ? 1u : 0u;
        text++;
        len--;
    }
    if ((parse_u32_value(text, len, &magnitude) == 0u) || (magnitude > 100000u)) {
        return 0u;
    }
    *value = (negative != 0u) ? -(int32_t)magnitude : (int32_t)magnitude;
    return 1u;
}

/* "month.week.weekday.minute", e.g. 3.5.0.120 = last Sunday of March, 02:00. */
static uint32_t parse_rule_value(const uint8_t *text, uint32_t len, TempLogProDstRule *rule)
{
    uint32_t parts[4] = {0u, 0u, 0u, 0u};
    uint32_t part = 0u;
    uint32_t digits = 0u;
    uint32_t i;

    for (i = 0u; i < len; i++) {
        if (text[i] == '.') {
            if ((digits == 0u) || (part >= 3u)) {
                return 0u;
            }
            part++;
            digits = 0u;
        } else if ((text[i] >= '0') && (text[i] <= '9') && (digits < 5u)) {
            parts[part] = (parts[part] * 10u) + (uint32_t)(text[i] - '0');
            digits++;
        } else {
            return 0u;
        }
    }
    if ((part != 3u) || (digits == 0u) || (parts[0] > 12u) || (parts[1] > 5u) ||
        (parts[2] > 6u) || (parts[3] >= 1440u)) {
        return 0u;
    }
    rule->month = (uint8_t)parts[0];
    rule->week = (uint8_t)parts[1];
    rule->weekday = (uint8_t)parts[2];
    rule->minute = (uint16_t)parts[3];
    return 1u;
}

/* IANA zone names: letters, digits and / _ - + only; empty clears the name. */
static uint32_t parse_name_value(const uint8_t *text, uint32_t len, char *name)
{
    uint32_t i;

    if (len >= TEMPLOGPRO_TZ_NAME_SIZE) {
        return 0u;
    }
    for (i = 0u; i < len; i++) {
        uint8_t ch = text[i];
        if (!(((ch >= 'a') && (ch <= 'z')) || ((ch >= 'A') && (ch <= 'Z')) || ((ch >= '0') && (ch <= '9')) ||
              (ch == '/') || (ch == '_') || (ch == '-') || (ch == '+'))) {
            return 0u;
        }
    }
    memset(name, 0, TEMPLOGPRO_TZ_NAME_SIZE);
    memcpy(name, text, len);
    return 1u;
}
