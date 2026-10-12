#ifndef TEMPLOGPRO_USB_H
#define TEMPLOGPRO_USB_H

#include <stdint.h>

uint32_t templogpro_usb_st_msc_init(void);
void templogpro_usb_st_msc_process(void);
uint32_t templogpro_usb_st_work_pending(void);
/* STOP mode: wake on USB cable insertion (D+ pulled low by the host). */
uint32_t templogpro_usb_arm_cable_wake(void);
uint32_t templogpro_usb_disarm_cable_wake(void);
void templogpro_usb_request_probe(void);

#endif
