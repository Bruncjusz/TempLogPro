#ifndef TEMPLOGPRO_USB_MSC_STORAGE_H
#define TEMPLOGPRO_USB_MSC_STORAGE_H

#include "usbd_msc.h"

extern USBD_StorageTypeDef TEMPLOGPRO_MSC_STORAGE;

void templogpro_usb_msc_storage_process(void);
void templogpro_usb_msc_storage_layout_reset(void);
uint32_t templogpro_usb_msc_config_write_status(void);
uint32_t templogpro_usb_msc_time_write_status(void);

#endif
