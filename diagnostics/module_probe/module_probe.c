/* Reuse board-tested low-level drivers without the normal application: their
   static helpers are needed here, so the driver sources are included. The
   isolated link excludes logger, config, filesystem and RTC-init objects, and
   --gc-sections drops the unused flash write/erase entry points. */
#include "../../Src/board.c"
#include "../../Src/drv_lcd.c"
#include "../../Src/drv_sensor_i2c.c"
#include "../../Src/drv_w25x40.c"
#include "../../Src/drv_adc.c"
#include "templogpro_diag.h"
#include "templogpro_power.h"
#include "templogpro_usb.h"
#include "probe.h"
#include "sensor_test.h"
#include <stdio.h>
#include "sensor_bus.inc"

/* Keep the probe's deliberately raw register layer independent from the HAL
   headers, which use the same peripheral macro names as main.c. */
void HAL_SuspendTick(void);
void HAL_PWR_EnterSTOPMode(uint32_t regulator, uint8_t stop_entry);
#define PROBE_PWR_CR_LPSDSR       (1u << 0)
#define PROBE_PWR_CR_CWUF         (1u << 2)
#define PROBE_PWR_CR_ULP          (1u << 9)
#define PROBE_PWR_CR_FWU          (1u << 10)
#define PROBE_PWR_STOPENTRY_WFI   1u

#ifndef PROBE_INIT_LCD
#define PROBE_INIT_LCD 1
#endif
#ifndef PROBE_BARE
#define PROBE_BARE 0
#endif
#ifndef PROBE_BARE_STOP
#define PROBE_BARE_STOP 0
#endif
#ifndef PROBE_BARE_STOP_DEVICES
#define PROBE_BARE_STOP_DEVICES 0
#endif
#ifndef PROBE_BARE_STOP_RESCUE
#define PROBE_BARE_STOP_RESCUE 0
#endif

extern uint32_t SystemCoreClock;
static uint32_t probe_lcd_ready;
static uint32_t probe_lcd_last_on_ms;

/* PA0 and PA1 are the two active-low user buttons on the verified board.
   The rescue check is intentionally local to this diagnostic image so a
   STOP experiment can always be recovered without relying on NRST. */
static uint32_t probe_rescue_buttons_held(void)
{
    RCC_AHBENR |= (1u << 0);
    GPIO_MODER(GPIOA_BASE) &= ~((3u << (0u * 2u)) | (3u << (1u * 2u)));
    GPIO_PUPDR(GPIOA_BASE) &= ~((3u << (0u * 2u)) | (3u << (1u * 2u)));
    GPIO_PUPDR(GPIOA_BASE) |= (1u << (0u * 2u)) | (1u << (1u * 2u));

    uint32_t start = HAL_GetTick();
    while ((uint32_t)(HAL_GetTick() - start) < 1000u) {
        if ((GPIO_IDR(GPIOA_BASE) & 0x03u) != 0u) return 0u;
    }
    return 1u;
}

static void probe_lcd_suspend_for_current_test(void)
{
    if (probe_lcd_ready == 0u) return;
    LCD_CR = 0u;
    uint32_t timeout = 100000u;
    while (((LCD_SR & LCD_SR_ENS_BIT) != 0u) && timeout != 0u) timeout--;
    probe_lcd_ready = 0u;
}
static uint32_t probe_flash_ready;

static void emit_reg(ProbeEmit emit, const char *name, uint32_t value)
{
    char line[72];
    snprintf(line, sizeof(line), "%s=%08lX\r\n", name, (unsigned long)value);
    emit(line);
}

static void probe_lcd_init(void)
{
    uint32_t csr = RCC_CSR;
    uint32_t sel = csr & LCD_RCC_CSR_RTCSEL;
    /* NRST stops LSI. Resume an already selected LSI without touching the
       calendar, prescalers, clock selection or backup-domain reset. */
    if ((csr & LCD_RCC_CSR_RTCEN) && sel == LCD_RCC_CSR_RTCSEL_LSI) {
        RCC_CSR |= LCD_RCC_CSR_LSION;
        if (!lcd_wait_register(&RCC_CSR, LCD_RCC_CSR_LSIRDY, LCD_RCC_CSR_LSIRDY, LCD_CLOCK_TIMEOUT_MS)) return;
        csr = RCC_CSR;
    }
    if ((csr & LCD_RCC_CSR_RTCEN) == 0u ||
        !(((sel == LCD_RCC_CSR_RTCSEL_LSE) && (csr & LCD_RCC_CSR_LSERDY)) ||
          ((sel == LCD_RCC_CSR_RTCSEL_LSI) && (csr & LCD_RCC_CSR_LSIRDY)))) return;
    init_lcd_pins_bruteforce();
    RCC_APB1ENR |= (1u << 9);
    LCD_CR &= ~LCD_CR_ENABLE;
    lcd_clear_ram();
    LCD_CLR = LCD_SR_UDD_BIT;
    LCD_FCR = LCD_FCR_DIV31 | LCD_FCR_CONTRAST(3) | LCD_FCR_DEAD(0) | LCD_FCR_PON(5);
    if (!lcd_wait_register(&LCD_SR, LCD_SR_FCRSF_BIT, LCD_SR_FCRSF_BIT, LCD_READY_TIMEOUT_MS)) return;
    LCD_CR = LCD_CR_DUTY_BIAS | LCD_CR_MUX | LCD_CR_VSEL_INTERNAL | LCD_CR_ENABLE;
    if (!lcd_wait_register(&LCD_SR, LCD_SR_RDY_BIT, LCD_SR_RDY_BIT, LCD_READY_TIMEOUT_MS)) return;
    lcd_show_text3("dIA");
    probe_lcd_ready = 1;
    probe_lcd_last_on_ms = HAL_GetTick();
}

static void probe_sensor_read(ProbeEmit emit, uint8_t reg, uint32_t len)
{
    uint8_t bytes[2] = {0, 0};
    char line[96];
    uint32_t ok = (uint32_t)probe_bus_read_register(reg, bytes, len);
    snprintf(line, sizeof(line), "SENSOR reg=%02X len=%lu ack=%lu data=%02X%02X tick_ms=%lu\r\n",
             reg, (unsigned long)len, (unsigned long)ok, bytes[0], bytes[1], (unsigned long)HAL_GetTick());
    emit(line);
}

static void probe_sensor(ProbeEmit emit)
{
    RCC_AHBENR |= (1u << 1);
    i2c_bitbang_init_pins(GPIOB_BASE, 6, 7);
    emit("BUS=I2C_BITBANG PB6=SCL PB7=SDA ADDR=48 pointer_reads_only\r\n");
    probe_sensor_read(emit, 0, 2);
    probe_sensor_read(emit, 1, 1);
    probe_sensor_read(emit, 1, 2);
    probe_sensor_read(emit, 2, 2);
    probe_sensor_read(emit, 3, 2);
    /* Leave the sensor pointer at the temperature register, also after errors. */
    probe_sensor_read(emit, 0, 2);
}

static int probe_flash_init_once(void)
{
    if (!probe_flash_ready) {
        w25x40_init();
        uint8_t id[3] = {0, 0, 0};
        probe_flash_ready = (w25x40_read_jedec_id(id) == W25X40_OK) &&
                            id[0] == 0xEFu && id[1] == 0x30u && id[2] == 0x13u;
        (void)w25x40_power_down();
    }
    return probe_flash_ready != 0u;
}

static void probe_flash_info(ProbeEmit emit)
{
    char line[96];
    uint8_t id[3] = {0, 0, 0};
    w25x40_init();
    uint32_t ok = (w25x40_read_jedec_id(id) == W25X40_OK);
    uint8_t status = w25x40_read_status();
    uint32_t down = w25x40_power_down();
    snprintf(line, sizeof(line), "FLASH ok=%lu jedec=%02X%02X%02X status=%02X power_down=%lu\r\n",
             (unsigned long)ok, id[0], id[1], id[2], status, (unsigned long)down);
    emit(line);
    emit("BUS=SPI1 PA4=CS PA5=SCK PA6=MISO PA7=MOSI\r\n");
    emit_reg(emit, "SPI1_CR1_AFTER", SPI1_CR1);
    emit_reg(emit, "RCC_APB2ENR_AFTER", RCC_APB2ENR);
}

static void probe_flash_block(ProbeEmit emit, uint32_t address)
{
    uint8_t data[256];
    char line[536];
    if (!probe_flash_init_once() || !templogpro_hw_flash_read(address, data, sizeof(data))) {
        emit("ERROR flash_read_or_unexpected_id\r\n");
        return;
    }
    unsigned pos = (unsigned)snprintf(line, sizeof(line), "FLASH %06lX ", (unsigned long)address);
    const char hex[] = "0123456789ABCDEF";
    for (unsigned i = 0; i < sizeof(data); ++i) {
        line[pos++] = hex[data[i] >> 4];
        line[pos++] = hex[data[i] & 15u];
    }
    line[pos++] = '\r'; line[pos++] = '\n'; line[pos] = 0;
    emit(line);
}

void probe_command(const char *line, ProbeEmit emit)
{
    uint32_t address;
    emit("BEGIN\r\n");
    if (!strcmp(line, "PING")) {
        emit("TEMPLOGPRO_MODULE_PROBE_V2 BOUNDED_SENSOR_TEST LOGGER_DISABLED\r\n");
    } else if (!strcmp(line, "HELP")) {
        emit("PING | GET MCU | GET CLOCKS | GET GPIO A/B/C/D/E | GET RTC | GET SENSOR | GET FLASH | GET VDD | FLASH READ hhhhhh | SENSOR SLEEP/WAKE | TEST SENSOR | RESTORE SENSOR\r\n");
    } else if (!strcmp(line, "GET MCU")) {
        emit_reg(emit, "CPUID", REG32(0xE000ED00u));
        emit_reg(emit, "DBGMCU_IDCODE", REG32(0xE0042000u));
        emit_reg(emit, "DBGMCU_CR", REG32(0xE0042004u));
        emit_reg(emit, "FLASH_KIB", REG16(0x1FF800CCu));
        emit_reg(emit, "UID0", REG32(0x1FF800D0u));
        emit_reg(emit, "UID1", REG32(0x1FF800D4u));
        emit_reg(emit, "UID2", REG32(0x1FF800E4u));
        emit_reg(emit, "CORE_HZ", SystemCoreClock);
        emit_reg(emit, "LCD_READY", probe_lcd_ready);
    } else if (!strcmp(line, "GET CLOCKS")) {
        emit_reg(emit, "RCC_CR", RCC_CR);
        emit_reg(emit, "RCC_CFGR", RCC_CFGR);
        emit_reg(emit, "RCC_CSR", RCC_CSR);
        emit_reg(emit, "RCC_AHBENR", RCC_AHBENR);
        emit_reg(emit, "RCC_APB1ENR", RCC_APB1ENR);
        emit_reg(emit, "RCC_APB2ENR", RCC_APB2ENR);
        emit_reg(emit, "PWR_CR", PWR_CR);
        emit_reg(emit, "PWR_CSR", PWR_CSR);
        emit_reg(emit, "LCD_CR", LCD_CR);
        emit_reg(emit, "LCD_FCR", LCD_FCR);
        emit_reg(emit, "LCD_SR", LCD_SR);
        emit_reg(emit, "ADC_CR2", ADC1_CR2);
        emit_reg(emit, "ADC_CCR", ADC_CCR);
        emit_reg(emit, "SPI1_CR1", SPI1_CR1);
        emit_reg(emit, "SCB_SCR", REG32(0xE000ED10u));
    } else if (strlen(line) == 10u && !strncmp(line, "GET GPIO ", 9) && line[9] >= 'A' && line[9] <= 'E') {
        uint32_t base = GPIOA_BASE + (uint32_t)(line[9] - 'A') * 0x400u;
        emit_reg(emit, "PORT_BASE", base);
        emit_reg(emit, "MODER", GPIO_MODER(base));
        emit_reg(emit, "OTYPER", GPIO_OTYPER(base));
        emit_reg(emit, "PUPDR", GPIO_PUPDR(base));
        emit_reg(emit, "IDR", GPIO_IDR(base));
        emit_reg(emit, "ODR", GPIO_ODR(base));
        emit_reg(emit, "AFRL", GPIO_AFRL(base));
        emit_reg(emit, "AFRH", GPIO_AFRH(base));
    } else if (!strcmp(line, "GET RTC")) {
        uint32_t ssr = REG32(RTC_BASE + 0x28u);
        uint32_t tr = REG32(RTC_BASE);
        uint32_t dr = REG32(RTC_BASE + 4u);
        emit_reg(emit, "RTC_SSR", ssr);
        emit_reg(emit, "RTC_TR", tr);
        emit_reg(emit, "RTC_DR", dr);
        emit_reg(emit, "RTC_CR", REG32(RTC_BASE + 8u));
        emit_reg(emit, "RTC_ISR", REG32(RTC_BASE + 12u));
        emit_reg(emit, "RTC_PRER", REG32(RTC_BASE + 16u));
    } else if (!strcmp(line, "GET SENSOR")) {
        probe_sensor(emit);
    } else if (!strcmp(line, "SENSOR SLEEP")) {
        emit(probe_sensor_enter_sleep() ? "SENSOR shutdown_confirmed\r\n" : "ERROR sensor_sleep_not_confirmed\r\n");
    } else if (!strcmp(line, "SENSOR WAKE")) {
        emit(probe_sensor_recover() ? "SENSOR baseline_restored\r\n" : "ERROR SENSOR_RESTORE_FAILED\r\n");
    } else if (!strcmp(line, "TEST SENSOR")) {
        if (SystemCoreClock == 32000000u) probe_sensor_test(emit);
        else emit("ERROR test_requires_32MHz_USB_clock\r\n");
    } else if (!strcmp(line, "RESTORE SENSOR")) {
        emit(probe_sensor_recover() ? "SENSOR baseline_restored\r\n" : "ERROR SENSOR_RESTORE_FAILED\r\n");
    } else if (!strcmp(line, "GET FLASH")) {
        probe_flash_info(emit);
    } else if (!strcmp(line, "GET VDD")) {
        uint16_t mv = 0;
        emit_reg(emit, "VDD_OK", templogpro_hw_adc_read_vdd_mv(&mv));
        emit_reg(emit, "VDD_MV", mv);
    } else if (!strncmp(line, "FLASH READ ", 11) && probe_flash_address(line + 11, &address)) {
        probe_flash_block(emit, address);
    } else {
        emit("ERROR command_not_allowed\r\n");
    }
    emit("END\r\n");
}

int main(void)
{
#if PROBE_BARE_STOP_RESCUE
    HAL_Init();
    /* Hold both buttons during power-up to leave the MCU in a shallow WFI
       rescue loop. ST-Link can then attach normally and replace the image. */
    if (probe_rescue_buttons_held()) {
        for (;;) {
            __asm volatile ("dsb");
            __asm volatile ("wfi");
        }
    }
    /* Differential STOP test: explicitly put the two known external
       consumers into their lowest safe state before stopping the MCU. */
    (void)templogpro_power_battery_clock_config();
    w25x40_init();
    (void)w25x40_power_down();
    (void)probe_sensor_enter_sleep();
    LCD_CR = 0u;
    ADC1_CR2 = 0u;
    ADC_CCR = 0u;
    USB_CNTR = USB_CNTR_PDWN;
    RCC_APB1ENR &= ~RCC_APB1ENR_USBEN;
    HAL_SuspendTick();
    RCC_APB1ENR |= RCC_APB1ENR_PWREN;
    for (;;) {
        PWR_CR |= PROBE_PWR_CR_CWUF;
        PWR_CR |= PROBE_PWR_CR_ULP | PROBE_PWR_CR_FWU;
        HAL_PWR_EnterSTOPMode(PROBE_PWR_CR_LPSDSR, PROBE_PWR_STOPENTRY_WFI);
    }
#elif PROBE_BARE_STOP_DEVICES
    HAL_Init();
    /* Differential STOP test: explicitly put the two known external
       consumers into their lowest safe state before stopping the MCU. */
    (void)templogpro_power_battery_clock_config();
    w25x40_init();
    (void)w25x40_power_down();
    (void)probe_sensor_enter_sleep();
    LCD_CR = 0u;
    ADC1_CR2 = 0u;
    ADC_CCR = 0u;
    USB_CNTR = USB_CNTR_PDWN;
    RCC_APB1ENR &= ~RCC_APB1ENR_USBEN;
    HAL_SuspendTick();
    RCC_APB1ENR |= RCC_APB1ENR_PWREN;
    for (;;) {
        PWR_CR |= PROBE_PWR_CR_CWUF;
        PWR_CR |= PROBE_PWR_CR_ULP | PROBE_PWR_CR_FWU;
        HAL_PWR_EnterSTOPMode(PROBE_PWR_CR_LPSDSR, PROBE_PWR_STOPENTRY_WFI);
    }
#elif PROBE_BARE_STOP
    HAL_Init();
    /* Controlled MCU STOP baseline. No USB, LCD, sensor, SPI, ADC or logger
       is initialized. The existing RTC/LSI state is intentionally preserved. */
    (void)templogpro_power_battery_clock_config();
    RCC_APB1ENR |= RCC_APB1ENR_PWREN;
    HAL_SuspendTick();
    for (;;) {
        PWR_CR |= PROBE_PWR_CR_CWUF;
        PWR_CR |= PROBE_PWR_CR_ULP | PROBE_PWR_CR_FWU;
        HAL_PWR_EnterSTOPMode(PROBE_PWR_CR_LPSDSR, PROBE_PWR_STOPENTRY_WFI);
    }
#elif PROBE_BARE
    HAL_Init();
    (void)templogpro_power_battery_clock_config();
    for (;;) {
        __asm volatile ("dsb");
        __asm volatile ("wfi");
    }
#else
    HAL_Init();
    templogpro_diag_init();
    board_leds_init();
    board_led_green_off();
    board_led_red_off();
#if PROBE_INIT_LCD
    probe_lcd_init();
#endif
    (void)templogpro_usb_st_msc_init();
    /* Do not auto-wake the sensor: USB removal can reset the MCU, and the
       shutdown-current experiment must survive that reset. Use SENSOR WAKE
       explicitly before returning to the main firmware. */
    for (;;) {
        templogpro_usb_st_msc_process();
        if (probe_lcd_ready != 0u &&
            (uint32_t)(HAL_GetTick() - probe_lcd_last_on_ms) >= 10000u) {
            probe_lcd_suspend_for_current_test();
        }
        board_led_green_off();
        board_led_red_off();
        probe_sensor_recovery_poll();
        templogpro_power_enter_idle();
    }
#endif
}
