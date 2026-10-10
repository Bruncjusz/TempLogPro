/*
 * Temperature sensor at I2C address 0x48 (TMP102/TMP112 compatible), software
 * I2C on PB6=SCL / PB7=SDA (open drain, internal pull-ups). The bounded
 * transactions come from the verified module probe; every wait for SCL is
 * limited, so a stuck bus fails instead of hanging.
 */
#include "board.h"
#include "board_regs.h"
#include "templogpro_hw_bridge.h"

#define SENSOR_ADDR   0x48u

extern uint32_t SystemCoreClock;

static uint32_t g_i2c_port, g_scl_pin, g_sda_pin;

static void i2c_bitbang_init_pins(uint32_t port, uint32_t scl_pin, uint32_t sda_pin)
{
    g_i2c_port = port;
    g_scl_pin = scl_pin;
    g_sda_pin = sda_pin;

    uint32_t mask = (1u << scl_pin) | (1u << sda_pin);
    for (uint32_t pin = 0; pin < 16; pin++) {
        if ((mask & (1u << pin)) == 0) {
            continue;
        }
        GPIO_MODER(port) &= ~(3u << (pin * 2u));
        GPIO_MODER(port) |=  (1u << (pin * 2u));   /* output */
        GPIO_OTYPER(port) |= (1u << pin);           /* open drain */
        GPIO_OSPEEDR(port) |= (3u << (pin * 2u));
        GPIO_PUPDR(port) &= ~(3u << (pin * 2u));
        GPIO_PUPDR(port) |=  (1u << (pin * 2u));    /* weak internal pull-up */
    }
    GPIO_BSRR(port) = mask; /* release both lines */
}

static void i2c_scl_high(void) { GPIO_BSRR(g_i2c_port) = (1u << g_scl_pin); }
static void i2c_scl_low(void)  { GPIO_BSRR(g_i2c_port) = (1u << (g_scl_pin + 16u)); }
static void i2c_sda_high(void) { GPIO_BSRR(g_i2c_port) = (1u << g_sda_pin); }
static void i2c_sda_low(void)  { GPIO_BSRR(g_i2c_port) = (1u << (g_sda_pin + 16u)); }
static uint32_t i2c_sda_read(void) { return (GPIO_IDR(g_i2c_port) >> g_sda_pin) & 1u; }

/* Keeps the conservative USB-clock timing when running from the battery clock. */
static void sensor_bus_delay(void)
{
    board_delay((SystemCoreClock / 1000000u) * 2u + 1u);
}

static uint32_t sensor_bus_high(void)
{
    i2c_scl_high();
    uint32_t start = HAL_GetTick();
    for (uint32_t i = 0; i < 20000u; ++i) {
        if (GPIO_IDR(GPIOB_BASE) & (1u << 6)) { sensor_bus_delay(); return 1u; }
        if ((uint32_t)(HAL_GetTick() - start) >= 2u) break;
    }
    return 0u;
}

static void sensor_bus_stop(void)
{
    i2c_scl_low();
    i2c_sda_low();
    sensor_bus_delay();
    (void)sensor_bus_high();
    i2c_sda_high();
    sensor_bus_delay();
}

static uint32_t sensor_bus_start(void)
{
    i2c_sda_high();
    sensor_bus_delay();
    if (!sensor_bus_high() || !i2c_sda_read()) return 0u;
    i2c_sda_low();
    sensor_bus_delay();
    i2c_scl_low();
    return 1u;
}

static uint32_t sensor_bus_write(uint8_t value)
{
    for (uint32_t i = 0; i < 8u; ++i) {
        if (value & 0x80u) i2c_sda_high(); else i2c_sda_low();
        value <<= 1;
        sensor_bus_delay();
        if (!sensor_bus_high()) return 0u;
        i2c_scl_low();
        sensor_bus_delay();
    }
    i2c_sda_high();
    sensor_bus_delay();
    if (!sensor_bus_high()) return 0u;
    uint32_t ack = !i2c_sda_read();
    i2c_scl_low();
    sensor_bus_delay();
    return ack;
}

static uint32_t sensor_bus_read(uint8_t *value, uint32_t ack)
{
    uint8_t data = 0u;
    i2c_sda_high();
    for (uint32_t i = 0; i < 8u; ++i) {
        sensor_bus_delay();
        if (!sensor_bus_high()) return 0u;
        data = (uint8_t)((data << 1) | i2c_sda_read());
        i2c_scl_low();
        sensor_bus_delay();
    }
    if (ack) i2c_sda_low(); else i2c_sda_high();
    sensor_bus_delay();
    if (!sensor_bus_high()) return 0u;
    i2c_scl_low();
    i2c_sda_high();
    sensor_bus_delay();
    *value = data;
    return 1u;
}

uint32_t templogpro_hw_sensor_init(void)
{
    RCC_AHBENR |= (1u << 1);
    i2c_bitbang_init_pins(GPIOB_BASE, 6u, 7u);
    return 1u;
}

uint32_t templogpro_hw_sensor_read_register(uint8_t reg, uint16_t *value)
{
    uint8_t high, low;
    if (reg > 3u || !value) return 0u;
    uint32_t ok = sensor_bus_start() && sensor_bus_write(SENSOR_ADDR << 1) && sensor_bus_write(reg) &&
                  sensor_bus_start() && sensor_bus_write((SENSOR_ADDR << 1) | 1u) &&
                  sensor_bus_read(&high, 1u) && sensor_bus_read(&low, 0u);
    sensor_bus_stop();
    if (ok) *value = (uint16_t)((high << 8) | low);
    return ok;
}

/* Only the two known values are allowed: shutdown (0x61A0) and one-shot (0xE1A0). */
uint32_t templogpro_hw_sensor_write_config(uint16_t value)
{
    if (value != 0x61A0u && value != 0xE1A0u) return 0u;
    uint32_t ok = sensor_bus_start() && sensor_bus_write(SENSOR_ADDR << 1) && sensor_bus_write(1u) &&
                  sensor_bus_write((uint8_t)(value >> 8)) && sensor_bus_write((uint8_t)value);
    sensor_bus_stop();
    return ok;
}

/* Register 0 holds Q8.8 for the whole LM75 family: the raw value is temp x256. */
uint32_t templogpro_hw_sensor_read_x256(int32_t *temperature_x256)
{
    uint16_t raw;
    if ((temperature_x256 == 0) || !templogpro_hw_sensor_read_register(0u, &raw)) {
        return 0u;
    }
    *temperature_x256 = (int16_t)raw;
    return 1u;
}
