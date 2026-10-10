#ifndef TEMPLOGPRO_BUTTONS_H
#define TEMPLOGPRO_BUTTONS_H

#include <stdint.h>

typedef enum
{
    BUTTON_NONE = 0,
    BUTTON_START_SHORT,
    BUTTON_STOP_SHORT,
    BUTTON_START_LONG_CONFIRMED,
    BUTTON_STOP_LONG_CONFIRMED,
    BUTTON_BOTH_LONG_CONFIRMED,
    BUTTON_PRESS_CANCELLED
} TempLogProButtonEvent;

typedef struct
{
    uint32_t active;
    uint32_t stable_mask;
    TempLogProButtonEvent pending_long_event;
    uint32_t elapsed_ms;
    uint32_t required_ms;
} TempLogProButtonHold;

void templogpro_buttons_init(void);
TempLogProButtonEvent templogpro_buttons_poll(uint32_t now_ms);
void templogpro_buttons_get_hold(uint32_t now_ms, TempLogProButtonHold *hold);
uint32_t templogpro_buttons_current_mask(void);
uint32_t templogpro_buttons_take_wakeup(void);
const char *templogpro_buttons_event_name(TempLogProButtonEvent event);

#ifdef TEMPLOGPRO_BUTTONS_TEST
void templogpro_buttons_test_set_raw_mask(uint32_t mask);
#endif

#endif
