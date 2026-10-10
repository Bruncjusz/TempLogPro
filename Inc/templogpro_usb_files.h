#ifndef TEMPLOGPRO_USB_FILES_H
#define TEMPLOGPRO_USB_FILES_H

#include <stdint.h>

#define TEMPLOGPRO_STATUS_FILE_SIZE       512u
#define TEMPLOGPRO_ADC_FILE_SIZE          512u
#define TEMPLOGPRO_POWER_FILE_SIZE       1024u
#define TEMPLOGPRO_README_FILE_SIZE       2048u

void templogpro_usb_files_snapshot_reset(void);
void templogpro_usb_files_snapshot_ensure(void);
uint32_t templogpro_html_get_size(void);
uint32_t templogpro_html_read(uint32_t file_offset, uint8_t *destination, uint32_t requested_length);
uint32_t templogpro_readme_get_size(void);
uint32_t templogpro_readme_read(uint32_t file_offset, uint8_t *destination, uint32_t requested_length);
uint32_t templogpro_status_get_size(void);
uint32_t templogpro_status_read(uint32_t file_offset, uint8_t *destination, uint32_t requested_length);
uint32_t templogpro_adc_get_size(void);
uint32_t templogpro_adc_read(uint32_t file_offset, uint8_t *destination, uint32_t requested_length);
uint32_t templogpro_power_get_size(void);
uint32_t templogpro_power_read(uint32_t file_offset, uint8_t *destination, uint32_t requested_length);
uint32_t templogpro_time_get_size(void);
uint32_t templogpro_time_read(uint32_t file_offset, uint8_t *destination, uint32_t requested_length);
uint32_t templogpro_csv_get_size(void);
uint32_t templogpro_csv_read(uint32_t file_offset, uint8_t *destination, uint32_t requested_length);
const char *templogpro_alarm_code_for_flags(uint16_t flags);

#endif
