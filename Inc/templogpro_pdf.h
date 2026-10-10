#ifndef TEMPLOGPRO_PDF_H
#define TEMPLOGPRO_PDF_H

#include <stdint.h>

/* Legacy entry points kept only so old CubeIDE build metadata stays harmless.
 * Current firmware exports PC-side PDF through TEMPLOGPRO.HTML, not MCU PDF files.
 */
uint32_t templogpro_manual_pdf_get_size(void);
uint32_t templogpro_manual_pdf_read(uint32_t file_offset, uint8_t *destination, uint32_t requested_length);
uint32_t templogpro_report_pdf_get_size(void);
uint32_t templogpro_report_pdf_read(uint32_t file_offset, uint8_t *destination, uint32_t requested_length);
uint32_t templogpro_data_pdf_get_size(void);
uint32_t templogpro_data_pdf_read(uint32_t file_offset, uint8_t *destination, uint32_t requested_length);

#endif
