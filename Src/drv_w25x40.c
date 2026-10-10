/*
 * Winbond W25X40 (JEDEC EF 30 13, 512 KiB) on SPI1: PA4=CS, PA5=SCK,
 * PA6=MISO, PA7=MOSI. Between operations the chip sits in deep power-down
 * (B9h) and the SPI1 clock is gated. Every page program is read back and
 * compared.
 *
 * MSC file reads run in USB_LP_IRQHandler while the logger writes from the
 * main loop, so each public operation masks only IRQ20 (USB_LP) for its
 * duration; SysTick and the button EXTI lines keep running.
 */
#include <string.h>
#include "board.h"
#include "board_regs.h"
#include "templogpro_hw_bridge.h"

#define FLASH_CS_PORT       GPIOA_BASE
#define FLASH_CS_PIN        4u
#define FLASH_SCK_PIN       5u
#define FLASH_MISO_PIN      6u
#define FLASH_MOSI_PIN      7u

#define W25X40_SIZE_BYTES       (512u * 1024u)
#define W25X40_SECTOR_SIZE      4096u
#define W25X40_PAGE_SIZE        256u

#define W25X40_CMD_WRITE_ENABLE 0x06u
#define W25X40_CMD_READ_STATUS  0x05u
#define W25X40_CMD_READ_DATA    0x03u
#define W25X40_CMD_PAGE_PROGRAM 0x02u
#define W25X40_CMD_SECTOR_ERASE 0x20u
#define W25X40_CMD_JEDEC_ID     0x9Fu
#define W25X40_CMD_POWER_DOWN   0xB9u
#define W25X40_CMD_RELEASE_PD   0xABu

#define W25X40_STATUS_BUSY      0x01u
#define W25X40_STATUS_WEL       0x02u

#define W25X40_OK               0u
#define W25X40_ERR_RANGE        1u
#define W25X40_ERR_BUSY_TIMEOUT 2u
#define W25X40_ERR_WRITE_ENABLE 3u
#define W25X40_ERR_VERIFY       4u

/* Generous margins over the datasheet page-program and sector-erase times. */
#define W25X40_PROGRAM_TIMEOUT_MS  20u
#define W25X40_ERASE_TIMEOUT_MS    1000u
/* Upper bound on status polls per millisecond, in case SysTick is stopped. */
#define W25X40_POLLS_PER_MS        1000u
#define SPI1_TIMEOUT_LOOPS         100000u

static uint8_t g_w25x40_verify_buf[W25X40_PAGE_SIZE];
static volatile uint32_t g_hw_flash_busy;
/* Retain the first write failure for nonintrusive SWD inspection. */
static volatile uint32_t g_hw_flash_failure[4];
static volatile uint32_t g_spi1_timeout_error;
static uint32_t g_w25x40_powered_down;

static void flash_cs_high(void)
{
    GPIO_BSRR(FLASH_CS_PORT) = (1u << FLASH_CS_PIN);
}

static void flash_cs_low(void)
{
    GPIO_BSRR(FLASH_CS_PORT) = (1u << (FLASH_CS_PIN + 16u));
}

static uint32_t flash_range_is_valid(uint32_t address, uint32_t len)
{
    if (len == 0u) {
        return 1u;
    }
    if (address >= W25X40_SIZE_BYTES) {
        return 0u;
    }
    if (len > (W25X40_SIZE_BYTES - address)) {
        return 0u;
    }
    return 1u;
}

static uint8_t spi1_transfer(uint8_t value)
{
    uint32_t timeout = SPI1_TIMEOUT_LOOPS;

    while (((SPI1_SR & SPI_SR_TXE) == 0u) && (timeout != 0u)) {
        timeout--;
    }
    if (timeout == 0u) {
        g_spi1_timeout_error = 1u;
        return 0xFFu;
    }
    SPI1_DR8 = value;

    timeout = SPI1_TIMEOUT_LOOPS;
    while (((SPI1_SR & SPI_SR_RXNE) == 0u) && (timeout != 0u)) {
        timeout--;
    }
    if (timeout == 0u) {
        g_spi1_timeout_error = 1u;
        return 0xFFu;
    }
    return SPI1_DR8;
}

static void spi1_wait_idle(void)
{
    uint32_t timeout = SPI1_TIMEOUT_LOOPS;

    while (((SPI1_SR & SPI_SR_BSY) != 0u) && (timeout != 0u)) {
        timeout--;
    }
    if (timeout == 0u) {
        g_spi1_timeout_error = 1u;
    }
}

static void spi1_clear_timeout_error(void)
{
    g_spi1_timeout_error = 0u;
}

static uint32_t spi1_has_timeout_error(void)
{
    return (g_spi1_timeout_error != 0u) ? 1u : 0u;
}

static void spi1_clock_resume(void)
{
    RCC_APB2ENR |= RCC_APB2ENR_SPI1EN;
    (void)RCC_APB2ENR;
    SPI1_CR1 |= SPI_CR1_SPE;
}

static void spi1_clock_suspend(void)
{
    spi1_wait_idle();
    SPI1_CR1 &= ~SPI_CR1_SPE;
    RCC_APB2ENR &= ~RCC_APB2ENR_SPI1EN;
    (void)RCC_APB2ENR;
}

static void spi1_init(void)
{
    RCC_AHBENR |= (1u << 0);    /* GPIOA */
    RCC_APB2ENR |= RCC_APB2ENR_SPI1EN;

    flash_cs_high();
    GPIO_MODER(FLASH_CS_PORT) &= ~(3u << (FLASH_CS_PIN * 2u));
    GPIO_MODER(FLASH_CS_PORT) |=  (1u << (FLASH_CS_PIN * 2u));
    GPIO_OTYPER(FLASH_CS_PORT) &= ~(1u << FLASH_CS_PIN);
    GPIO_OSPEEDR(FLASH_CS_PORT) |= (3u << (FLASH_CS_PIN * 2u));
    GPIO_PUPDR(FLASH_CS_PORT) &= ~(3u << (FLASH_CS_PIN * 2u));
    flash_cs_high();

    board_gpio_af(GPIOA_BASE, (1u << FLASH_SCK_PIN) |
                              (1u << FLASH_MISO_PIN) |
                              (1u << FLASH_MOSI_PIN), BOARD_AF5_SPI1);

    SPI1_CR1 = 0u;
    SPI1_CR2 = 0u;
    (void)SPI1_DR;
    (void)SPI1_SR;

    SPI1_CR1 = SPI_CR1_MSTR |
               SPI_CR1_BR_0 |  /* PCLK/4, mode 0, MSB first, 8 bit */
               SPI_CR1_SSM |
               SPI_CR1_SSI |
               SPI_CR1_SPE;
}

static uint32_t w25x40_release_power_down(void)
{
    spi1_clock_resume();
    spi1_clear_timeout_error();
    flash_cs_low();
    (void)spi1_transfer(W25X40_CMD_RELEASE_PD);
    spi1_wait_idle();
    flash_cs_high();

    /* tRES1 is at most 3 us; this loop leaves margin at MSI 2.1 MHz and at
       32 MHz in the USB profile. */
    board_delay(256u);
    if (spi1_has_timeout_error() != 0u) {
        spi1_clock_suspend();
        return 0u;
    }

    g_w25x40_powered_down = 0u;
    return 1u;
}

static uint32_t w25x40_wake(void)
{
    if (g_w25x40_powered_down == 0u) {
        return 1u;
    }
    return w25x40_release_power_down();
}

static uint32_t w25x40_power_down(void)
{
    if (g_w25x40_powered_down != 0u) {
        spi1_clock_suspend();
        return 1u;
    }

    spi1_clear_timeout_error();
    flash_cs_low();
    (void)spi1_transfer(W25X40_CMD_POWER_DOWN);
    spi1_wait_idle();
    flash_cs_high();

    /* tDP is at most 3 us. */
    board_delay(256u);
    if (spi1_has_timeout_error() != 0u) {
        return 0u;
    }

    g_w25x40_powered_down = 1u;
    spi1_clock_suspend();
    return 1u;
}

static void w25x40_init(void)
{
    spi1_init();
    /* After an MCU-only reset the chip may still be in power-down. */
    g_w25x40_powered_down = 1u;
    (void)w25x40_release_power_down();
}

static void w25x40_send_address(uint32_t address)
{
    spi1_transfer((uint8_t)(address >> 16));
    spi1_transfer((uint8_t)(address >> 8));
    spi1_transfer((uint8_t)address);
}

static uint8_t w25x40_read_status(void)
{
    uint8_t status;

    spi1_clear_timeout_error();
    flash_cs_low();
    spi1_transfer(W25X40_CMD_READ_STATUS);
    status = spi1_transfer(0xFFu);
    spi1_wait_idle();
    flash_cs_high();

    if (spi1_has_timeout_error() != 0u) {
        return 0xFFu;
    }
    return status;
}

/* HAL_GetTick() counts milliseconds at both the 1 kHz (USB) and 100 Hz
   (battery) SysTick rates, so the timeout holds in either power profile. */
static uint32_t w25x40_wait_busy(uint32_t timeout_ms)
{
    uint32_t start_ms = HAL_GetTick();
    uint32_t polls_left = (timeout_ms + 1u) * W25X40_POLLS_PER_MS;

    for (;;) {
        uint8_t status = w25x40_read_status();
        if (spi1_has_timeout_error() != 0u) {
            return 0u;
        }
        if ((status & W25X40_STATUS_BUSY) == 0u) {
            return 1u;
        }
        if (((HAL_GetTick() - start_ms) > timeout_ms) || (polls_left == 0u)) {
            return 0u;
        }
        polls_left--;
    }
}

static uint32_t w25x40_write_enable(void)
{
    uint8_t status;

    spi1_clear_timeout_error();
    flash_cs_low();
    spi1_transfer(W25X40_CMD_WRITE_ENABLE);
    spi1_wait_idle();
    flash_cs_high();
    if (spi1_has_timeout_error() != 0u) {
        return 0u;
    }

    status = w25x40_read_status();
    if (spi1_has_timeout_error() != 0u) {
        return 0u;
    }
    return ((status & W25X40_STATUS_WEL) != 0u) ? 1u : 0u;
}

static uint32_t w25x40_read_jedec_id(uint8_t id[3])
{
    if (id == 0) {
        return W25X40_ERR_RANGE;
    }

    spi1_clear_timeout_error();
    flash_cs_low();
    spi1_transfer(W25X40_CMD_JEDEC_ID);
    id[0] = spi1_transfer(0xFFu);
    id[1] = spi1_transfer(0xFFu);
    id[2] = spi1_transfer(0xFFu);
    spi1_wait_idle();
    flash_cs_high();

    if (spi1_has_timeout_error() != 0u) {
        return W25X40_ERR_BUSY_TIMEOUT;
    }
    return W25X40_OK;
}

static uint32_t w25x40_read_data(uint32_t address, uint8_t *buf, uint32_t len)
{
    if (buf == 0 || !flash_range_is_valid(address, len)) {
        return W25X40_ERR_RANGE;
    }
    if (len == 0u) {
        return W25X40_OK;
    }

    if (!w25x40_wait_busy(W25X40_PROGRAM_TIMEOUT_MS)) {
        return W25X40_ERR_BUSY_TIMEOUT;
    }

    spi1_clear_timeout_error();
    flash_cs_low();
    spi1_transfer(W25X40_CMD_READ_DATA);
    w25x40_send_address(address);
    for (uint32_t i = 0; i < len; i++) {
        buf[i] = spi1_transfer(0xFFu);
    }
    spi1_wait_idle();
    flash_cs_high();

    if (spi1_has_timeout_error() != 0u) {
        return W25X40_ERR_BUSY_TIMEOUT;
    }
    return W25X40_OK;
}

static uint32_t w25x40_page_program(uint32_t address, const uint8_t *buf, uint32_t len)
{
    if (buf == 0 || len == 0u || len > W25X40_PAGE_SIZE ||
        ((address & (W25X40_PAGE_SIZE - 1u)) + len) > W25X40_PAGE_SIZE ||
        !flash_range_is_valid(address, len)) {
        return W25X40_ERR_RANGE;
    }

    if (!w25x40_wait_busy(W25X40_PROGRAM_TIMEOUT_MS)) {
        return W25X40_ERR_BUSY_TIMEOUT;
    }
    if (!w25x40_write_enable()) {
        return W25X40_ERR_WRITE_ENABLE;
    }

    spi1_clear_timeout_error();
    flash_cs_low();
    spi1_transfer(W25X40_CMD_PAGE_PROGRAM);
    w25x40_send_address(address);
    for (uint32_t i = 0; i < len; i++) {
        spi1_transfer(buf[i]);
    }
    spi1_wait_idle();
    flash_cs_high();
    if (spi1_has_timeout_error() != 0u) {
        return W25X40_ERR_BUSY_TIMEOUT;
    }

    if (!w25x40_wait_busy(W25X40_PROGRAM_TIMEOUT_MS)) {
        return W25X40_ERR_BUSY_TIMEOUT;
    }

    if (w25x40_read_data(address, g_w25x40_verify_buf, len) != W25X40_OK) {
        return W25X40_ERR_VERIFY;
    }
    if (memcmp(g_w25x40_verify_buf, buf, len) != 0) {
        return W25X40_ERR_VERIFY;
    }

    return W25X40_OK;
}

static uint32_t w25x40_write(uint32_t address, const uint8_t *buf, uint32_t len)
{
    if (buf == 0 || !flash_range_is_valid(address, len)) {
        return W25X40_ERR_RANGE;
    }

    while (len > 0u) {
        uint32_t page_left = W25X40_PAGE_SIZE - (address & (W25X40_PAGE_SIZE - 1u));
        uint32_t chunk = (len < page_left) ? len : page_left;
        uint32_t result = w25x40_page_program(address, buf, chunk);
        if (result != W25X40_OK) {
            return result;
        }
        address += chunk;
        buf += chunk;
        len -= chunk;
    }

    return W25X40_OK;
}

static uint32_t w25x40_sector_erase(uint32_t address)
{
    address &= ~(W25X40_SECTOR_SIZE - 1u);
    if (!flash_range_is_valid(address, W25X40_SECTOR_SIZE)) {
        return W25X40_ERR_RANGE;
    }

    if (!w25x40_wait_busy(W25X40_ERASE_TIMEOUT_MS)) {
        return W25X40_ERR_BUSY_TIMEOUT;
    }
    if (!w25x40_write_enable()) {
        return W25X40_ERR_WRITE_ENABLE;
    }

    spi1_clear_timeout_error();
    flash_cs_low();
    spi1_transfer(W25X40_CMD_SECTOR_ERASE);
    w25x40_send_address(address);
    spi1_wait_idle();
    flash_cs_high();
    if (spi1_has_timeout_error() != 0u) {
        return W25X40_ERR_BUSY_TIMEOUT;
    }

    if (!w25x40_wait_busy(W25X40_ERASE_TIMEOUT_MS)) {
        return W25X40_ERR_BUSY_TIMEOUT;
    }

    return W25X40_OK;
}

static uint32_t flash_lock_usb_irq(void)
{
    uint32_t enabled = NVIC_ISER0 & NVIC_USB_LP_BIT;
    NVIC_ICER0 = NVIC_USB_LP_BIT;
    __asm volatile("dsb\n\tisb" ::: "memory");
    return enabled;
}

static void flash_unlock_usb_irq(uint32_t enabled)
{
    if (enabled != 0u) {
        NVIC_ISER0 = NVIC_USB_LP_BIT;
    }
}

static void flash_record_failure(uint32_t kind, uint32_t address, uint32_t result)
{
    if ((result != W25X40_OK) && (g_hw_flash_failure[0] == 0u)) {
        g_hw_flash_failure[1] = address;
        g_hw_flash_failure[2] = result;
        g_hw_flash_failure[3] = HAL_GetTick();
        g_hw_flash_failure[0] = kind;
    }
}

uint32_t templogpro_hw_flash_init(void)
{
    uint8_t id[3];
    uint32_t result;
    uint32_t usb_irq_enabled = flash_lock_usb_irq();

    g_hw_flash_busy = 1u;
    w25x40_init();
    result = (w25x40_read_jedec_id(id) == W25X40_OK) ? 1u : 0u;
    (void)w25x40_power_down();
    g_hw_flash_busy = 0u;
    flash_unlock_usb_irq(usb_irq_enabled);
    return result;
}

uint32_t templogpro_hw_flash_read(uint32_t address, uint8_t *buf, uint32_t len)
{
    uint32_t result;
    uint32_t usb_irq_enabled = flash_lock_usb_irq();

    g_hw_flash_busy = 1u;
    if (w25x40_wake() == 0u) {
        result = W25X40_ERR_BUSY_TIMEOUT;
    } else {
        result = w25x40_read_data(address, buf, len);
        (void)w25x40_power_down();
    }
    g_hw_flash_busy = 0u;
    flash_unlock_usb_irq(usb_irq_enabled);
    return (result == W25X40_OK) ? 1u : 0u;
}

uint32_t templogpro_hw_flash_write(uint32_t address, const uint8_t *buf, uint32_t len)
{
    uint32_t result;
    uint32_t usb_irq_enabled = flash_lock_usb_irq();

    g_hw_flash_busy = 1u;
    if (w25x40_wake() == 0u) {
        result = W25X40_ERR_BUSY_TIMEOUT;
    } else {
        result = w25x40_write(address, buf, len);
        (void)w25x40_power_down();
    }
    flash_record_failure(1u, address, result);
    g_hw_flash_busy = 0u;
    flash_unlock_usb_irq(usb_irq_enabled);
    return (result == W25X40_OK) ? 1u : 0u;
}

uint32_t templogpro_hw_flash_erase_sector(uint32_t address)
{
    uint32_t result;
    uint32_t usb_irq_enabled = flash_lock_usb_irq();

    g_hw_flash_busy = 1u;
    if (w25x40_wake() == 0u) {
        result = W25X40_ERR_BUSY_TIMEOUT;
    } else {
        result = w25x40_sector_erase(address);
        (void)w25x40_power_down();
    }
    flash_record_failure(2u, address, result);
    g_hw_flash_busy = 0u;
    flash_unlock_usb_irq(usb_irq_enabled);
    return (result == W25X40_OK) ? 1u : 0u;
}

uint32_t templogpro_hw_flash_is_busy(void)
{
    return g_hw_flash_busy;
}
