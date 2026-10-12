#include "stm32l1xx_hal.h"
#include "templogpro_diag.h"
#include "templogpro_power.h"
#include "templogpro_usb.h"
#include "templogpro_usb_msc_storage.h"
#include "usbd_core.h"
#include "usbd_desc.h"
#include "usbd_msc.h"

#define TEMPLOGPRO_LED_GREEN_PIN       0U
#define TEMPLOGPRO_LED_RED_PIN         8U
#define TEMPLOGPRO_USB_RETRY_MS        5000U
#define TEMPLOGPRO_USB_ENUM_TIMEOUT_MS 3000U
#define TEMPLOGPRO_USB_LINK_LOSS_MS    3000U
#define TEMPLOGPRO_USB_PROBE_SAMPLES   16U
#define TEMPLOGPRO_USB_PROBE_REQUIRED  12U

#ifndef TEMPLOGPRO_USB_POWER_PATH_DIAG
#define TEMPLOGPRO_USB_POWER_PATH_DIAG  0U
#endif

#ifndef TEMPLOGPRO_HSE_BYPASS
#define TEMPLOGPRO_HSE_BYPASS          0U
#endif

#if (HSE_VALUE != 8000000U)
#error "TempLogPro USB test expects HSE_VALUE = 8000000U for PLL VCO 96 MHz"
#endif

#define TEMPLOGPRO_RCC_AHBENR          (*(volatile uint32_t *)0x4002381Cu)
#define TEMPLOGPRO_GPIOA_MODER         (*(volatile uint32_t *)0x40020000u)
#define TEMPLOGPRO_GPIOA_PUPDR         (*(volatile uint32_t *)0x4002000Cu)
#define TEMPLOGPRO_GPIOA_IDR           (*(volatile uint32_t *)0x40020010u)
#define TEMPLOGPRO_GPIOB_MODER         (*(volatile uint32_t *)0x40020400u)
#define TEMPLOGPRO_GPIOB_OTYPER        (*(volatile uint32_t *)0x40020404u)
#define TEMPLOGPRO_GPIOB_OSPEEDR       (*(volatile uint32_t *)0x40020408u)
#define TEMPLOGPRO_GPIOB_PUPDR         (*(volatile uint32_t *)0x4002040Cu)
#define TEMPLOGPRO_GPIOB_BSRR          (*(volatile uint32_t *)0x40020418u)
#define TEMPLOGPRO_GPIOC_MODER         (*(volatile uint32_t *)0x40020800u)
#define TEMPLOGPRO_GPIOC_OTYPER        (*(volatile uint32_t *)0x40020804u)
#define TEMPLOGPRO_GPIOC_OSPEEDR       (*(volatile uint32_t *)0x40020808u)
#define TEMPLOGPRO_GPIOC_PUPDR         (*(volatile uint32_t *)0x4002080Cu)
#define TEMPLOGPRO_GPIOC_BSRR          (*(volatile uint32_t *)0x40020818u)

USBD_HandleTypeDef USBD_Device;
extern volatile uint32_t templogpro_usb_st_status;

static void templogpro_leds_init(void);
static void templogpro_green_on(void);
static void templogpro_green_off(void);
static void templogpro_red_on(void);
static void templogpro_red_off(void);
static void templogpro_delay_raw(uint32_t loops);
static uint32_t templogpro_usb_clock_config(void);
static void templogpro_usb_stop_for_battery(void);
static uint32_t templogpro_usb_fail(uint32_t status);
static uint32_t templogpro_usb_cable_present(void);

static uint32_t g_templogpro_usb_started;
static uint32_t g_templogpro_usb_last_retry_ms;
static volatile uint32_t g_templogpro_usb_cable_wake;
static uint32_t g_templogpro_usb_start_ms;

extern volatile uint32_t templogpro_usb_bus_event_count;
extern volatile uint32_t templogpro_usb_last_bus_activity_ms;

uint32_t templogpro_usb_st_msc_init(void)
{
    if (templogpro_usb_cable_present() == 0U) {
        templogpro_usb_st_status = 0xE201U;
        g_templogpro_usb_started = 0U;
        templogpro_diag_set_usb(0U);
        templogpro_diag_set_usb_vbus(0U);
        (void)templogpro_power_battery_clock_config();
        g_templogpro_usb_last_retry_ms = HAL_GetTick();
        return 0U;
    }

#if (TEMPLOGPRO_USB_POWER_PATH_DIAG != 0U)
    /* Diagnostic image: detect the cable but leave the MCU in the battery
       clock/sleep profile. USB does not enumerate in this build. */
    templogpro_usb_st_status = 0xD201U;
    g_templogpro_usb_started = 0U;
    templogpro_diag_set_usb(0U);
    templogpro_diag_set_usb_vbus(0U);
    (void)templogpro_power_battery_clock_config();
    g_templogpro_usb_last_retry_ms = HAL_GetTick();
    return 0U;
#endif

    templogpro_leds_init();
    templogpro_green_on();
    templogpro_red_off();

    templogpro_usb_st_status = 1U;
    g_templogpro_usb_started = 0U;

    if (HAL_Init() != HAL_OK) {
        return templogpro_usb_fail(0xE001U);
    }

    if (templogpro_usb_clock_config() == 0U) {
        return 0U;
    }
    templogpro_usb_st_status = 2U;

    if (USBD_Init(&USBD_Device, &HID_Desc, 0U) != USBD_OK) {
        return templogpro_usb_fail(0xE004U);
    }
    templogpro_usb_st_status = 3U;

    if (USBD_RegisterClass(&USBD_Device, USBD_MSC_CLASS) != USBD_OK) {
        return templogpro_usb_fail(0xE005U);
    }
    templogpro_usb_st_status = 4U;

    if (USBD_MSC_RegisterStorage(&USBD_Device, &TEMPLOGPRO_MSC_STORAGE) != USBD_OK) {
        return templogpro_usb_fail(0xE006U);
    }

    templogpro_usb_bus_event_count = 0U;
    templogpro_usb_last_bus_activity_ms = HAL_GetTick();
    if (USBD_Start(&USBD_Device) != USBD_OK) {
        return templogpro_usb_fail(0xE007U);
    }

    g_templogpro_usb_started = 1U;
    g_templogpro_usb_start_ms = HAL_GetTick();
    templogpro_diag_set_usb(0U);
    templogpro_diag_set_usb_vbus(1U);
    return 1U;
}

void templogpro_usb_st_msc_process(void)
{
    uint32_t now;

    if (g_templogpro_usb_started == 0U) {
        now = HAL_GetTick();

        templogpro_diag_set_usb(0U);
        if ((now - g_templogpro_usb_last_retry_ms) >= TEMPLOGPRO_USB_RETRY_MS) {
            g_templogpro_usb_last_retry_ms = now;
            if (templogpro_usb_cable_present() != 0U) {
                (void)templogpro_usb_st_msc_init();
            }
        }
        return;
    }

    now = HAL_GetTick();
    if ((templogpro_usb_bus_event_count == 0U) &&
        ((now - g_templogpro_usb_start_ms) >= TEMPLOGPRO_USB_ENUM_TIMEOUT_MS)) {
        templogpro_usb_stop_for_battery();
        return;
    }

    if ((templogpro_usb_bus_event_count != 0U) &&
        ((now - templogpro_usb_last_bus_activity_ms) >= TEMPLOGPRO_USB_LINK_LOSS_MS)) {
        templogpro_usb_stop_for_battery();
        return;
    }

    USBD_MSC_CDC_Process(&USBD_Device);
    templogpro_usb_msc_storage_process();
    templogpro_diag_set_usb((USBD_Device.dev_state == USBD_STATE_CONFIGURED) ? 1U : 0U);
    if (templogpro_usb_bus_event_count != 0U) {
        templogpro_diag_set_usb_vbus(1U);
    }
}

uint32_t templogpro_usb_st_work_pending(void)
{
    if (g_templogpro_usb_started == 0U) {
        return 0U;
    }
    return USBD_MSC_CDC_WorkPending(&USBD_Device);
}

static void templogpro_leds_init(void)
{
    TEMPLOGPRO_RCC_AHBENR |= (1U << 1) | (1U << 2);

    TEMPLOGPRO_GPIOC_MODER &= ~(3U << (TEMPLOGPRO_LED_GREEN_PIN * 2U));
    TEMPLOGPRO_GPIOC_MODER |=  (1U << (TEMPLOGPRO_LED_GREEN_PIN * 2U));
    TEMPLOGPRO_GPIOC_OTYPER &= ~(1U << TEMPLOGPRO_LED_GREEN_PIN);
    TEMPLOGPRO_GPIOC_OSPEEDR |= (3U << (TEMPLOGPRO_LED_GREEN_PIN * 2U));
    TEMPLOGPRO_GPIOC_PUPDR &= ~(3U << (TEMPLOGPRO_LED_GREEN_PIN * 2U));

    TEMPLOGPRO_GPIOB_MODER &= ~(3U << (TEMPLOGPRO_LED_RED_PIN * 2U));
    TEMPLOGPRO_GPIOB_MODER |=  (1U << (TEMPLOGPRO_LED_RED_PIN * 2U));
    TEMPLOGPRO_GPIOB_OTYPER &= ~(1U << TEMPLOGPRO_LED_RED_PIN);
    TEMPLOGPRO_GPIOB_OSPEEDR |= (3U << (TEMPLOGPRO_LED_RED_PIN * 2U));
    TEMPLOGPRO_GPIOB_PUPDR &= ~(3U << (TEMPLOGPRO_LED_RED_PIN * 2U));

    templogpro_green_off();
    templogpro_red_off();
}

static void templogpro_green_on(void)
{
    TEMPLOGPRO_GPIOC_BSRR = (1U << TEMPLOGPRO_LED_GREEN_PIN);
}

static void templogpro_green_off(void)
{
    TEMPLOGPRO_GPIOC_BSRR = (1U << (TEMPLOGPRO_LED_GREEN_PIN + 16U));
}

static void templogpro_red_on(void)
{
    TEMPLOGPRO_GPIOB_BSRR = (1U << TEMPLOGPRO_LED_RED_PIN);
}

static void templogpro_red_off(void)
{
    TEMPLOGPRO_GPIOB_BSRR = (1U << (TEMPLOGPRO_LED_RED_PIN + 16U));
}

static void templogpro_delay_raw(uint32_t loops)
{
    volatile uint32_t i;

    for (i = 0U; i < loops; i++) {
        __NOP();
    }
}

static uint32_t templogpro_usb_clock_config(void)
{
    RCC_OscInitTypeDef osc = {0U};
    RCC_ClkInitTypeDef clk = {0U};
    uint32_t timeout;

    __HAL_RCC_PWR_CLK_ENABLE();
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);
    timeout = 1000000U;
    while ((__HAL_PWR_GET_FLAG(PWR_FLAG_VOS) != RESET) && (timeout != 0U)) {
        timeout--;
    }
    if (__HAL_PWR_GET_FLAG(PWR_FLAG_VOS) != RESET) {
        return templogpro_usb_fail(0xE101U);
    }

    osc.OscillatorType = RCC_OSCILLATORTYPE_HSE;
#if (TEMPLOGPRO_HSE_BYPASS != 0U)
    osc.HSEState = RCC_HSE_BYPASS;
#else
    osc.HSEState = RCC_HSE_ON;
#endif
    osc.PLL.PLLState = RCC_PLL_ON;
    osc.PLL.PLLSource = RCC_PLLSOURCE_HSE;
    osc.PLL.PLLMUL = RCC_PLL_MUL12;
    osc.PLL.PLLDIV = RCC_PLL_DIV3;

    if (HAL_RCC_OscConfig(&osc) != HAL_OK) {
        return templogpro_usb_fail(0xE102U);
    }

    clk.ClockType = RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    clk.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    clk.AHBCLKDivider = RCC_SYSCLK_DIV1;
    clk.APB1CLKDivider = RCC_HCLK_DIV1;
    clk.APB2CLKDivider = RCC_HCLK_DIV1;

    if (HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_1) != HAL_OK) {
        return templogpro_usb_fail(0xE103U);
    }

    if (HAL_SetTickFreq(HAL_TICK_FREQ_1KHZ) != HAL_OK) {
        return templogpro_usb_fail(0xE104U);
    }

    return 1U;
}

static void templogpro_usb_stop_for_battery(void)
{
    (void)USBD_DeInit(&USBD_Device);
    __HAL_RCC_SYSCFG_CLK_DISABLE();
    g_templogpro_usb_started = 0U;
    g_templogpro_usb_start_ms = 0U;
    templogpro_usb_st_status = 0U;
    templogpro_diag_set_usb(0U);
    templogpro_diag_set_usb_vbus(0U);
    (void)templogpro_power_battery_clock_config();
    g_templogpro_usb_last_retry_ms = HAL_GetTick();
}

static uint32_t templogpro_usb_fail(uint32_t status)
{
    templogpro_usb_st_status = status;
    g_templogpro_usb_started = 0U;
    templogpro_diag_set_usb(0U);
    templogpro_red_on();
    return 0U;
}

/* STOP mode cable detection: with D+ (PA12) pulled up internally, a connected
   host's 15k pull-down drives it low, which wakes the MCU through EXTI12. Only
   armed while the line is high, so a plugged-in host that does not enumerate
   cannot keep the device awake (the normal retry still runs on the RTC tick). */
uint32_t templogpro_usb_arm_cable_wake(void)
{
    if (g_templogpro_usb_started != 0U) {
        return 0U;
    }
    TEMPLOGPRO_RCC_AHBENR |= (1U << 0);
    TEMPLOGPRO_GPIOA_MODER &= ~(3U << 24);
    TEMPLOGPRO_GPIOA_PUPDR = (TEMPLOGPRO_GPIOA_PUPDR & ~(3U << 24)) | (1U << 24);
    templogpro_delay_raw(64U);
    if ((TEMPLOGPRO_GPIOA_IDR & (1U << 12)) == 0U) {
        TEMPLOGPRO_GPIOA_PUPDR &= ~(3U << 24);
        return 0U;
    }
    __HAL_RCC_SYSCFG_CLK_ENABLE();
    SYSCFG->EXTICR[3] &= ~SYSCFG_EXTICR4_EXTI12;
    __HAL_RCC_SYSCFG_CLK_DISABLE();
    g_templogpro_usb_cable_wake = 0U;
    EXTI->RTSR &= ~EXTI_RTSR_TR12;
    EXTI->FTSR |= EXTI_FTSR_TR12;
    EXTI->PR = EXTI_PR_PR12;
    EXTI->IMR |= EXTI_IMR_MR12;
    HAL_NVIC_SetPriority(EXTI15_10_IRQn, 3U, 0U);
    HAL_NVIC_EnableIRQ(EXTI15_10_IRQn);
    return 1U;
}

uint32_t templogpro_usb_disarm_cable_wake(void)
{
    EXTI->IMR &= ~EXTI_IMR_MR12;
    EXTI->FTSR &= ~EXTI_FTSR_TR12;
    EXTI->PR = EXTI_PR_PR12;
    HAL_NVIC_DisableIRQ(EXTI15_10_IRQn);
    TEMPLOGPRO_GPIOA_PUPDR &= ~(3U << 24);
    return g_templogpro_usb_cable_wake;
}

void templogpro_usb_request_probe(void)
{
    g_templogpro_usb_last_retry_ms = HAL_GetTick() - TEMPLOGPRO_USB_RETRY_MS;
}

void EXTI15_10_IRQHandler(void)
{
    if ((EXTI->PR & EXTI_PR_PR12) != 0U) {
        EXTI->PR = EXTI_PR_PR12;
        g_templogpro_usb_cable_wake = 1U;
    }
}

static uint32_t templogpro_usb_cable_present(void)
{
    const uint32_t pin_mask = (1U << 11) | (1U << 12);
    const uint32_t mode_mask = (3U << 22) | (3U << 24);
    const uint32_t pull_up = (1U << 22) | (1U << 24);
    uint32_t saved_moder;
    uint32_t saved_pupdr;
    uint32_t low_samples = 0U;
    uint32_t sample;

    TEMPLOGPRO_RCC_AHBENR |= (1U << 0);
    __HAL_RCC_SYSCFG_CLK_ENABLE();
    __HAL_SYSCFG_USBPULLUP_DISABLE();

    saved_moder = TEMPLOGPRO_GPIOA_MODER;
    saved_pupdr = TEMPLOGPRO_GPIOA_PUPDR;
    TEMPLOGPRO_GPIOA_MODER = saved_moder & ~mode_mask;
    TEMPLOGPRO_GPIOA_PUPDR = (saved_pupdr & ~mode_mask) | pull_up;

    templogpro_delay_raw(256U);
    for (sample = 0U; sample < TEMPLOGPRO_USB_PROBE_SAMPLES; sample++) {
        if ((TEMPLOGPRO_GPIOA_IDR & pin_mask) == 0U) {
            low_samples++;
        }
        __NOP();
        __NOP();
        __NOP();
        __NOP();
    }

    TEMPLOGPRO_GPIOA_PUPDR = saved_pupdr;
    TEMPLOGPRO_GPIOA_MODER = saved_moder;

    if (low_samples >= TEMPLOGPRO_USB_PROBE_REQUIRED) {
        templogpro_diag_set_usb_vbus(1U);
        return 1U;
    }

    __HAL_RCC_SYSCFG_CLK_DISABLE();
    templogpro_diag_set_usb_vbus(0U);
    return 0U;
}
