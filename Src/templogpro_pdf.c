#include "templogpro_pdf.h"
#include <string.h>

uint32_t templogpro_manual_pdf_get_size(void)
{
    return 0u;
}

uint32_t templogpro_manual_pdf_read(uint32_t file_offset, uint8_t *destination, uint32_t requested_length)
{
    (void)file_offset;
    if ((destination != 0) && (requested_length != 0u)) {
        memset(destination, 0, requested_length);
    }
    return 0u;
}

uint32_t templogpro_report_pdf_get_size(void)
{
    return 0u;
}

uint32_t templogpro_report_pdf_read(uint32_t file_offset, uint8_t *destination, uint32_t requested_length)
{
    (void)file_offset;
    if ((destination != 0) && (requested_length != 0u)) {
        memset(destination, 0, requested_length);
    }
    return 0u;
}

uint32_t templogpro_data_pdf_get_size(void)
{
    return 0u;
}

uint32_t templogpro_data_pdf_read(uint32_t file_offset, uint8_t *destination, uint32_t requested_length)
{
    (void)file_offset;
    if ((destination != 0) && (requested_length != 0u)) {
        memset(destination, 0, requested_length);
    }
    return 0u;
}
