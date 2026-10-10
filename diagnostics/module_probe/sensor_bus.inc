/* A diagnostic-only, conservative bit-banged bus on the verified PB6/PB7.
   At USB's 32 MHz the delay is substantially slower than 100 kHz I2C, but fast
   enough to observe a roughly 10 ms conversion. Slower CPU only lengthens it. */
static void probe_bus_delay(void) { board_delay(64u); }

static int probe_bus_high(void)
{
    i2c_scl_high();
    uint32_t start = HAL_GetTick();
    for (unsigned i = 0; i < 20000u; ++i) {
        if (GPIO_IDR(GPIOB_BASE) & (1u << 6)) { probe_bus_delay(); return 1; }
        if ((uint32_t)(HAL_GetTick() - start) >= 2u) break;
    }
    return 0;
}

static void probe_bus_stop(void)
{
    i2c_scl_low();
    i2c_sda_low();
    probe_bus_delay();
    (void)probe_bus_high();
    i2c_sda_high();
    probe_bus_delay();
}

static int probe_bus_start(void)
{
    i2c_sda_high();
    probe_bus_delay();
    if (!probe_bus_high() || !i2c_sda_read()) return 0;
    i2c_sda_low();
    probe_bus_delay();
    i2c_scl_low();
    return 1;
}

static int probe_bus_write(uint8_t value)
{
    for (unsigned i = 0; i < 8; ++i) {
        if (value & 0x80u) i2c_sda_high(); else i2c_sda_low();
        value <<= 1;
        probe_bus_delay();
        if (!probe_bus_high()) return 0;
        i2c_scl_low();
        probe_bus_delay();
    }
    i2c_sda_high();
    probe_bus_delay();
    if (!probe_bus_high()) return 0;
    int ack = !i2c_sda_read();
    i2c_scl_low();
    probe_bus_delay();
    return ack;
}

static int probe_bus_read(uint8_t *value, int ack)
{
    uint8_t data = 0;
    i2c_sda_high();
    for (unsigned i = 0; i < 8; ++i) {
        probe_bus_delay();
        if (!probe_bus_high()) return 0;
        data = (uint8_t)((data << 1) | i2c_sda_read());
        i2c_scl_low();
        probe_bus_delay();
    }
    if (ack) i2c_sda_low(); else i2c_sda_high();
    probe_bus_delay();
    if (!probe_bus_high()) return 0;
    i2c_scl_low();
    i2c_sda_high();
    probe_bus_delay();
    *value = data;
    return 1;
}

/* Pointer write, repeated start, then `len` bytes; reads only. */
static int probe_bus_read_register(uint8_t reg, uint8_t *buf, uint32_t len)
{
    int ok = probe_bus_start() && probe_bus_write(SENSOR_ADDR << 1) && probe_bus_write(reg) &&
             probe_bus_start() && probe_bus_write((SENSOR_ADDR << 1) | 1u);
    for (uint32_t i = 0; ok && (i < len); ++i) {
        ok = probe_bus_read(&buf[i], (i + 1u) < len);
    }
    probe_bus_stop();
    return ok;
}

int probe_sensor_read_word(uint8_t reg, uint16_t *value)
{
    uint8_t high, low;
    if (reg > 3u) return 0;
    RCC_AHBENR |= (1u << 1);
    i2c_bitbang_init_pins(GPIOB_BASE, 6, 7);
    int ok = probe_bus_start() && probe_bus_write(SENSOR_ADDR << 1) && probe_bus_write(reg) &&
             probe_bus_start() && probe_bus_write((SENSOR_ADDR << 1) | 1u) &&
             probe_bus_read(&high, 1) && probe_bus_read(&low, 0);
    probe_bus_stop();
    if (ok) *value = (uint16_t)((high << 8) | low);
    return ok;
}

int probe_sensor_write_config(uint16_t value)
{
    /* Three fixed values only: restore, shutdown, one-shot. */
    if (value != 0x60A0u && value != 0x61A0u && value != 0xE1A0u) return 0;
    int ok = probe_bus_start() && probe_bus_write(SENSOR_ADDR << 1) && probe_bus_write(1u) &&
             probe_bus_write((uint8_t)(value >> 8)) && probe_bus_write((uint8_t)value);
    probe_bus_stop();
    return ok;
}

uint32_t probe_sensor_tick(void) { return HAL_GetTick(); }
void probe_sensor_wait(uint32_t ms)
{
    uint32_t start = HAL_GetTick();
    while ((uint32_t)(HAL_GetTick() - start) < ms) {}
}
