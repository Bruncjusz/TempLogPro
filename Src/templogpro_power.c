#include "templogpro_power.h"
#include "templogpro_app.h"
#include "templogpro_buttons.h"
#include "templogpro_diag.h"
#include "templogpro_hw_bridge.h"
#include "templogpro_usb.h"
#include "stm32l1xx_hal.h"

#define TEMPLOGPRO_STOP_WAKE_MS          1000u
#define TEMPLOGPRO_BUTTON_WAKE_GUARD_MS   250u
#define TEMPLOGPRO_STOP_WAKE_MAX_S         60u
#define TEMPLOGPRO_RTC_WUT_TIMEOUT     100000u
#define TEMPLOGPRO_VDD_CACHE_MS           5000u
#define TEMPLOGPRO_VOS_TIMEOUT          100000u

extern __IO uint32_t uwTick;
extern uint32_t uwTickPrio;

static volatile uint32_t g_rtc_wakeup;
static uint32_t g_stop_count;
static uint32_t g_stop_inhibit_until_ms;
static uint32_t g_stop_deferred;
static uint32_t g_rtc_wakeups;
static uint32_t g_button_wakeups;
static uint32_t g_idle_entries;
static uint32_t g_idle_sleep_ms;
static uint32_t g_stop_sleep_ms;
static uint32_t g_usb_time_ms;
static uint32_t g_battery_time_ms;
static uint32_t g_account_last_ms;
static uint32_t g_account_usb;
static uint32_t g_cycle_counter_ok;
static uint32_t g_work_active;
static uint32_t g_work_start_cycle;
static uint32_t g_work_start_clock_hz;
static uint64_t g_active_cpu_us;
static uint32_t g_vdd_valid;
static uint32_t g_vdd_last_ms;
static uint32_t g_vdd_samples;
static uint16_t g_vdd_millivolts;
static uint16_t g_vdd_min_millivolts;
static uint16_t g_vdd_max_millivolts;

static void power_rtc_unlock(void);
static void power_rtc_lock(void);
static void power_rtc_clear_wakeup_flag(void);
static uint32_t power_rtc_wakeup_start(uint32_t seconds);
static void power_rtc_wakeup_stop(void);
static void power_account_elapsed(void);
static void power_cycle_counter_set(uint32_t enable);
static uint32_t power_add_u32(uint32_t value, uint32_t increment);

uint32_t templogpro_power_init(void)
{
    TempLogProDiagnostics diag;

    __HAL_RCC_PWR_CLK_ENABLE();

    EXTI->IMR |= EXTI_IMR_MR20;
    EXTI->RTSR |= EXTI_RTSR_TR20;
    EXTI->FTSR &= ~EXTI_FTSR_TR20;
    EXTI->PR = EXTI_PR_PR20;

    HAL_NVIC_SetPriority(RTC_WKUP_IRQn, 3u, 0u);
    HAL_NVIC_EnableIRQ(RTC_WKUP_IRQn);

    g_rtc_wakeup = 0u;
    g_stop_count = 0u;
    g_stop_deferred = 0u;
    g_rtc_wakeups = 0u;
    g_button_wakeups = 0u;
    g_idle_entries = 0u;
    g_idle_sleep_ms = 0u;
    g_stop_sleep_ms = 0u;
    g_usb_time_ms = 0u;
    g_battery_time_ms = 0u;
    g_active_cpu_us = 0u;
    g_work_active = 0u;
    g_vdd_valid = 0u;
    g_vdd_last_ms = 0u;
    g_vdd_samples = 0u;
    g_vdd_millivolts = 0u;
    g_vdd_min_millivolts = 0u;
    g_vdd_max_millivolts = 0u;

    templogpro_diag_get(&diag);
    g_account_last_ms = HAL_GetTick();
    g_account_usb = diag.usb_vbus_present ? 1u : 0u;

    power_cycle_counter_set(g_account_usb);

    g_stop_inhibit_until_ms = HAL_GetTick() + TEMPLOGPRO_BUTTON_WAKE_GUARD_MS;
    return 1u;
}

uint32_t templogpro_power_battery_clock_config(void)
{
    RCC_OscInitTypeDef osc = {0u};
    RCC_ClkInitTypeDef clk = {0u};
    uint32_t timeout;

    osc.OscillatorType = RCC_OSCILLATORTYPE_MSI;
    osc.MSIState = RCC_MSI_ON;
    osc.MSIClockRange = RCC_MSIRANGE_5;
    osc.MSICalibrationValue = RCC_MSICALIBRATION_DEFAULT;
    osc.PLL.PLLState = RCC_PLL_NONE;
    if (HAL_RCC_OscConfig(&osc) != HAL_OK) {
        return 0u;
    }

    clk.ClockType = RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_HCLK |
                    RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    clk.SYSCLKSource = RCC_SYSCLKSOURCE_MSI;
    clk.AHBCLKDivider = RCC_SYSCLK_DIV1;
    clk.APB1CLKDivider = RCC_HCLK_DIV1;
    clk.APB2CLKDivider = RCC_HCLK_DIV1;
    if (HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_0) != HAL_OK) {
        return 0u;
    }

    osc = (RCC_OscInitTypeDef){0u};
    osc.OscillatorType = RCC_OSCILLATORTYPE_HSE | RCC_OSCILLATORTYPE_HSI;
    osc.HSEState = RCC_HSE_OFF;
    osc.HSIState = RCC_HSI_OFF;
    osc.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
    osc.PLL.PLLState = RCC_PLL_OFF;
    if (HAL_RCC_OscConfig(&osc) != HAL_OK) {
        return 0u;
    }

    __HAL_RCC_PWR_CLK_ENABLE();
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE3);
    timeout = TEMPLOGPRO_VOS_TIMEOUT;
    while ((__HAL_PWR_GET_FLAG(PWR_FLAG_VOS) != RESET) && (timeout != 0u)) {
        timeout--;
    }
    if (__HAL_PWR_GET_FLAG(PWR_FLAG_VOS) != RESET) {
        return 0u;
    }

    if (HAL_SetTickFreq(HAL_TICK_FREQ_100HZ) != HAL_OK) {
        return 0u;
    }

    return 1u;
}

void templogpro_power_process(void)
{
    power_account_elapsed();
}

void templogpro_power_work_begin(void)
{
    TempLogProDiagnostics diag;

    templogpro_diag_get(&diag);
    power_cycle_counter_set(diag.usb_vbus_present);
    if ((g_cycle_counter_ok == 0u) || (g_work_active != 0u)) {
        return;
    }

    g_work_start_clock_hz = SystemCoreClock;
    g_work_start_cycle = DWT->CYCCNT;
    g_work_active = 1u;
}

void templogpro_power_work_end(void)
{
    uint32_t elapsed_cycles;

    if ((g_cycle_counter_ok == 0u) || (g_work_active == 0u)) {
        return;
    }

    elapsed_cycles = DWT->CYCCNT - g_work_start_cycle;
    if (g_work_start_clock_hz != 0u) {
        g_active_cpu_us += (((uint64_t)elapsed_cycles * 1000000u) +
                            (g_work_start_clock_hz / 2u)) /
                           g_work_start_clock_hz;
    }
    g_work_active = 0u;
}

void templogpro_power_enter_idle(void)
{
    uint32_t before_ms = HAL_GetTick();
    uint32_t after_ms;
    uint32_t primask = __get_PRIMASK();

    if ((primask != 0u) || (__get_IPSR() != 0u)) {
        return;
    }

    /* Keep the queue check and WFI atomic. A pending IRQ still wakes WFI
       with PRIMASK set; its handler runs as soon as the mask is restored. */
    __disable_irq();
    if (templogpro_usb_st_work_pending() != 0u) {
        __set_PRIMASK(primask);
        return;
    }

    SCB->SCR &= ~SCB_SCR_SLEEPDEEP_Msk;
    __DSB();
    __WFI();
    __set_PRIMASK(primask);
    __ISB();

    after_ms = HAL_GetTick();
    g_idle_sleep_ms = power_add_u32(g_idle_sleep_ms, after_ms - before_ms);
    g_idle_entries = power_add_u32(g_idle_entries, 1u);
}

uint32_t templogpro_power_enter_stop(void)
{
    uint32_t now_ms = HAL_GetTick();
    uint32_t button_wakeup;
    uint32_t wake_s = templogpro_app_stop_wake_seconds();
    uint32_t usb_armed;

    if (templogpro_buttons_take_wakeup() != 0u) {
        g_stop_inhibit_until_ms = now_ms + TEMPLOGPRO_BUTTON_WAKE_GUARD_MS;
        g_stop_deferred = power_add_u32(g_stop_deferred, 1u);
        return 0u;
    }
    if ((int32_t)(now_ms - g_stop_inhibit_until_ms) < 0) {
        g_stop_deferred = power_add_u32(g_stop_deferred, 1u);
        return 0u;
    }
    if (templogpro_buttons_current_mask() != 0u) {
        g_stop_deferred = power_add_u32(g_stop_deferred, 1u);
        return 0u;
    }
    /* 0 = OFF (storage): no RTC tick at all, only buttons or the USB cable wake the MCU. */
    if (wake_s > TEMPLOGPRO_STOP_WAKE_MAX_S) {
        wake_s = 1u;
    }
    if ((wake_s != 0u) && (power_rtc_wakeup_start(wake_s) == 0u)) {
        g_stop_deferred = power_add_u32(g_stop_deferred, 1u);
        return 0u;
    }
    if (templogpro_buttons_take_wakeup() != 0u) {
        power_rtc_wakeup_stop();
        g_stop_inhibit_until_ms = HAL_GetTick() + TEMPLOGPRO_BUTTON_WAKE_GUARD_MS;
        g_stop_deferred = power_add_u32(g_stop_deferred, 1u);
        return 0u;
    }

    g_rtc_wakeup = 0u;
    usb_armed = templogpro_usb_arm_cable_wake();
    HAL_SuspendTick();
    __HAL_PWR_CLEAR_FLAG(PWR_FLAG_WU);
    SET_BIT(PWR->CR, PWR_CR_ULP | PWR_CR_FWU);
    HAL_PWR_EnterSTOPMode(PWR_LOWPOWERREGULATOR_ON, PWR_STOPENTRY_WFI);

    power_rtc_wakeup_stop();
    if ((usb_armed != 0u) && (templogpro_usb_disarm_cable_wake() != 0u)) {
        templogpro_usb_request_probe();
    }
    SystemCoreClockUpdate();
    (void)HAL_InitTick(uwTickPrio);
    HAL_ResumeTick();

    if (g_rtc_wakeup != 0u) {
        uwTick += wake_s * TEMPLOGPRO_STOP_WAKE_MS;
        g_stop_sleep_ms = power_add_u32(g_stop_sleep_ms, wake_s * TEMPLOGPRO_STOP_WAKE_MS);
        g_rtc_wakeups = power_add_u32(g_rtc_wakeups, 1u);
    }

    button_wakeup = templogpro_buttons_take_wakeup();
    if (button_wakeup != 0u) {
        g_stop_inhibit_until_ms = HAL_GetTick() + TEMPLOGPRO_BUTTON_WAKE_GUARD_MS;
        g_button_wakeups = power_add_u32(g_button_wakeups, 1u);
    }

    g_stop_count = power_add_u32(g_stop_count, 1u);
    return 1u;
}

void templogpro_power_system_reset(void)
{
    NVIC_SystemReset();
}

uint32_t templogpro_power_stop_count(void)
{
    return g_stop_count;
}

uint32_t templogpro_power_read_vdd_mv(uint16_t *vdd_millivolts)
{
    uint16_t sample;
    uint32_t now_ms;

    if (vdd_millivolts == 0) {
        return 0u;
    }

    now_ms = HAL_GetTick();
    if ((g_vdd_valid != 0u) &&
        ((now_ms - g_vdd_last_ms) < TEMPLOGPRO_VDD_CACHE_MS)) {
        *vdd_millivolts = g_vdd_millivolts;
        return 1u;
    }

    if (templogpro_hw_adc_read_vdd_mv(&sample) == 0u) {
        return 0u;
    }

    g_vdd_millivolts = sample;
    g_vdd_last_ms = now_ms;
    g_vdd_valid = 1u;
    g_vdd_samples = power_add_u32(g_vdd_samples, 1u);
    if ((g_vdd_min_millivolts == 0u) || (sample < g_vdd_min_millivolts)) {
        g_vdd_min_millivolts = sample;
    }
    if (sample > g_vdd_max_millivolts) {
        g_vdd_max_millivolts = sample;
    }

    *vdd_millivolts = sample;
    return 1u;
}

void templogpro_power_get_stats(TempLogProPowerStats *stats)
{
    TempLogProDiagnostics diag;
    uint64_t active_ms;

    if (stats == 0) {
        return;
    }

    power_account_elapsed();
    templogpro_diag_get(&diag);
    active_ms = g_active_cpu_us / 1000u;

    stats->uptime_ms = HAL_GetTick();
    stats->usb_time_ms = g_usb_time_ms;
    stats->battery_time_ms = g_battery_time_ms;
    stats->active_cpu_ms = (active_ms > 0xFFFFFFFFu) ? 0xFFFFFFFFu : (uint32_t)active_ms;
    stats->idle_sleep_ms = g_idle_sleep_ms;
    stats->stop_sleep_ms = g_stop_sleep_ms;
    stats->idle_entries = g_idle_entries;
    stats->stop_entries = g_stop_count;
    stats->rtc_wakeups = g_rtc_wakeups;
    stats->button_wakeups = g_button_wakeups;
    stats->stop_deferred = g_stop_deferred;
    stats->system_clock_hz = SystemCoreClock;
    stats->usb_present = diag.usb_vbus_present ? 1u : 0u;
    stats->cycle_counter_ok = g_cycle_counter_ok;
    stats->vdd_samples = g_vdd_samples;
    stats->vdd_millivolts = g_vdd_millivolts;
    stats->vdd_min_millivolts = g_vdd_min_millivolts;
    stats->vdd_max_millivolts = g_vdd_max_millivolts;
}

void RTC_WKUP_IRQHandler(void)
{
    if ((RTC->ISR & RTC_ISR_WUTF) != 0u) {
        power_rtc_clear_wakeup_flag();
        g_rtc_wakeup = 1u;
    }
    EXTI->PR = EXTI_PR_PR20;
}

static void power_rtc_unlock(void)
{
    RTC->WPR = 0xCAu;
    RTC->WPR = 0x53u;
}

static void power_rtc_lock(void)
{
    RTC->WPR = 0xFFu;
}

static void power_rtc_clear_wakeup_flag(void)
{
    RTC->ISR = ~((uint32_t)RTC_ISR_WUTF | RTC_ISR_INIT) |
               (RTC->ISR & RTC_ISR_INIT);
}

static uint32_t power_rtc_wakeup_start(uint32_t seconds)
{
    uint32_t timeout = TEMPLOGPRO_RTC_WUT_TIMEOUT;

    power_rtc_unlock();
    RTC->CR &= ~(RTC_CR_WUTE | RTC_CR_WUTIE);
    while (((RTC->ISR & RTC_ISR_WUTWF) == 0u) && (timeout != 0u)) {
        timeout--;
    }
    if ((RTC->ISR & RTC_ISR_WUTWF) == 0u) {
        power_rtc_lock();
        return 0u;
    }

    RTC->WUTR = seconds - 1u;  /* ck_spre (1 Hz): wake after "seconds" */
    RTC->CR = (RTC->CR & ~RTC_CR_WUCKSEL) | RTC_CR_WUCKSEL_2;
    power_rtc_clear_wakeup_flag();
    EXTI->PR = EXTI_PR_PR20;
    RTC->CR |= RTC_CR_WUTIE | RTC_CR_WUTE;
    power_rtc_lock();
    return 1u;
}

static void power_rtc_wakeup_stop(void)
{
    power_rtc_unlock();
    RTC->CR &= ~(RTC_CR_WUTE | RTC_CR_WUTIE);
    power_rtc_clear_wakeup_flag();
    power_rtc_lock();
    EXTI->PR = EXTI_PR_PR20;
}

static void power_account_elapsed(void)
{
    TempLogProDiagnostics diag;
    uint32_t now_ms = HAL_GetTick();
    uint32_t elapsed_ms = now_ms - g_account_last_ms;

    if (g_account_usb != 0u) {
        g_usb_time_ms = power_add_u32(g_usb_time_ms, elapsed_ms);
    } else {
        g_battery_time_ms = power_add_u32(g_battery_time_ms, elapsed_ms);
    }

    g_account_last_ms = now_ms;
    templogpro_diag_get(&diag);
    g_account_usb = diag.usb_vbus_present ? 1u : 0u;
}

static void power_cycle_counter_set(uint32_t enable)
{
    if (enable != 0u) {
        if (g_cycle_counter_ok != 0u) {
            return;
        }
        CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
        DWT->CYCCNT = 0u;
        DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
        g_cycle_counter_ok = ((DWT->CTRL & DWT_CTRL_CYCCNTENA_Msk) != 0u) ? 1u : 0u;
        return;
    }

    DWT->CTRL &= ~DWT_CTRL_CYCCNTENA_Msk;
    CoreDebug->DEMCR &= ~CoreDebug_DEMCR_TRCENA_Msk;
    g_cycle_counter_ok = 0u;
    g_work_active = 0u;
}

static uint32_t power_add_u32(uint32_t value, uint32_t increment)
{
    if (increment > (0xFFFFFFFFu - value)) {
        return 0xFFFFFFFFu;
    }
    return value + increment;
}
