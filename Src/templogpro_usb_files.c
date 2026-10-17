#include "templogpro_usb_files.h"
#include "templogpro_config.h"
#include "templogpro_diag.h"
#include "templogpro_hw_bridge.h"
#include "templogpro_logger.h"
#include "templogpro_power.h"
#include "templogpro_rtc.h"
#include "templogpro_usb_msc_storage.h"
#include "templogpro_version.h"
#include "templogpro_tz.h"
#include <string.h>

#define CSV_RECORD_LINE_LEN      48u
#define HTML_CONFIG_BLOCK_SIZE   576u

/* Date/Time are local time (stored UTC + UTC_Offset of that moment). */
static const char CSV_HEADER[] = "Sequence,Date,Time,Temperature_C,Code,UTC_Offset\r\n";
/* HTML_PREFIX and HTML_SUFFIX: edit panel/templogpro.html, then run tools/gen_panel.py. */
#include "templogpro_panel_html.inc"

typedef struct
{
    uint32_t valid;
    uint32_t record_count;
    uint32_t csv_size;
    TempLogProLoggerInfo logger_info;
    TempLogProConfig config;  /* time zone used for every CSV line of this snapshot */
} UsbSnapshot;

static UsbSnapshot g_snapshot;

static void copy_range(uint32_t object_offset, const uint8_t *source, uint32_t source_len,
                       uint32_t *file_offset, uint8_t **destination, uint32_t *remaining);
static uint32_t html_copy(uint32_t file_offset, uint8_t *destination, uint32_t requested_length);
static void html_config_block_build(char *buf, uint32_t len);
static void readme_build(char *buf, uint32_t len);
static void csv_make_line(uint32_t index, char *line);
static void put_dec(char *dst, uint32_t digits, uint32_t value);
static void put_code4(char *dst, uint16_t flags);
static void put_temp_x256(char *dst, int16_t temp_x256);
static void put_datetime_date(char *dst, const TempLogProDateTime *dt);
static void put_datetime_time(char *dst, const TempLogProDateTime *dt);
static void status_build(char *buf, uint32_t len);
static void adc_build(char *buf, uint32_t len);
static void power_build(char *buf, uint32_t len);
static uint32_t append_str(char *buf, uint32_t pos, uint32_t max, const char *text);
static uint32_t append_u32(char *buf, uint32_t pos, uint32_t max, uint32_t value);
static uint32_t append_hex_u32(char *buf, uint32_t pos, uint32_t max, uint32_t value);
static uint32_t append_temp_x256(char *buf, uint32_t pos, uint32_t max, int32_t temp_x256);
static uint32_t append_dt(char *buf, uint32_t pos, uint32_t max, const TempLogProDateTime *dt);
static uint32_t append_pin(char *buf, uint32_t pos, uint32_t max, const char pin[4]);
static const char *state_name(uint32_t state);
static const char *config_write_status_name(uint32_t status);

void templogpro_usb_msc_storage_media_changed(void);

void templogpro_usb_files_snapshot_reset(void)
{
    memset(&g_snapshot, 0, sizeof(g_snapshot));
    templogpro_usb_msc_storage_layout_reset();
    templogpro_usb_msc_storage_media_changed();
}

void templogpro_usb_files_snapshot_ensure(void)
{
    if (g_snapshot.valid != 0u) {
        return;
    }

    memset(&g_snapshot, 0, sizeof(g_snapshot));
    if (templogpro_logger_get_info(&g_snapshot.logger_info) != 0u) {
        g_snapshot.record_count = g_snapshot.logger_info.record_count;
    } else {
        g_snapshot.record_count = 0u;
    }
    templogpro_config_get(&g_snapshot.config);
    g_snapshot.csv_size = (uint32_t)(sizeof(CSV_HEADER) - 1u) +
                          (g_snapshot.record_count * CSV_RECORD_LINE_LEN);
    g_snapshot.valid = 1u;
}

uint32_t templogpro_html_get_size(void)
{
    return (uint32_t)(sizeof(HTML_PREFIX) - 1u) +
           templogpro_csv_get_size() +
           HTML_CONFIG_BLOCK_SIZE +
           (uint32_t)(sizeof(HTML_SUFFIX) - 1u);
}

uint32_t templogpro_html_read(uint32_t file_offset, uint8_t *destination, uint32_t requested_length)
{
    if (destination == 0) {
        return 0u;
    }
    return html_copy(file_offset, destination, requested_length);
}

uint32_t templogpro_readme_get_size(void)
{
    return TEMPLOGPRO_README_FILE_SIZE;
}

uint32_t templogpro_readme_read(uint32_t file_offset, uint8_t *destination, uint32_t requested_length)
{
    static char readme[TEMPLOGPRO_README_FILE_SIZE];
    uint32_t available;

    if (destination == 0) {
        return 0u;
    }
    readme_build(readme, sizeof(readme));
    if (file_offset >= TEMPLOGPRO_README_FILE_SIZE) {
        return 0u;
    }
    available = TEMPLOGPRO_README_FILE_SIZE - file_offset;
    if (requested_length > available) {
        requested_length = available;
    }
    memcpy(destination, &readme[file_offset], requested_length);
    return requested_length;
}

uint32_t templogpro_status_get_size(void)
{
    return TEMPLOGPRO_STATUS_FILE_SIZE;
}

uint32_t templogpro_adc_get_size(void)
{
    return TEMPLOGPRO_ADC_FILE_SIZE;
}

uint32_t templogpro_power_get_size(void)
{
    return TEMPLOGPRO_POWER_FILE_SIZE;
}

uint32_t templogpro_time_get_size(void)
{
    return TEMPLOGPRO_TIME_TEXT_SIZE;
}

uint32_t templogpro_status_read(uint32_t file_offset, uint8_t *destination, uint32_t requested_length)
{
    static char status[TEMPLOGPRO_STATUS_FILE_SIZE];
    uint32_t available;

    if (destination == 0) {
        return 0u;
    }
    templogpro_usb_files_snapshot_ensure();
    status_build(status, sizeof(status));

    if (file_offset >= TEMPLOGPRO_STATUS_FILE_SIZE) {
        return 0u;
    }
    available = TEMPLOGPRO_STATUS_FILE_SIZE - file_offset;
    if (requested_length > available) {
        requested_length = available;
    }
    memcpy(destination, &status[file_offset], requested_length);
    return requested_length;
}

uint32_t templogpro_adc_read(uint32_t file_offset, uint8_t *destination, uint32_t requested_length)
{
    static char adc[TEMPLOGPRO_ADC_FILE_SIZE];
    uint32_t available;

    if (destination == 0) {
        return 0u;
    }
    adc_build(adc, sizeof(adc));

    if (file_offset >= TEMPLOGPRO_ADC_FILE_SIZE) {
        return 0u;
    }
    available = TEMPLOGPRO_ADC_FILE_SIZE - file_offset;
    if (requested_length > available) {
        requested_length = available;
    }
    memcpy(destination, &adc[file_offset], requested_length);
    return requested_length;
}

uint32_t templogpro_power_read(uint32_t file_offset, uint8_t *destination, uint32_t requested_length)
{
    static char power[TEMPLOGPRO_POWER_FILE_SIZE];
    uint32_t available;

    if (destination == 0) {
        return 0u;
    }
    power_build(power, sizeof(power));

    if (file_offset >= TEMPLOGPRO_POWER_FILE_SIZE) {
        return 0u;
    }
    available = TEMPLOGPRO_POWER_FILE_SIZE - file_offset;
    if (requested_length > available) {
        requested_length = available;
    }
    memcpy(destination, &power[file_offset], requested_length);
    return requested_length;
}

uint32_t templogpro_time_read(uint32_t file_offset, uint8_t *destination, uint32_t requested_length)
{
    return templogpro_rtc_time_read_text(file_offset, destination, requested_length);
}

uint32_t templogpro_csv_get_size(void)
{
    templogpro_usb_files_snapshot_ensure();
    return g_snapshot.csv_size;
}

uint32_t templogpro_csv_read(uint32_t file_offset, uint8_t *destination, uint32_t requested_length)
{
    uint8_t *out = destination;
    uint32_t remaining = requested_length;
    uint32_t header_len = (uint32_t)(sizeof(CSV_HEADER) - 1u);
    uint32_t record_index;
    uint32_t record_offset;
    char line[CSV_RECORD_LINE_LEN];

    if (destination == 0) {
        return 0u;
    }

    templogpro_usb_files_snapshot_ensure();
    memset(destination, 0, requested_length);

    if (file_offset >= g_snapshot.csv_size) {
        return 0u;
    }
    if (remaining > (g_snapshot.csv_size - file_offset)) {
        remaining = g_snapshot.csv_size - file_offset;
    }
    requested_length = remaining;

    copy_range(0u, (const uint8_t *)CSV_HEADER, header_len, &file_offset, &out, &remaining);

    if (remaining == 0u) {
        return requested_length;
    }

    if (file_offset < header_len) {
        return requested_length - remaining;
    }

    record_offset = file_offset - header_len;
    record_index = record_offset / CSV_RECORD_LINE_LEN;
    record_offset %= CSV_RECORD_LINE_LEN;

    while ((remaining != 0u) && (record_index < g_snapshot.record_count)) {
        uint32_t chunk;

        csv_make_line(record_index, line);
        chunk = CSV_RECORD_LINE_LEN - record_offset;
        if (chunk > remaining) {
            chunk = remaining;
        }
        memcpy(out, &line[record_offset], chunk);
        out += chunk;
        remaining -= chunk;
        record_index++;
        record_offset = 0u;
    }

    return requested_length - remaining;
}

const char *templogpro_alarm_code_for_flags(uint16_t flags)
{
    if ((flags & TEMPLOGPRO_LOG_FLAG_SENSOR_ERR) != 0u) {
        return "S001";
    }
    if ((flags & TEMPLOGPRO_LOG_FLAG_TEMP_HIGH) != 0u) {
        return "T001";
    }
    if ((flags & TEMPLOGPRO_LOG_FLAG_TEMP_LOW) != 0u) {
        return "T002";
    }
    if ((flags & TEMPLOGPRO_LOG_FLAG_START) != 0u) {
        return "R001";
    }
    if ((flags & TEMPLOGPRO_LOG_FLAG_RESUME) != 0u) {
        return "R002";
    }
    if ((flags & TEMPLOGPRO_LOG_FLAG_PAUSE) != 0u) {
        return "R003";
    }
    if ((flags & TEMPLOGPRO_LOG_FLAG_TIME_SET) != 0u) {
        return "R004";
    }
    if ((flags & TEMPLOGPRO_LOG_FLAG_TIME_GUARD) != 0u) {
        return "R005";
    }
    if (flags == 0xFFFFu) {
        return "E001";
    }
    return "M000";
}

static uint32_t html_copy(uint32_t file_offset, uint8_t *destination, uint32_t requested_length)
{
    uint8_t *out = destination;
    char config_block[HTML_CONFIG_BLOCK_SIZE];
    uint32_t remaining = requested_length;
    uint32_t prefix_len = (uint32_t)(sizeof(HTML_PREFIX) - 1u);
    uint32_t csv_size = templogpro_csv_get_size();
    uint32_t suffix_len = (uint32_t)(sizeof(HTML_SUFFIX) - 1u);
    uint32_t config_start = prefix_len + csv_size;
    uint32_t suffix_start = config_start + HTML_CONFIG_BLOCK_SIZE;
    uint32_t total_size = suffix_start + suffix_len;

    memset(destination, 0, requested_length);
    html_config_block_build(config_block, sizeof(config_block));
    if (file_offset >= total_size) {
        return 0u;
    }
    if (remaining > (total_size - file_offset)) {
        remaining = total_size - file_offset;
    }
    requested_length = remaining;

    while (remaining != 0u) {
        uint32_t chunk;

        if (file_offset < prefix_len) {
            chunk = prefix_len - file_offset;
            if (chunk > remaining) {
                chunk = remaining;
            }
            memcpy(out, &HTML_PREFIX[file_offset], chunk);
        } else if (file_offset < config_start) {
            uint32_t csv_offset = file_offset - prefix_len;

            chunk = config_start - file_offset;
            if (chunk > remaining) {
                chunk = remaining;
            }
            chunk = templogpro_csv_read(csv_offset, out, chunk);
            if (chunk == 0u) {
                break;
            }
        } else if (file_offset < suffix_start) {
            uint32_t config_offset = file_offset - config_start;

            chunk = HTML_CONFIG_BLOCK_SIZE - config_offset;
            if (chunk > remaining) {
                chunk = remaining;
            }
            memcpy(out, &config_block[config_offset], chunk);
        } else {
            uint32_t suffix_offset = file_offset - suffix_start;

            chunk = suffix_len - suffix_offset;
            if (chunk > remaining) {
                chunk = remaining;
            }
            memcpy(out, &HTML_SUFFIX[suffix_offset], chunk);
        }

        out += chunk;
        file_offset += chunk;
        remaining -= chunk;
    }

    return requested_length - remaining;
}

static uint32_t append_i32_json(char *buf, uint32_t pos, uint32_t max, int32_t value)
{
    if (value < 0) {
        pos = append_str(buf, pos, max, "-");
        value = -value;
    }
    return append_u32(buf, pos, max, (uint32_t)value);
}

static uint32_t append_rule_json(char *buf, uint32_t pos, uint32_t max, const TempLogProDstRule *rule)
{
    pos = append_u32(buf, pos, max, rule->month);
    pos = append_str(buf, pos, max, ",");
    pos = append_u32(buf, pos, max, rule->week);
    pos = append_str(buf, pos, max, ",");
    pos = append_u32(buf, pos, max, rule->weekday);
    pos = append_str(buf, pos, max, ",");
    return append_u32(buf, pos, max, rule->minute);
}

static void html_config_block_build(char *buf, uint32_t len)
{
    TempLogProConfig config;
    TempLogProLoggerInfo info = {0};
    uint32_t pos = 0u;

    memset(buf, ' ', len);
    templogpro_config_get(&config);
    (void)templogpro_logger_get_info(&info);

    pos = append_str(buf, pos, len, "</script><script id=\"config-data\" type=\"application/json\">{\"interval\":");
    pos = append_u32(buf, pos, len, config.interval_seconds);
    pos = append_str(buf, pos, len, ",\"low\":");
    pos = append_temp_x256(buf, pos, len, config.temperature_low_x256);
    pos = append_str(buf, pos, len, ",\"high\":");
    pos = append_temp_x256(buf, pos, len, config.temperature_high_x256);
    pos = append_str(buf, pos, len, ",\"battery\":");
    pos = append_u32(buf, pos, len, config.battery_mode_enable);
    pos = append_str(buf, pos, len, ",\"usbLogging\":");
    pos = append_u32(buf, pos, len, config.usb_logging_enable);
    pos = append_str(buf, pos, len, ",\"lcd\":");
    pos = append_u32(buf, pos, len, config.lcd_mode);
    pos = append_str(buf, pos, len, ",\"lcdMenu\":");
    pos = append_u32(buf, pos, len, config.lcd_menu_mask);
    pos = append_str(buf, pos, len, ",\"records\":");
    pos = append_u32(buf, pos, len, info.record_count);
    pos = append_str(buf, pos, len, ",\"capacity\":");
    pos = append_u32(buf, pos, len, info.capacity_records);
    pos = append_str(buf, pos, len, ",\"pdf\":");
    pos = append_u32(buf, pos, len, config.pdf_enable);
    pos = append_str(buf, pos, len, ",\"led\":");
    pos = append_u32(buf, pos, len, config.led_enable);
    pos = append_str(buf, pos, len, ",\"unit\":");
    pos = append_u32(buf, pos, len, config.temperature_unit);
    pos = append_str(buf, pos, len, ",\"lcdOrder\":[");
    {
        static const uint8_t cards[TEMPLOGPRO_LCD_ORDER_COUNT + 1u] = TEMPLOGPRO_LCD_DEFAULT_CARDS;

        for (uint32_t k = 0u; k < TEMPLOGPRO_LCD_ORDER_COUNT; k++) {
            uint32_t slot = (config.lcd_order[0] != 0u) ? config.lcd_order[k] : (k + 1u);

            if (k != 0u) {
                pos = append_str(buf, pos, len, ",");
            }
            pos = append_u32(buf, pos, len, cards[slot]);
        }
    }
    pos = append_str(buf, pos, len, "]");
    pos = append_str(buf, pos, len, ",\"alarmDelay\":");
    pos = append_u32(buf, pos, len, config.alarm_delay_min);
    {
        TempLogProPowerStats power_stats;

        templogpro_power_get_stats(&power_stats);
        pos = append_str(buf, pos, len, ",\"vbatMin\":");
        /* cell voltage: VDD behind the diode + its drop */
        pos = append_u32(buf, pos, len, ((power_stats.battery_time_ms != 0u) && (power_stats.vdd_min_millivolts != 0u)) ?
                                            (power_stats.vdd_min_millivolts + TEMPLOGPRO_BATTERY_DIODE_MV) : 0u);
    }
    pos = append_str(buf, pos, len, ",\"ecoMode\":");
    pos = append_u32(buf, pos, len, config.eco_mode);
    pos = append_str(buf, pos, len, ",\"clockMode\":");
    pos = append_u32(buf, pos, len, config.clock_mode);
    pos = append_str(buf, pos, len, ",\"reset\":\"");
    pos = append_str(buf, pos, len, templogpro_diag_reset_cause());
    pos = append_str(buf, pos, len, "\",\"tzName\":\"");
    pos = append_str(buf, pos, len, config.tz_name);
    pos = append_str(buf, pos, len, "\",\"tzStd\":");
    pos = append_i32_json(buf, pos, len, config.tz_std_offset_min);
    pos = append_str(buf, pos, len, ",\"tzDst\":");
    pos = append_i32_json(buf, pos, len, config.tz_dst_delta_min);
    pos = append_str(buf, pos, len, ",\"tzStart\":[");
    pos = append_rule_json(buf, pos, len, &config.tz_dst_start);
    pos = append_str(buf, pos, len, "],\"tzEnd\":[");
    pos = append_rule_json(buf, pos, len, &config.tz_dst_end);
    pos = append_str(buf, pos, len, "]}</script><script>");
    (void)pos;
}

static void readme_build(char *buf, uint32_t len)
{
    uint32_t pos = 0u;

    memset(buf, ' ', len);
    pos = append_str(buf, pos, len, TEMPLOGPRO_PRODUCT_NAME " - temperature logger\r\n");
    pos = append_str(buf, pos, len, "Firmware: " TEMPLOGPRO_FW_VERSION "\r\n");
    pos = append_str(buf, pos, len, "Author: " TEMPLOGPRO_AUTHOR "\r\n");
    pos = append_str(buf, pos, len, "Developed in collaboration with OpenAI Codex and Anthropic Claude.\r\n\r\n");
    pos = append_str(buf, pos, len, "Files:\r\n");
    pos = append_str(buf, pos, len, "- TempLogPro.html  report, chart, data and settings (offline, Chrome/Edge)\r\n");
    pos = append_str(buf, pos, len, "- DATA.CSV         all entries, local time + UTC_Offset column\r\n");
    pos = append_str(buf, pos, len, "- POWER.TXT        live power and sleep diagnostics\r\n");
    pos = append_str(buf, pos, len, "- README.TXT       this file\r\n\r\n");
    pos = append_str(buf, pos, len, "CSV codes:\r\n");
    pos = append_str(buf, pos, len, "M000 measurement   T001/T002 above/below limit   T003 back in range\r\n");
    pos = append_str(buf, pos, len, "S001 sensor error  E001 damaged entry\r\n");
    pos = append_str(buf, pos, len, "R001 start  R002 resume  R003 pause  R004 clock set  R005 clock guard\r\n\r\n");
    pos = append_str(buf, pos, len, "Buttons:\r\n");
    pos = append_str(buf, pos, len, "Short START/STOP: previous/next LCD card.\r\n");
    pos = append_str(buf, pos, len, "Hold START 3 s: start or resume. Hold STOP 3 s: pause.\r\n");
    pos = append_str(buf, pos, len, "On the CLR card hold both buttons 3 s to clear the log.\r\n");
    pos = append_str(buf, pos, len, "Releasing a button during the countdown cancels the action.\r\n\r\n");
    pos = append_str(buf, pos, len, "Report:\r\n");
    pos = append_str(buf, pos, len, "Open TempLogPro.html from this drive; it needs no internet.\r\n");
    pos = append_str(buf, pos, len, "Generuj PDF / Generate PDF prints the short, standard or full report.\r\n");
    pos = append_str(buf, pos, len, "Settings are saved over USB (Web Serial) in Chrome or Edge.\r\n");
    (void)pos;
}

static void copy_range(uint32_t object_offset, const uint8_t *source, uint32_t source_len,
                       uint32_t *file_offset, uint8_t **destination, uint32_t *remaining)
{
    uint32_t relative;
    uint32_t chunk;

    if (*remaining == 0u) {
        return;
    }
    if (*file_offset >= (object_offset + source_len)) {
        return;
    }
    if (*file_offset < object_offset) {
        return;
    }

    relative = *file_offset - object_offset;
    chunk = source_len - relative;
    if (chunk > *remaining) {
        chunk = *remaining;
    }

    memcpy(*destination, &source[relative], chunk);
    *destination += chunk;
    *file_offset += chunk;
    *remaining -= chunk;
}

static void csv_make_line(uint32_t index, char *line)
{
    TempLogProLogRecord record;
    TempLogProDateTime dt;
    int32_t offset_min = 0;
    uint32_t offset_abs;

    memset(line, '0', CSV_RECORD_LINE_LEN);
    uint32_t have_record = ((index < g_snapshot.record_count) &&
                            (templogpro_logger_read_record(index, &record) != 0u)) ? 1u : 0u;

    if (have_record != 0u) {
        offset_min = templogpro_tz_offset_minutes(&g_snapshot.config, record.timestamp);
    }
    if ((have_record == 0u) ||
        (templogpro_rtc_timestamp_to_datetime((uint32_t)((int32_t)record.timestamp + (offset_min * 60)), &dt) == 0u)) {
        offset_min = 0;
        dt.year = 1970u;
        dt.month = 1u;
        dt.day = 1u;
        dt.hour = 0u;
        dt.minute = 0u;
        dt.second = 0u;
        record.sequence = (uint16_t)(index + 1u);
        record.temperature_x256 = 0;
        record.flags = 0xFFFFu;
    }

    put_dec(&line[0], 5u, record.sequence);
    line[5] = ',';
    put_datetime_date(&line[6], &dt);
    line[16] = ',';
    put_datetime_time(&line[17], &dt);
    line[25] = ',';
    put_temp_x256(&line[26], record.temperature_x256);
    line[34] = ',';
    put_code4(&line[35], record.flags);
    line[39] = ',';
    offset_abs = (uint32_t)((offset_min < 0) ? -offset_min : offset_min);
    line[40] = (offset_min < 0) ? '-' : '+';
    put_dec(&line[41], 2u, offset_abs / 60u);
    line[43] = ':';
    put_dec(&line[44], 2u, offset_abs % 60u);
    line[46] = '\r';
    line[47] = '\n';
}

static void put_dec(char *dst, uint32_t digits, uint32_t value)
{
    uint32_t i;

    for (i = 0u; i < digits; i++) {
        uint32_t pos = digits - 1u - i;
        dst[pos] = (char)('0' + (value % 10u));
        value /= 10u;
    }
}

static void put_code4(char *dst, uint16_t flags)
{
    const char *code = templogpro_alarm_code_for_flags(flags);

    dst[0] = code[0];
    dst[1] = code[1];
    dst[2] = code[2];
    dst[3] = code[3];
}

static void put_temp_x256(char *dst, int16_t temp_x256)
{
    int32_t value = temp_x256;
    uint32_t milli;
    uint32_t integer;
    uint32_t fraction;

    if (value < 0) {
        dst[0] = '-';
        value = -value;
    } else {
        dst[0] = '+';
    }

    milli = ((uint32_t)value * 1000u) / 256u;
    integer = milli / 1000u;
    fraction = milli % 1000u;
    if (integer > 999u) {
        integer = 999u;
    }
    put_dec(&dst[1], 3u, integer);
    dst[4] = '.';
    put_dec(&dst[5], 3u, fraction);
}

static void put_datetime_date(char *dst, const TempLogProDateTime *dt)
{
    put_dec(&dst[0], 4u, dt->year);
    dst[4] = '-';
    put_dec(&dst[5], 2u, dt->month);
    dst[7] = '-';
    put_dec(&dst[8], 2u, dt->day);
}

static void put_datetime_time(char *dst, const TempLogProDateTime *dt)
{
    put_dec(&dst[0], 2u, dt->hour);
    dst[2] = ':';
    put_dec(&dst[3], 2u, dt->minute);
    dst[5] = ':';
    put_dec(&dst[6], 2u, dt->second);
}

static void status_build(char *buf, uint32_t len)
{
    TempLogProDiagnostics diag;
    TempLogProDateTime dt;
    TempLogProConfig config;
    TempLogProLoggerInfo live_info;
    TempLogProLogStats stats;
    uint32_t pos = 0u;
    uint32_t rtc_ok;
    uint32_t config_status;
    uint16_t vdd_mv = 0u;
    uint32_t battery_percent = 0u;
    uint32_t stats_ok;
    uint32_t live_records = g_snapshot.record_count;
    uint32_t live_state = (uint32_t)g_snapshot.logger_info.logger_state;
    uint32_t live_interval = g_snapshot.logger_info.logging_interval_seconds;
    uint32_t live_active = g_snapshot.logger_info.active_seconds;
    uint32_t timestamp_high_watermark = 0u;

    memset(buf, ' ', len);

    templogpro_diag_get(&diag);
    rtc_ok = templogpro_rtc_get(&dt);
    templogpro_config_get(&config);
    if (templogpro_logger_get_info(&live_info) != 0u) {
        live_records = live_info.record_count;
        live_state = (uint32_t)live_info.logger_state;
        live_interval = live_info.logging_interval_seconds;
        live_active = live_info.active_seconds;
    }
    (void)templogpro_logger_get_timestamp_high_watermark(&timestamp_high_watermark);

    pos = append_str(buf, pos, len, "Device: " TEMPLOGPRO_PRODUCT_NAME "\r\n");
    pos = append_str(buf, pos, len, "Firmware: " TEMPLOGPRO_FW_VERSION "\r\n");
    pos = append_str(buf, pos, len, "RTC: ");
    if (rtc_ok != 0u) {
        pos = append_dt(buf, pos, len, &dt);
    } else {
        pos = append_str(buf, pos, len, "INVALID");
    }
    pos = append_str(buf, pos, len, "\r\nRTC source: ");
    pos = append_str(buf, pos, len, templogpro_rtc_source_name());
    pos = append_str(buf, pos, len, "\r\nTime write: ");
    pos = append_str(buf, pos, len, templogpro_rtc_time_status_name());
    pos = append_str(buf, pos, len, "\r\nRTC rollback guard: ");
    pos = append_str(buf, pos, len,
                     templogpro_rtc_monotonic_guard_active() ? "ACTIVE" : "IDLE");
    pos = append_str(buf, pos, len, "\r\nRTC monotonic floor: ");
    pos = append_u32(buf, pos, len, templogpro_rtc_monotonic_floor());
    pos = append_str(buf, pos, len, "\r\nLog time high-watermark: ");
    pos = append_u32(buf, pos, len, timestamp_high_watermark);
    pos = append_str(buf, pos, len, "\r\nTime file write: ");
    pos = append_str(buf, pos, len, config_write_status_name(templogpro_usb_msc_time_write_status()));
    pos = append_str(buf, pos, len, "\r\nReset source: ");
    pos = append_str(buf, pos, len, templogpro_diag_reset_flags_name(diag.reset_flags));
    pos = append_str(buf, pos, len, "\r\nReset flags: 0x");
    pos = append_hex_u32(buf, pos, len, diag.reset_flags);
    pos = append_str(buf, pos, len, "\r\nBoot stage: ");
    pos = append_u32(buf, pos, len, diag.boot_stage);
    pos = append_str(buf, pos, len, "\r\nLast boot stage: ");
    pos = append_u32(buf, pos, len, diag.last_boot_stage);
    pos = append_str(buf, pos, len, "\r\nUSB host power: ");
    pos = append_str(buf, pos, len, diag.usb_vbus_present ? "LIKELY" : "UNKNOWN_OR_BATTERY");
    pos = append_str(buf, pos, len, "\r\nState: ");
    pos = append_str(buf, pos, len, state_name(live_state));
    pos = append_str(buf, pos, len, "\r\nRecords: ");
    pos = append_u32(buf, pos, len, live_records);
    pos = append_str(buf, pos, len, "\r\nInterval: ");
    pos = append_u32(buf, pos, len, live_interval);
    pos = append_str(buf, pos, len, " s\r\nActive time: ");
    pos = append_u32(buf, pos, len, live_active);
    pos = append_str(buf, pos, len, " s\r\nConfig: ");
    config_status = templogpro_config_init();
    if (config_status == TEMPLOGPRO_CONFIG_STATUS_ERROR) {
        pos = append_str(buf, pos, len, "ERR");
    } else if (config_status == TEMPLOGPRO_CONFIG_STATUS_DEFAULTS) {
        pos = append_str(buf, pos, len, "DEFAULTS");
    } else {
        pos = append_str(buf, pos, len, "OK");
    }
    pos = append_str(buf, pos, len, "\r\nConfig write: ");
    pos = append_str(buf, pos, len, config_write_status_name(templogpro_usb_msc_config_write_status()));
    pos = append_str(buf, pos, len, "\r\nLCD mode: ");
    pos = append_u32(buf, pos, len, config.lcd_mode);
    pos = append_str(buf, pos, len, "\r\nLCD icon scan count: ");
    pos = append_u32(buf, pos, len, templogpro_hw_lcd_icon_scan_count());
    pos = append_str(buf, pos, len, "\r\nVDD mV: ");
    if (templogpro_power_read_vdd_mv(&vdd_mv) != 0u) {
        pos = append_u32(buf, pos, len, vdd_mv);
        if (vdd_mv >= 3000u) {
            battery_percent = 100u;
        } else if (vdd_mv <= 2000u) {
            battery_percent = 0u;
        } else {
            battery_percent = (((uint32_t)vdd_mv - 2000u) * 100u + 500u) / 1000u;
        }
    } else {
        pos = append_str(buf, pos, len, "ERR");
    }
    pos = append_str(buf, pos, len, "\r\nBattery percent from VDD: ");
    pos = append_u32(buf, pos, len, battery_percent);
    pos = append_str(buf, pos, len, "\r\nBattery mode: ");
    pos = append_str(buf, pos, len, config.battery_mode_enable ? "ON" : "OFF");
    pos = append_str(buf, pos, len, "\r\nLEDs: ");
    pos = append_str(buf, pos, len, config.led_enable ? "ON" : "OFF");
    pos = append_str(buf, pos, len, "\r\nSensor: ");
    pos = append_str(buf, pos, len, diag.sensor_ok ? "OK" : "ERR");
    pos = append_str(buf, pos, len, "\r\nFlash: ");
    pos = append_str(buf, pos, len, diag.flash_ok ? "OK" : "ERR");
    pos = append_str(buf, pos, len, "\r\nUSB: ");
    pos = append_str(buf, pos, len, diag.usb_ok ? "OK" : "ERR");
    pos = append_str(buf, pos, len, "\r\nLast error: ");
    pos = append_str(buf, pos, len, templogpro_diag_error_name(diag.last_error));
    stats_ok = templogpro_logger_get_stats(&stats);
    pos = append_str(buf, pos, len, "\r\nSamples: ");
    if (stats_ok != 0u) {
        pos = append_u32(buf, pos, len, stats.sample_count);
        pos = append_str(buf, pos, len, "\r\nTemp min C: ");
        if (stats.sample_count != 0u) {
            pos = append_temp_x256(buf, pos, len, stats.min_temperature_x256);
            pos = append_str(buf, pos, len, "\r\nTemp max C: ");
            pos = append_temp_x256(buf, pos, len, stats.max_temperature_x256);
            pos = append_str(buf, pos, len, "\r\nTemp avg C: ");
            pos = append_temp_x256(buf, pos, len, stats.avg_temperature_x256);
        } else {
            pos = append_str(buf, pos, len, "NONE\r\nTemp max C: NONE\r\nTemp avg C: NONE");
        }
        pos = append_str(buf, pos, len, "\r\nActive duration s: ");
        pos = append_u32(buf, pos, len, stats.active_seconds);
        pos = append_str(buf, pos, len, "\r\nTotal span s: ");
        pos = append_u32(buf, pos, len, stats.total_seconds);
        pos = append_str(buf, pos, len, "\r\nLog duration s: ");
        pos = append_u32(buf, pos, len, stats.duration_seconds);
    } else {
        pos = append_str(buf, pos, len, "ERR");
    }
    pos = append_str(buf, pos, len, "\r\n");

    (void)pos;
}

static void adc_build(char *buf, uint32_t len)
{
    TempLogProAdcScan scan;
    uint32_t pos = 0u;

    memset(buf, ' ', len);

    pos = append_str(buf, pos, len, "# " TEMPLOGPRO_PRODUCT_NAME " ADC.TXT\r\n");
    pos = append_str(buf, pos, len, "# Short diagnostic for battery-divider search.\r\n");
    pos = append_str(buf, pos, len, "# GPIO mode is restored after each sample.\r\n");

    if (templogpro_hw_adc_scan(&scan) == 0u) {
        pos = append_str(buf, pos, len, "ADC=ERR\r\n");
        (void)pos;
        return;
    }

    pos = append_str(buf, pos, len, "ADC=OK\r\nVREFINT_RAW=");
    pos = append_u32(buf, pos, len, scan.vrefint_raw);
    pos = append_str(buf, pos, len, "\r\nVDD_MV=");
    pos = append_u32(buf, pos, len, scan.vdd_millivolts);
    pos = append_str(buf, pos, len, "\r\n\r\nPIN,CH,RAW,MV\r\n");

    for (uint32_t i = 0u; i < scan.item_count; i++) {
        pos = append_pin(buf, pos, len, scan.items[i].pin);
        pos = append_str(buf, pos, len, ",");
        pos = append_u32(buf, pos, len, scan.items[i].channel);
        pos = append_str(buf, pos, len, ",");
        pos = append_u32(buf, pos, len, scan.items[i].raw);
        pos = append_str(buf, pos, len, ",");
        pos = append_u32(buf, pos, len, scan.items[i].millivolts);
        pos = append_str(buf, pos, len, "\r\n");
    }

    pos = append_str(buf, pos, len, "\r\n# Candidate battery pin: stable MV between 0 and VDD.\r\n");
    (void)pos;
}

static void power_build(char *buf, uint32_t len)
{
    TempLogProPowerStats stats;
    uint16_t vdd_mv = 0u;
    uint32_t duty_permille = 0u;
    uint32_t pos = 0u;

    memset(buf, ' ', len);
    (void)templogpro_power_read_vdd_mv(&vdd_mv);
    templogpro_power_get_stats(&stats);
    if (stats.uptime_ms != 0u) {
        uint64_t duty = ((uint64_t)stats.active_cpu_ms * 1000u) / stats.uptime_ms;
        duty_permille = (duty > 1000u) ? 1000u : (uint32_t)duty;
    }

    pos = append_str(buf, pos, len, "# " TEMPLOGPRO_PRODUCT_NAME " POWER.TXT\r\n");
    pos = append_str(buf, pos, len, "# Firmware: " TEMPLOGPRO_FW_VERSION "\r\n");
    pos = append_str(buf, pos, len, "# Software telemetry, not a direct current measurement.\r\n\r\n");
    pos = append_str(buf, pos, len, "SOURCE=");
    pos = append_str(buf, pos, len, stats.usb_present ? "USB" : "BATTERY");
    pos = append_str(buf, pos, len, "\r\nRTC_SOURCE=");
    pos = append_str(buf, pos, len, templogpro_rtc_source_name());
    pos = append_str(buf, pos, len, "\r\nLAST_RESET=");
    pos = append_str(buf, pos, len, templogpro_diag_reset_cause());
    pos = append_str(buf, pos, len, "\r\nVDD_MV=");
    if (vdd_mv != 0u) {
        pos = append_u32(buf, pos, len, vdd_mv);
    } else {
        pos = append_str(buf, pos, len, "ERR");
    }
    pos = append_str(buf, pos, len, "\r\nVDD_MIN_MV=");
    pos = append_u32(buf, pos, len, stats.vdd_min_millivolts);
    pos = append_str(buf, pos, len, "\r\nVDD_MAX_MV=");
    pos = append_u32(buf, pos, len, stats.vdd_max_millivolts);
    pos = append_str(buf, pos, len, "\r\nVDD_SAMPLES=");
    pos = append_u32(buf, pos, len, stats.vdd_samples);
    pos = append_str(buf, pos, len, "\r\nCLOCK_HZ=");
    pos = append_u32(buf, pos, len, stats.system_clock_hz);
    pos = append_str(buf, pos, len, "\r\nUPTIME_MS=");
    pos = append_u32(buf, pos, len, stats.uptime_ms);
    pos = append_str(buf, pos, len, "\r\nCPU_ACTIVE_MS_EST=");
    pos = append_u32(buf, pos, len, stats.active_cpu_ms);
    pos = append_str(buf, pos, len, "\r\nCPU_DUTY_PERMILLE_EST=");
    pos = append_u32(buf, pos, len, duty_permille);
    pos = append_str(buf, pos, len, "\r\nIDLE_SLEEP_MS=");
    pos = append_u32(buf, pos, len, stats.idle_sleep_ms);
    pos = append_str(buf, pos, len, "\r\nSTOP_SLEEP_MS=");
    pos = append_u32(buf, pos, len, stats.stop_sleep_ms);
    pos = append_str(buf, pos, len, "\r\nUSB_TIME_MS=");
    pos = append_u32(buf, pos, len, stats.usb_time_ms);
    pos = append_str(buf, pos, len, "\r\nBATTERY_TIME_MS=");
    pos = append_u32(buf, pos, len, stats.battery_time_ms);
    pos = append_str(buf, pos, len, "\r\nIDLE_ENTRIES=");
    pos = append_u32(buf, pos, len, stats.idle_entries);
    pos = append_str(buf, pos, len, "\r\nSTOP_ENTRIES=");
    pos = append_u32(buf, pos, len, stats.stop_entries);
    pos = append_str(buf, pos, len, "\r\nRTC_WAKEUPS=");
    pos = append_u32(buf, pos, len, stats.rtc_wakeups);
    pos = append_str(buf, pos, len, "\r\nBUTTON_WAKEUPS=");
    pos = append_u32(buf, pos, len, stats.button_wakeups);
    pos = append_str(buf, pos, len, "\r\nSTOP_DEFERRED=");
    pos = append_u32(buf, pos, len, stats.stop_deferred);
    pos = append_str(buf, pos, len, "\r\nCYCLE_COUNTER=");
    pos = append_str(buf, pos, len, stats.cycle_counter_ok ? "OK" : "UNAVAILABLE");
    pos = append_str(buf, pos, len,
                     "\r\n\r\n# VDD is the MCU supply. It is not the CR3450 terminal voltage while USB powers the board.\r\n");
    pos = append_str(buf, pos, len,
                     "# CPU duty does not include LCD, RTC, sensor, SPI flash or USB peripheral current.\r\n");
    (void)pos;
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

static uint32_t append_hex_u32(char *buf, uint32_t pos, uint32_t max, uint32_t value)
{
    static const char hex[] = "0123456789ABCDEF";

    for (uint32_t i = 0u; i < 8u; i++) {
        uint32_t shift = 28u - (i * 4u);
        if (pos < max) {
            buf[pos++] = hex[(value >> shift) & 0x0Fu];
        }
    }
    return pos;
}

static uint32_t append_temp_x256(char *buf, uint32_t pos, uint32_t max, int32_t temp_x256)
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
    if (pos < max) {
        buf[pos++] = '.';
    }
    if (fraction < 100u) {
        pos = append_str(buf, pos, max, "0");
    }
    if (fraction < 10u) {
        pos = append_str(buf, pos, max, "0");
    }
    return append_u32(buf, pos, max, fraction);
}

static uint32_t append_dt(char *buf, uint32_t pos, uint32_t max, const TempLogProDateTime *dt)
{
    char tmp[19];

    put_datetime_date(&tmp[0], dt);
    tmp[10] = ' ';
    put_datetime_time(&tmp[11], dt);
    if ((pos + sizeof(tmp)) <= max) {
        memcpy(&buf[pos], tmp, sizeof(tmp));
        pos += sizeof(tmp);
    }
    return pos;
}

static uint32_t append_pin(char *buf, uint32_t pos, uint32_t max, const char pin[4])
{
    for (uint32_t i = 0u; i < 4u; i++) {
        if (pin[i] == '\0') {
            break;
        }
        if (pos < max) {
            buf[pos++] = pin[i];
        }
    }
    return pos;
}

static const char *state_name(uint32_t state)
{
    switch (state) {
    case TEMPLOGPRO_LOGGER_IDLE: return "IDLE";
    case TEMPLOGPRO_LOGGER_LOGGING: return "LOGGING";
    case TEMPLOGPRO_LOGGER_STOPPED: return "STOPPED";
    case TEMPLOGPRO_LOGGER_ERROR: return "ERROR";
    default: return "UNKNOWN";
    }
}

static const char *config_write_status_name(uint32_t status)
{
    switch (status) {
    case 0u: return "IDLE";
    case 1u: return "PENDING";
    case 2u: return "OK";
    case 3u: return "ERR";
    default: return "UNKNOWN";
    }
}

