#ifndef PROBE_SENSOR_TEST_H
#define PROBE_SENSOR_TEST_H
#include "probe.h"

int probe_sensor_read_word(uint8_t reg, uint16_t *value);
int probe_sensor_write_config(uint16_t value);
uint32_t probe_sensor_tick(void);
void probe_sensor_wait(uint32_t ms);
void probe_sensor_test(ProbeEmit emit);
int probe_sensor_recover(void);
int probe_sensor_enter_sleep(void);
void probe_sensor_recovery_poll(void);
#endif
