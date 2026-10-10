#include <stddef.h>
#include <stdint.h>
#include "stm32l1xx.h"
#include "templogpro_app.h"
#include "templogpro_config.h"
#include "templogpro_diag.h"
#include "templogpro_usb.h"
#include "usbd_msc.h"

static TempLogProConfig g_app_config;
static TempLogProState g_app_state;
static uint32_t g_lcd_awake, g_action_active, g_templogpro_usb_started;
static uint32_t g_idle_sleep_ms, g_idle_entries;
static USBD_HandleTypeDef USBD_Device;
static volatile uint16_t cdc_rx_head, cdc_rx_tail, cdc_tx_head, cdc_tx_tail;
static volatile uint8_t cdc_tx_busy;
static uint32_t usb_present, flash_busy, buttons, stats_ready, measure_pulse, alarm;
static uint32_t tick, primask, ipsr, wfi_count, sleep_ms, injection, pending_irq;
static uint32_t bad_order;
static uint32_t sensor_pending;
uint32_t templogpro_sensor_pending(void) { return sensor_pending; }
static struct { uint32_t SCR; } fake_scb;
volatile uint32_t test_count;


uint32_t HAL_GetTick(void) { return tick; }
void templogpro_diag_get(TempLogProDiagnostics *diag) { diag->usb_vbus_present = usb_present; }
uint32_t templogpro_logger_flash_is_busy(void) { return flash_busy; }
static uint32_t templogpro_buttons_current_mask(void) { return buttons; }
static uint32_t templogpro_logger_stats_ready(void) { return stats_ready; }
static uint32_t app_measure_pulse_active(uint32_t now) { (void)now; return measure_pulse; }
static uint32_t app_alarm_or_error_active(void) { return alarm; }
/* Same rule as templogpro_app.c: awake for the 350 ms double flash and when one is due (every 3 s). */
static uint32_t app_red_led_busy(uint32_t now) { return (now < 350u || now >= 3000u) ? 1u : 0u; }
static uint32_t fake_get_primask(void) { return primask; }

/* Model interrupt arrival at both sides of the masked queue check. */
static void fake_disable_irq(void)
{
    if (injection == 1u) cdc_rx_head++;
    primask = 1u;
}
static void fake_dsb(void)
{
    if (primask != 1u) bad_order++;
    if (injection == 2u) pending_irq = 1u;
}
static void fake_wfi(void)
{
    if (primask != 1u || (fake_scb.SCR & SCB_SCR_SLEEPDEEP_Msk)) bad_order++;
    wfi_count++;
    if (!pending_irq) tick += sleep_ms;
}
static void fake_restore_irq(uint32_t mask)
{
    primask = mask;
    if (!mask && pending_irq) {
        cdc_rx_head++;
        pending_irq = 0u;
    }
}
#undef SCB
#undef __WFI
#define SCB (&fake_scb)
#define __get_PRIMASK() fake_get_primask()
#define __get_IPSR() (ipsr)
#define __disable_irq() fake_disable_irq()
#define __set_PRIMASK(mask) fake_restore_irq(mask)
#define __DSB() fake_dsb()
#define __WFI() fake_wfi()
#define __ISB() ((void)0)

/* Generated verbatim from the production function definitions. */
#include "usb_idle_functions.inc"

#define CHECK(expr) do { test_count++; if (!(expr)) return __LINE__; } while (0)

int run_tests(void)
{
    test_count = 0u;
    stats_ready = 1u;
    tick = 500u;
    for (uint32_t usb = 0u; usb < 2u; usb++) {
        for (uint32_t battery = 0u; battery < 2u; battery++) {
            for (uint32_t error = 0u; error < 2u; error++) {
                for (uint32_t busy = 0u; busy < 2u; busy++) {
                    usb_present = usb;
                    g_app_config.battery_mode_enable = battery;
                    g_app_state = error ? TEMPLOGPRO_STATE_ERROR : TEMPLOGPRO_STATE_RUN;
                    flash_busy = busy;
                    uint32_t idle = TEMPLOGPRO_IDLE_WFI_ENABLE && !busy && (usb || (battery && !error));
                    CHECK(templogpro_app_should_idle_sleep() == idle);
                    CHECK(templogpro_app_should_stop_sleep() == (idle && !usb));
                }
            }
        }
    }
    usb_present = flash_busy = 0u;
    g_app_config.battery_mode_enable = 1u;
    g_app_state = TEMPLOGPRO_STATE_RUN;
    g_lcd_awake = 1u;
    CHECK(templogpro_app_should_stop_sleep() == 0u);
    g_lcd_awake = 0u;
    sensor_pending = 1u;
    CHECK(templogpro_app_should_stop_sleep() == 0u);
    sensor_pending = 0u;
    g_action_active = 1u;
    CHECK(templogpro_app_should_stop_sleep() == 0u);
    g_action_active = 0u;
    buttons = 1u;
    CHECK(templogpro_app_should_stop_sleep() == 0u);
    buttons = 0u;
    stats_ready = 0u;
    CHECK(templogpro_app_should_stop_sleep() == 0u);
    stats_ready = 1u;
    measure_pulse = 1u;
    CHECK(templogpro_app_should_stop_sleep() == 0u);
    measure_pulse = 0u;
    alarm = g_app_config.led_enable = 1u;
    tick = 50u;
    CHECK(templogpro_app_should_stop_sleep() == 0u);
    tick = 300u;
    CHECK(templogpro_app_should_stop_sleep() == 0u);
    tick = 400u;
    CHECK(templogpro_app_should_stop_sleep() == TEMPLOGPRO_IDLE_WFI_ENABLE);
    tick = 3000u;
    CHECK(templogpro_app_should_stop_sleep() == 0u);
    tick = 100u;

    CHECK(USBD_MSC_CDC_WorkPending(NULL) == 0u);
    cdc_rx_head = 1u;
    CHECK(templogpro_usb_st_work_pending() == 0u);
    g_templogpro_usb_started = 1u;
    CHECK(templogpro_usb_st_work_pending() == 0u);
    USBD_Device.dev_state = USBD_STATE_CONFIGURED;
    CHECK(templogpro_usb_st_work_pending() == 1u);
    cdc_rx_head = 0u;
    cdc_rx_tail = 255u;
    CHECK(templogpro_usb_st_work_pending() == 1u);
    cdc_rx_tail = 0u;
    CHECK(templogpro_usb_st_work_pending() == 0u);
    cdc_tx_head = 1u;
    CHECK(templogpro_usb_st_work_pending() == 1u);
    cdc_tx_busy = 1u;
    CHECK(templogpro_usb_st_work_pending() == 0u);
    cdc_tx_busy = 0u;
    cdc_tx_head = 0u;
    cdc_tx_tail = 1023u;
    CHECK(templogpro_usb_st_work_pending() == 1u);
    cdc_tx_tail = 0u;

    fake_scb.SCR = SCB_SCR_SLEEPDEEP_Msk | SCB_SCR_SEVONPEND_Msk;
    sleep_ms = 7u;
    templogpro_power_enter_idle();
    CHECK(wfi_count == 1u && g_idle_entries == 1u && g_idle_sleep_ms == 7u);
    CHECK(primask == 0u && bad_order == 0u);
    CHECK(fake_scb.SCR == SCB_SCR_SEVONPEND_Msk);
    cdc_rx_head = 1u;
    templogpro_power_enter_idle();
    CHECK(wfi_count == 1u && primask == 0u && g_idle_entries == 1u);
    cdc_rx_head = 0u;
    cdc_tx_head = 1u;
    templogpro_power_enter_idle();
    CHECK(wfi_count == 1u && primask == 0u);
    cdc_tx_busy = 1u;
    templogpro_power_enter_idle();
    CHECK(wfi_count == 2u && primask == 0u);
    cdc_tx_head = cdc_tx_busy = 0u;
    primask = 1u;
    templogpro_power_enter_idle();
    CHECK(wfi_count == 2u && primask == 1u);
    primask = 0u;
    ipsr = 15u;
    templogpro_power_enter_idle();
    CHECK(wfi_count == 2u && primask == 0u);
    ipsr = 0u;

    injection = 1u;
    templogpro_power_enter_idle();
    CHECK(wfi_count == 2u && primask == 0u && cdc_rx_head == 1u);
    cdc_rx_head = 0u;
    injection = 2u;
    tick = 100u;
    templogpro_power_enter_idle();
    CHECK(wfi_count == 3u && primask == 0u && cdc_rx_head == 1u);
    CHECK(tick == 100u && pending_irq == 0u && bad_order == 0u);
    templogpro_power_enter_idle();
    CHECK(wfi_count == 3u);
    injection = 0u;
    cdc_rx_head = 0u;
    tick = UINT32_MAX - 2u;
    g_idle_sleep_ms = 0u;
    templogpro_power_enter_idle();
    CHECK(g_idle_sleep_ms == 7u && tick == 4u);
    g_idle_sleep_ms = UINT32_MAX - 2u;
    g_idle_entries = UINT32_MAX;
    templogpro_power_enter_idle();
    CHECK(g_idle_sleep_ms == UINT32_MAX && g_idle_entries == UINT32_MAX);
    return 0;
}
