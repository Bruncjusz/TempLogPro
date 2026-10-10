#ifndef TEMPLOGPRO_USB_H
#define TEMPLOGPRO_USB_H

#include <stdint.h>

uint32_t templogpro_usb_st_msc_init(void);
void templogpro_usb_st_msc_process(void);
uint32_t templogpro_usb_st_work_pending(void);

#endif
