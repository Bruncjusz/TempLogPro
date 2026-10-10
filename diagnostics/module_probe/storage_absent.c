#include "templogpro_usb_msc_storage.h"

static int8_t init(uint8_t lun) { (void)lun; return 0; }
static int8_t capacity(uint8_t lun, uint32_t *count, uint16_t *size)
{
    (void)lun;
    *count = 0; *size = 512;
    return -1;
}
static int8_t ready(uint8_t lun) { (void)lun; return -1; }
static int8_t protected(uint8_t lun) { (void)lun; return 1; }
static int8_t access_denied(uint8_t lun, uint8_t *data, uint32_t address, uint16_t count)
{
    (void)lun; (void)data; (void)address; (void)count;
    return -1;
}
static int8_t max_lun(void) { return 0; }
static int8_t inquiry[36] = {
    0, 0x80, 2, 2, 31, 0, 0, 0,
    'T','e','m','p','T','a','l','e',
    'P','R','O','B','E',' ','N','O',' ','M','E','D','I','A',' ',' ',
    '0','0','0','1'
};
USBD_StorageTypeDef TEMPLOGPRO_MSC_STORAGE = {
    init, capacity, ready, protected, access_denied, access_denied, max_lun, inquiry
};
void templogpro_usb_msc_storage_process(void) {}
