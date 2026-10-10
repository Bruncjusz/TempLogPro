#include "templogpro_sensor.h"
#include "templogpro_diag.h"
#include "templogpro_hw_bridge.h"

extern uint32_t HAL_GetTick(void);

#define SENSOR_BASELINE     0x60A0u
#define SENSOR_SHUTDOWN     0x0100u
#define SENSOR_ONE_SHOT     0x8000u
#define SENSOR_STATE_MASK   0x7FDFu /* OS and ALERT are read-only status. */
#define SENSOR_POLL_MS      10u
#define SENSOR_TIMEOUT_MS   200u

static uint32_t g_sensor_initialized;
static uint32_t g_sensor_pending;
static uint32_t g_sensor_start_ms;
static uint32_t g_sensor_poll_ms;

static uint32_t sensor_family(uint16_t cfg)
{
    return (cfg & (SENSOR_STATE_MASK & ~SENSOR_SHUTDOWN)) ==
           (SENSOR_BASELINE & (SENSOR_STATE_MASK & ~SENSOR_SHUTDOWN));
}

static uint32_t sensor_shutdown(void)
{
    uint16_t cfg;
    /* Only the board-verified configuration is writable. Never change ALERT
       thresholds or guess a shutdown bit for an unidentified replacement. */
    return templogpro_hw_sensor_read_register(1u, &cfg) && sensor_family(cfg) &&
           templogpro_hw_sensor_write_config(SENSOR_BASELINE | SENSOR_SHUTDOWN) &&
           templogpro_hw_sensor_read_register(1u, &cfg) &&
           ((cfg & SENSOR_STATE_MASK) == ((SENSOR_BASELINE | SENSOR_SHUTDOWN) & SENSOR_STATE_MASK));
}

static uint32_t sensor_error(void)
{
    /* A NACK may follow a partial write. Request shutdown once on error;
       a failed cleanup is retried at the next scheduled measurement. */
    (void)sensor_shutdown();
    g_sensor_pending = 0u;
    g_sensor_initialized = 0u;
    templogpro_diag_set_sensor(0u);
    templogpro_diag_set_error(TEMPLOGPRO_ERROR_SENSOR);
    return 0u;
}

uint32_t templogpro_sensor_init(void)
{
    g_sensor_pending = 0u;
    g_sensor_initialized = templogpro_hw_sensor_init() && sensor_shutdown();
    templogpro_diag_set_sensor(g_sensor_initialized);
    if (!g_sensor_initialized) templogpro_diag_set_error(TEMPLOGPRO_ERROR_SENSOR);
    return g_sensor_initialized;
}

uint32_t templogpro_sensor_request(void)
{
    uint16_t cfg;
    if (g_sensor_pending) return 0u;
    if (!g_sensor_initialized && !templogpro_sensor_init()) return 0u;
    if (!templogpro_hw_sensor_read_register(1u, &cfg) ||
        !sensor_family(cfg) || !(cfg & SENSOR_SHUTDOWN) || !(cfg & SENSOR_ONE_SHOT)) {
        return sensor_error();
    }
    if (!templogpro_hw_sensor_write_config(SENSOR_BASELINE | SENSOR_SHUTDOWN | SENSOR_ONE_SHOT)) {
        return sensor_error();
    }
    g_sensor_start_ms = HAL_GetTick();
    g_sensor_poll_ms = g_sensor_start_ms;
    g_sensor_pending = 1u;
    return 1u;
}

uint32_t templogpro_sensor_poll(int32_t *temperature_x256)
{
    uint16_t cfg;
    uint32_t now = HAL_GetTick();
    if (!g_sensor_pending) return TEMPLOGPRO_SENSOR_WAIT;
    if (!temperature_x256 || (uint32_t)(now - g_sensor_start_ms) >= SENSOR_TIMEOUT_MS) {
        (void)sensor_error();
        return TEMPLOGPRO_SENSOR_ERROR;
    }
    if ((uint32_t)(now - g_sensor_poll_ms) < SENSOR_POLL_MS) return TEMPLOGPRO_SENSOR_WAIT;
    g_sensor_poll_ms = now;
    if (!templogpro_hw_sensor_read_register(1u, &cfg) ||
        (cfg & SENSOR_STATE_MASK) != ((SENSOR_BASELINE | SENSOR_SHUTDOWN) & SENSOR_STATE_MASK)) {
        (void)sensor_error();
        return TEMPLOGPRO_SENSOR_ERROR;
    }
    if (!(cfg & SENSOR_ONE_SHOT)) return TEMPLOGPRO_SENSOR_WAIT;
    if (!templogpro_hw_sensor_read_x256(temperature_x256)) {
        (void)sensor_error();
        return TEMPLOGPRO_SENSOR_ERROR;
    }
    /* SD remains set: conversion completion returns the sensor to shutdown. */
    g_sensor_pending = 0u;
    templogpro_diag_set_sensor(1u);
    return TEMPLOGPRO_SENSOR_READY;
}

uint32_t templogpro_sensor_pending(void) { return g_sensor_pending; }

void templogpro_sensor_cancel(void)
{
    if (g_sensor_pending) {
        if (!sensor_shutdown()) (void)sensor_error();
        g_sensor_pending = 0u;
    }
}
