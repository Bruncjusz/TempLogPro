/*
 * ADC1 with the internal VREFINT reference: VDD of the MCU (not the battery
 * terminal) and a read-only scan of candidate analog pins. HSI and the ADC
 * are powered only for the duration of one call.
 */
#include <string.h>
#include "board.h"
#include "board_regs.h"
#include "templogpro_hw_bridge.h"

#define ADC_VREFINT_CHANNEL      17u
#define ADC_VREFINT_CAL_ADDR     0x1FF800F8u
#define ADC_VREFINT_CAL_MV       3000u
#define ADC_FULL_SCALE           4095u
#define ADC_WAIT_TIMEOUT         100000u
#define ADC_HSI_TIMEOUT          1000000u

typedef struct
{
    char pin[4];
    uint8_t channel;
    uint32_t port;
    uint8_t pin_no;
} AdcScanCandidate;

static const AdcScanCandidate ADC_SCAN_CANDIDATES[TEMPLOGPRO_ADC_SCAN_MAX_ITEMS] = {
    {{'P','A','2','\0'},  2u, GPIOA_BASE, 2u},
    {{'P','A','3','\0'},  3u, GPIOA_BASE, 3u},
    {{'P','B','0','\0'},  8u, GPIOB_BASE, 0u},
    {{'P','B','1','\0'},  9u, GPIOB_BASE, 1u},
    {{'P','C','1','\0'}, 11u, GPIOC_BASE, 1u},
    {{'P','C','2','\0'}, 12u, GPIOC_BASE, 2u},
    {{'P','C','3','\0'}, 13u, GPIOC_BASE, 3u},
    {{'P','C','4','\0'}, 14u, GPIOC_BASE, 4u},
    {{'P','C','5','\0'}, 15u, GPIOC_BASE, 5u},
};

/* Leaves HSI on when it is the system clock or feeds the PLL. */
static void adc_shutdown(void)
{
    uint32_t sysclk = RCC_CFGR & RCC_CFGR_SWS_MASK;

    ADC1_CR2 = 0u;
    ADC_CCR &= ~ADC_CCR_TSVREFE;
    RCC_APB2ENR &= ~RCC_APB2ENR_ADC1EN;

    if ((sysclk != RCC_CFGR_SWS_HSI) &&
        !((sysclk == RCC_CFGR_SWS_PLL) && ((RCC_CFGR & RCC_CFGR_PLLSRC_HSE) == 0u))) {
        RCC_CR &= ~RCC_CR_HSION;
    }
}

static uint32_t adc_init_once(void)
{
    uint32_t timeout;

    RCC_CR |= RCC_CR_HSION;
    for (timeout = ADC_HSI_TIMEOUT; ((RCC_CR & RCC_CR_HSIRDY) == 0u) && (timeout != 0u); timeout--) {
    }
    if ((RCC_CR & RCC_CR_HSIRDY) == 0u) {
        adc_shutdown();
        return 0u;
    }

    RCC_APB1ENR |= RCC_APB1ENR_PWREN;
    RCC_APB2ENR |= RCC_APB2ENR_ADC1EN;

    ADC1_CR2 = 0u;
    ADC1_CR1 = 0u;
    ADC1_SMPR1 = 0x3FFFFFFFu;
    ADC1_SMPR2 = 0x3FFFFFFFu;
    ADC1_SMPR3 = 0x3FFFFFFFu;
    ADC1_SQR1 = 0u;
    ADC1_SQR2 = 0u;
    ADC1_SQR3 = 0u;
    ADC1_SQR4 = 0u;
    ADC1_SQR5 = 0u;

    ADC_CCR |= ADC_CCR_TSVREFE;
    for (timeout = ADC_WAIT_TIMEOUT; ((PWR_CSR & PWR_CSR_VREFINTRDYF) == 0u) && (timeout != 0u); timeout--) {
    }
    board_delay(1000u);

    ADC1_CR1 &= ~(ADC_CR1_PDD | ADC_CR1_PDI);
    ADC1_CR2 = ADC_CR2_ADON | ADC_CR2_EOCS;
    for (timeout = ADC_WAIT_TIMEOUT; ((ADC1_SR & ADC_SR_ADONS) == 0u) && (timeout != 0u); timeout--) {
    }

    if ((ADC1_SR & ADC_SR_ADONS) == 0u) {
        adc_shutdown();
        return 0u;
    }

    return 1u;
}

static uint32_t adc_read_raw(uint8_t channel, uint16_t *raw)
{
    uint32_t timeout;

    if (raw == 0) {
        return 0u;
    }

    (void)ADC1_DR;
    ADC1_SR = 0u;
    ADC1_SQR1 = 0u;
    ADC1_SQR2 = 0u;
    ADC1_SQR3 = 0u;
    ADC1_SQR4 = 0u;
    ADC1_SQR5 = ((uint32_t)channel & 0x1Fu);
    ADC1_CR2 |= ADC_CR2_SWSTART;

    for (timeout = ADC_WAIT_TIMEOUT; ((ADC1_SR & ADC_SR_EOC) == 0u) && (timeout != 0u); timeout--) {
    }
    if ((ADC1_SR & ADC_SR_EOC) == 0u) {
        return 0u;
    }

    *raw = (uint16_t)(ADC1_DR & 0x0FFFu);
    return 1u;
}

/* Discards the first conversion, then averages four. */
static uint32_t adc_read_average(uint8_t channel, uint16_t *raw)
{
    uint32_t sum = 0u;
    uint16_t sample = 0u;

    if (raw == 0) {
        return 0u;
    }

    if (adc_read_raw(channel, &sample) == 0u) {
        return 0u;
    }

    for (uint32_t i = 0u; i < 4u; i++) {
        if (adc_read_raw(channel, &sample) == 0u) {
            return 0u;
        }
        sum += sample;
    }

    *raw = (uint16_t)((sum + 2u) / 4u);
    return 1u;
}

static uint32_t adc_read_vdd(uint16_t *vref_raw_out, uint16_t *vdd_mv_out)
{
    uint16_t vref_raw;
    uint16_t vref_cal;
    uint32_t vdd_mv;

    if ((adc_read_average(ADC_VREFINT_CHANNEL, &vref_raw) == 0u) || (vref_raw == 0u)) {
        return 0u;
    }
    vref_cal = REG16(ADC_VREFINT_CAL_ADDR);
    if ((vref_cal == 0u) || (vref_cal == 0xFFFFu)) {
        return 0u;
    }

    vdd_mv = ((uint32_t)vref_cal * ADC_VREFINT_CAL_MV + (vref_raw / 2u)) / vref_raw;
    if (vdd_mv > 65535u) {
        vdd_mv = 65535u;
    }
    *vref_raw_out = vref_raw;
    *vdd_mv_out = (uint16_t)vdd_mv;
    return 1u;
}

static void adc_make_gpio_analog(uint32_t port, uint8_t pin, uint32_t *moder_save, uint32_t *pupdr_save)
{
    uint32_t shift = (uint32_t)pin * 2u;

    *moder_save = GPIO_MODER(port);
    *pupdr_save = GPIO_PUPDR(port);
    GPIO_MODER(port) = (GPIO_MODER(port) & ~(3u << shift)) | (3u << shift);
    GPIO_PUPDR(port) &= ~(3u << shift);
}

static void adc_restore_gpio(uint32_t port, uint32_t moder_save, uint32_t pupdr_save)
{
    GPIO_MODER(port) = moder_save;
    GPIO_PUPDR(port) = pupdr_save;
}

static uint16_t adc_raw_to_mv(uint16_t raw, uint16_t vdd_mv)
{
    uint32_t mv = ((uint32_t)raw * (uint32_t)vdd_mv + (ADC_FULL_SCALE / 2u)) / ADC_FULL_SCALE;

    if (mv > 65535u) {
        mv = 65535u;
    }
    return (uint16_t)mv;
}

uint32_t templogpro_hw_adc_read_vdd_mv(uint16_t *vdd_millivolts)
{
    uint16_t vref_raw;
    uint32_t result;

    if (vdd_millivolts == 0) {
        return 0u;
    }
    if (adc_init_once() == 0u) {
        return 0u;
    }
    result = adc_read_vdd(&vref_raw, vdd_millivolts);
    adc_shutdown();
    return result;
}

uint32_t templogpro_hw_adc_scan(TempLogProAdcScan *scan)
{
    if (scan == 0) {
        return 0u;
    }
    memset(scan, 0, sizeof(*scan));

    RCC_AHBENR |= (1u << 0) | (1u << 1) | (1u << 2); /* GPIOA/B/C */

    if (adc_init_once() == 0u) {
        return 0u;
    }
    if (adc_read_vdd(&scan->vrefint_raw, &scan->vdd_millivolts) == 0u) {
        adc_shutdown();
        return 0u;
    }

    scan->valid = 1u;
    scan->item_count = TEMPLOGPRO_ADC_SCAN_MAX_ITEMS;

    for (uint32_t i = 0u; i < TEMPLOGPRO_ADC_SCAN_MAX_ITEMS; i++) {
        uint32_t moder_save;
        uint32_t pupdr_save;
        uint16_t raw = 0u;

        adc_make_gpio_analog(ADC_SCAN_CANDIDATES[i].port,
                             ADC_SCAN_CANDIDATES[i].pin_no,
                             &moder_save,
                             &pupdr_save);
        board_delay(1000u);
        (void)adc_read_average(ADC_SCAN_CANDIDATES[i].channel, &raw);
        adc_restore_gpio(ADC_SCAN_CANDIDATES[i].port, moder_save, pupdr_save);

        scan->items[i].pin[0] = ADC_SCAN_CANDIDATES[i].pin[0];
        scan->items[i].pin[1] = ADC_SCAN_CANDIDATES[i].pin[1];
        scan->items[i].pin[2] = ADC_SCAN_CANDIDATES[i].pin[2];
        scan->items[i].pin[3] = '\0';
        scan->items[i].channel = ADC_SCAN_CANDIDATES[i].channel;
        scan->items[i].raw = raw;
        scan->items[i].millivolts = adc_raw_to_mv(raw, scan->vdd_millivolts);
    }

    adc_shutdown();
    return 1u;
}
