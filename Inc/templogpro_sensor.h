#ifndef TEMPLOGPRO_SENSOR_H
#define TEMPLOGPRO_SENSOR_H

#include <stdint.h>

uint32_t templogpro_sensor_init(void);
#define TEMPLOGPRO_SENSOR_WAIT  0u
#define TEMPLOGPRO_SENSOR_READY 1u
#define TEMPLOGPRO_SENSOR_ERROR 2u
uint32_t templogpro_sensor_request(void);
uint32_t templogpro_sensor_poll(int32_t *temperature_x256);
uint32_t templogpro_sensor_pending(void);
void templogpro_sensor_cancel(void);

#endif
