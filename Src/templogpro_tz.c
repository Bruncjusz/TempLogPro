#include "templogpro_tz.h"

static uint32_t tz_is_leap(uint32_t year)
{
    return (((year % 4u) == 0u) && (((year % 100u) != 0u) || ((year % 400u) == 0u))) ? 1u : 0u;
}

static uint32_t tz_days_in_month(uint32_t year, uint32_t month)
{
    static const uint8_t days[12] = {31u, 28u, 31u, 30u, 31u, 30u, 31u, 31u, 30u, 31u, 30u, 31u};

    return ((month == 2u) && (tz_is_leap(year) != 0u)) ? 29u : days[month - 1u];
}

/* Days since 1970-01-01 (the RTC timestamp epoch). */
static uint32_t tz_days_from_civil(uint32_t year, uint32_t month, uint32_t day)
{
    uint32_t days = 0u;
    uint32_t y;
    uint32_t m;

    for (y = 1970u; y < year; y++) {
        days += (tz_is_leap(y) != 0u) ? 366u : 365u;
    }
    for (m = 1u; m < month; m++) {
        days += tz_days_in_month(year, m);
    }
    return days + day - 1u;
}

static uint32_t tz_year_of(uint32_t timestamp)
{
    uint32_t days = timestamp / 86400u;
    uint32_t year = 1970u;

    for (;;) {
        uint32_t len = (tz_is_leap(year) != 0u) ? 366u : 365u;
        if (days < len) {
            return year;
        }
        days -= len;
        year++;
    }
}

static uint32_t tz_rule_valid(const TempLogProDstRule *rule)
{
    return ((rule->month >= 1u) && (rule->month <= 12u) &&
            (rule->week >= 1u) && (rule->week <= 5u) &&
            (rule->weekday <= 6u) && (rule->minute < 1440u)) ? 1u : 0u;
}

uint32_t templogpro_tz_rule_utc(const TempLogProDstRule *rule, uint32_t year, int32_t offset_before_min)
{
    uint32_t first;
    uint32_t first_wday;
    uint32_t day;
    uint32_t dim;
    int32_t local;

    if ((tz_rule_valid(rule) == 0u) || (year < 1971u)) {
        return 0u;
    }
    dim = tz_days_in_month(year, rule->month);
    first = tz_days_from_civil(year, rule->month, 1u);
    first_wday = (first + 4u) % 7u;  /* 1970-01-01 was a Thursday (4) */
    day = 1u + ((rule->weekday + 7u - first_wday) % 7u) + ((uint32_t)(rule->week - 1u) * 7u);
    while (day > dim) {
        day -= 7u;  /* week 5 = last occurrence in the month */
    }
    local = (int32_t)((first + day - 1u) * 86400u) + (int32_t)rule->minute * 60;
    return (uint32_t)(local - (offset_before_min * 60));
}

uint32_t templogpro_tz_dst_active(const TempLogProConfig *config, uint32_t utc_timestamp)
{
    int32_t std = config->tz_std_offset_min;
    int32_t dst = std + config->tz_dst_delta_min;
    uint32_t year;
    uint32_t start;
    uint32_t end;

    if ((config->tz_dst_delta_min == 0) ||
        (tz_rule_valid(&config->tz_dst_start) == 0u) ||
        (tz_rule_valid(&config->tz_dst_end) == 0u)) {
        return 0u;
    }
    year = tz_year_of((uint32_t)((int32_t)utc_timestamp + std * 60));
    start = templogpro_tz_rule_utc(&config->tz_dst_start, year, std);
    end = templogpro_tz_rule_utc(&config->tz_dst_end, year, dst);
    if (start < end) {
        /* Northern hemisphere: summer time inside one calendar year. */
        return ((utc_timestamp >= start) && (utc_timestamp < end)) ? 1u : 0u;
    }
    /* Southern hemisphere: summer time spans the new year. */
    return ((utc_timestamp >= start) || (utc_timestamp < end)) ? 1u : 0u;
}

int32_t templogpro_tz_offset_minutes(const TempLogProConfig *config, uint32_t utc_timestamp)
{
    return config->tz_std_offset_min +
           ((templogpro_tz_dst_active(config, utc_timestamp) != 0u) ? config->tz_dst_delta_min : 0);
}
