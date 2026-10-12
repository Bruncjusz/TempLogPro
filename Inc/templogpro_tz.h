#ifndef TEMPLOGPRO_TZ_H
#define TEMPLOGPRO_TZ_H

#include <stdint.h>
#include "templogpro_config.h"

/*
 * Time zone support. The RTC and every stored record run in UTC, so the log
 * never goes backwards; local time is derived only for display (DATA.CSV,
 * report). A zone is a standard UTC offset plus an optional daylight-saving
 * shift with yearly start/end rules ("week w of month m, weekday d, at minute
 * t"), like the POSIX TZ "Mm.w.d/time" form: the start time is local standard
 * time, the end time local daylight time.
 */

/* UTC offset in minutes (e.g. +120 for CEST) in effect at the UTC timestamp. */
int32_t templogpro_tz_offset_minutes(const TempLogProConfig *config, uint32_t utc_timestamp);

/* 1 if daylight saving is in effect at the UTC timestamp. */
uint32_t templogpro_tz_dst_active(const TempLogProConfig *config, uint32_t utc_timestamp);

/* UTC timestamp of a rule's transition in the given year (0 if invalid). */
uint32_t templogpro_tz_rule_utc(const TempLogProDstRule *rule, uint32_t year, int32_t offset_before_min);

#endif
