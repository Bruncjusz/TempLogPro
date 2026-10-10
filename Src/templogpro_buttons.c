#include "templogpro_buttons.h"

#ifndef TEMPLOGPRO_BUTTONS_TEST
#include "stm32l1xx_hal.h"
#endif

#define REG32(a)               (*(volatile uint32_t *)(a))
#define RCC_AHBENR             REG32(0x4002381Cu)
#define BUTTON_GPIOA_BASE      0x40020000u
#define GPIO_MODER(b)          REG32((b) + 0x00u)
#define GPIO_PUPDR(b)          REG32((b) + 0x0Cu)
#define GPIO_IDR(b)            REG32((b) + 0x10u)

#define BUTTON_START_PIN       0u
#define BUTTON_STOP_PIN        1u
#define BUTTON_MASK_START      0x01u
#define BUTTON_MASK_STOP       0x02u
#define BUTTON_MASK_BOTH       (BUTTON_MASK_START | BUTTON_MASK_STOP)

#define BUTTON_DEBOUNCE_MS     50u
#define BUTTON_SHORT_MAX_MS    700u
#define BUTTON_LONG_MS         3000u

typedef enum
{
    BUTTON_FSM_IDLE = 0,
    BUTTON_FSM_TRACK_PRESS,
    BUTTON_FSM_WAIT_FULL_RELEASE
} ButtonFsmState;

typedef struct
{
    uint32_t raw_mask;
    uint32_t candidate_mask;
    uint32_t candidate_since_ms;
    uint32_t stable_mask;
    uint32_t press_mask;
    uint32_t combined_mask;
    uint32_t press_start_ms;
    uint32_t both_start_ms;
    ButtonFsmState state;
} ButtonDriver;

static ButtonDriver g_buttons;
static volatile uint32_t g_button_wakeup;

#ifdef TEMPLOGPRO_BUTTONS_TEST
static uint32_t g_test_raw_mask;
#endif

static void buttons_gpio_init(void);
static uint32_t buttons_read_raw_mask(void);
static uint32_t buttons_update_stable_mask(uint32_t now_ms);
static TempLogProButtonEvent buttons_short_event(uint32_t mask);
static TempLogProButtonEvent buttons_long_event(uint32_t mask);
static void buttons_reset_press_tracking(void);

static void buttons_gpio_init(void)
{
#ifndef TEMPLOGPRO_BUTTONS_TEST
    RCC_AHBENR |= (1u << 0); /* GPIOA */

    GPIO_MODER(BUTTON_GPIOA_BASE) &= ~(3u << (BUTTON_START_PIN * 2u));
    GPIO_PUPDR(BUTTON_GPIOA_BASE) &= ~(3u << (BUTTON_START_PIN * 2u));
    GPIO_PUPDR(BUTTON_GPIOA_BASE) |=  (1u << (BUTTON_START_PIN * 2u));

    GPIO_MODER(BUTTON_GPIOA_BASE) &= ~(3u << (BUTTON_STOP_PIN * 2u));
    GPIO_PUPDR(BUTTON_GPIOA_BASE) &= ~(3u << (BUTTON_STOP_PIN * 2u));
    GPIO_PUPDR(BUTTON_GPIOA_BASE) |=  (1u << (BUTTON_STOP_PIN * 2u));

    __HAL_RCC_SYSCFG_CLK_ENABLE();
    SYSCFG->EXTICR[0] &= ~(SYSCFG_EXTICR1_EXTI0 | SYSCFG_EXTICR1_EXTI1);
    EXTI->IMR |= EXTI_IMR_MR0 | EXTI_IMR_MR1;
    EXTI->RTSR |= EXTI_RTSR_TR0 | EXTI_RTSR_TR1;
    EXTI->FTSR |= EXTI_FTSR_TR0 | EXTI_FTSR_TR1;
    EXTI->PR = EXTI_PR_PR0 | EXTI_PR_PR1;
    HAL_NVIC_SetPriority(EXTI0_IRQn, 3u, 0u);
    HAL_NVIC_SetPriority(EXTI1_IRQn, 3u, 0u);
    HAL_NVIC_EnableIRQ(EXTI0_IRQn);
    HAL_NVIC_EnableIRQ(EXTI1_IRQn);
    __HAL_RCC_SYSCFG_CLK_DISABLE();
#endif
}

static uint32_t buttons_read_raw_mask(void)
{
#ifdef TEMPLOGPRO_BUTTONS_TEST
    return g_test_raw_mask & BUTTON_MASK_BOTH;
#else
    uint32_t mask = 0u;

    if ((((GPIO_IDR(BUTTON_GPIOA_BASE) >> BUTTON_START_PIN) & 1u) == 0u)) {
        mask |= BUTTON_MASK_START;
    }
    if ((((GPIO_IDR(BUTTON_GPIOA_BASE) >> BUTTON_STOP_PIN) & 1u) == 0u)) {
        mask |= BUTTON_MASK_STOP;
    }

    return mask;
#endif
}

static uint32_t buttons_update_stable_mask(uint32_t now_ms)
{
    uint32_t raw_mask = buttons_read_raw_mask();
    uint32_t changed = 0u;

    g_buttons.raw_mask = raw_mask;
    if (raw_mask != g_buttons.candidate_mask) {
        g_buttons.candidate_mask = raw_mask;
        g_buttons.candidate_since_ms = now_ms;
        return 0u;
    }

    if ((g_buttons.stable_mask != g_buttons.candidate_mask) &&
        ((now_ms - g_buttons.candidate_since_ms) >= BUTTON_DEBOUNCE_MS)) {
        g_buttons.stable_mask = g_buttons.candidate_mask;
        changed = 1u;
    }

    return changed;
}

static TempLogProButtonEvent buttons_short_event(uint32_t mask)
{
    if (mask == BUTTON_MASK_START) {
        return BUTTON_START_SHORT;
    }
    if (mask == BUTTON_MASK_STOP) {
        return BUTTON_STOP_SHORT;
    }
    return BUTTON_NONE;
}

static TempLogProButtonEvent buttons_long_event(uint32_t mask)
{
    if (mask == BUTTON_MASK_START) {
        return BUTTON_START_LONG_CONFIRMED;
    }
    if (mask == BUTTON_MASK_STOP) {
        return BUTTON_STOP_LONG_CONFIRMED;
    }
    if (mask == BUTTON_MASK_BOTH) {
        return BUTTON_BOTH_LONG_CONFIRMED;
    }
    return BUTTON_NONE;
}

static void buttons_reset_press_tracking(void)
{
    g_buttons.press_mask = 0u;
    g_buttons.combined_mask = 0u;
    g_buttons.press_start_ms = 0u;
    g_buttons.both_start_ms = 0u;
}

void templogpro_buttons_init(void)
{
    buttons_gpio_init();
    g_button_wakeup = 0u;

    g_buttons.raw_mask = buttons_read_raw_mask();
    g_buttons.candidate_mask = g_buttons.raw_mask;
    g_buttons.candidate_since_ms = 0u;
    g_buttons.stable_mask = g_buttons.raw_mask;
    buttons_reset_press_tracking();
    g_buttons.state = (g_buttons.stable_mask == 0u) ? BUTTON_FSM_IDLE : BUTTON_FSM_WAIT_FULL_RELEASE;
}

TempLogProButtonEvent templogpro_buttons_poll(uint32_t now_ms)
{
    TempLogProButtonEvent event = BUTTON_NONE;

    (void)buttons_update_stable_mask(now_ms);

    if (g_buttons.state == BUTTON_FSM_WAIT_FULL_RELEASE) {
        if (g_buttons.stable_mask == 0u) {
            buttons_reset_press_tracking();
            g_buttons.state = BUTTON_FSM_IDLE;
        }
        return BUTTON_NONE;
    }

    if (g_buttons.state == BUTTON_FSM_IDLE) {
        if (g_buttons.stable_mask != 0u) {
            g_buttons.press_mask = g_buttons.stable_mask;
            g_buttons.combined_mask = g_buttons.stable_mask;
            g_buttons.press_start_ms = now_ms;
            g_buttons.both_start_ms = (g_buttons.stable_mask == BUTTON_MASK_BOTH) ? now_ms : 0u;
            g_buttons.state = BUTTON_FSM_TRACK_PRESS;
        }
        return BUTTON_NONE;
    }

    if (g_buttons.stable_mask != 0u) {
        g_buttons.combined_mask |= g_buttons.stable_mask;
        if ((g_buttons.stable_mask == BUTTON_MASK_BOTH) && (g_buttons.both_start_ms == 0u)) {
            g_buttons.both_start_ms = now_ms;
        }

        if ((g_buttons.stable_mask == BUTTON_MASK_BOTH) &&
            (g_buttons.both_start_ms != 0u) &&
            ((now_ms - g_buttons.both_start_ms) >= BUTTON_LONG_MS)) {
            g_buttons.state = BUTTON_FSM_WAIT_FULL_RELEASE;
            return BUTTON_BOTH_LONG_CONFIRMED;
        }

        if ((g_buttons.combined_mask == g_buttons.press_mask) &&
            (g_buttons.stable_mask == g_buttons.press_mask) &&
            ((now_ms - g_buttons.press_start_ms) >= BUTTON_LONG_MS)) {
            event = buttons_long_event(g_buttons.press_mask);
            g_buttons.state = BUTTON_FSM_WAIT_FULL_RELEASE;
            return event;
        }

        return BUTTON_NONE;
    }

    if (((now_ms - g_buttons.press_start_ms) <= BUTTON_SHORT_MAX_MS) &&
        (g_buttons.combined_mask == g_buttons.press_mask)) {
        event = buttons_short_event(g_buttons.press_mask);
    } else {
        event = BUTTON_PRESS_CANCELLED;
    }

    g_buttons.state = BUTTON_FSM_WAIT_FULL_RELEASE;
    return event;
}

void templogpro_buttons_get_hold(uint32_t now_ms, TempLogProButtonHold *hold)
{
    uint32_t start_ms = 0u;
    TempLogProButtonEvent pending_event = BUTTON_NONE;

    if (hold == 0) {
        return;
    }

    hold->active = 0u;
    hold->stable_mask = g_buttons.stable_mask;
    hold->pending_long_event = BUTTON_NONE;
    hold->elapsed_ms = 0u;
    hold->required_ms = BUTTON_LONG_MS;

    if ((g_buttons.state != BUTTON_FSM_TRACK_PRESS) || (g_buttons.stable_mask == 0u)) {
        return;
    }

    if ((g_buttons.stable_mask == BUTTON_MASK_BOTH) && (g_buttons.both_start_ms != 0u)) {
        start_ms = g_buttons.both_start_ms;
        pending_event = BUTTON_BOTH_LONG_CONFIRMED;
    } else if ((g_buttons.combined_mask == g_buttons.press_mask) &&
               (g_buttons.stable_mask == g_buttons.press_mask)) {
        start_ms = g_buttons.press_start_ms;
        pending_event = buttons_long_event(g_buttons.press_mask);
    }

    if ((start_ms == 0u) || (pending_event == BUTTON_NONE)) {
        return;
    }

    hold->active = 1u;
    hold->pending_long_event = pending_event;
    hold->elapsed_ms = now_ms - start_ms;
}

uint32_t templogpro_buttons_current_mask(void)
{
    return g_buttons.stable_mask;
}

uint32_t templogpro_buttons_take_wakeup(void)
{
    uint32_t pending;
#ifndef TEMPLOGPRO_BUTTONS_TEST
    uint32_t primask = __get_PRIMASK();

    __disable_irq();
#endif
    pending = g_button_wakeup;
    g_button_wakeup = 0u;
#ifndef TEMPLOGPRO_BUTTONS_TEST
    if (primask == 0u) {
        __enable_irq();
    }
#endif
    return pending;
}

#ifndef TEMPLOGPRO_BUTTONS_TEST
void EXTI0_IRQHandler(void)
{
    if ((EXTI->PR & EXTI_PR_PR0) != 0u) {
        EXTI->PR = EXTI_PR_PR0;
        g_button_wakeup = 1u;
    }
}

void EXTI1_IRQHandler(void)
{
    if ((EXTI->PR & EXTI_PR_PR1) != 0u) {
        EXTI->PR = EXTI_PR_PR1;
        g_button_wakeup = 1u;
    }
}
#endif

const char *templogpro_buttons_event_name(TempLogProButtonEvent event)
{
    switch (event) {
    case BUTTON_START_SHORT: return "START_SHORT";
    case BUTTON_STOP_SHORT: return "STOP_SHORT";
    case BUTTON_START_LONG_CONFIRMED: return "START_LONG_CONFIRMED";
    case BUTTON_STOP_LONG_CONFIRMED: return "STOP_LONG_CONFIRMED";
    case BUTTON_BOTH_LONG_CONFIRMED: return "BOTH_LONG_CONFIRMED";
    case BUTTON_PRESS_CANCELLED: return "PRESS_CANCELLED";
    case BUTTON_NONE:
    default: return "NONE";
    }
}

#ifdef TEMPLOGPRO_BUTTONS_TEST
void templogpro_buttons_test_set_raw_mask(uint32_t mask)
{
    g_test_raw_mask = mask & BUTTON_MASK_BOTH;
}
#endif
