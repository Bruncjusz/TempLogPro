#define USBD_MSC_CDC_Process templogpro_normal_cdc_process_unused
#include "../../Src/st_usb/msc/usbd_msc.c"
#undef USBD_MSC_CDC_Process
#include "probe.h"

static USBD_HandleTypeDef *probe_usb_device;

static void probe_usb_emit(const char *text)
{
    cdc_queue_text(probe_usb_device, text);
}

void USBD_MSC_CDC_Process(USBD_HandleTypeDef *pdev)
{
    uint8_t ch;
    probe_usb_device = pdev;
    /* One response at a time; each response is bounded below the TX capacity. */
    if (cdc_tx_busy || cdc_tx_head != cdc_tx_tail) {
        cdc_tx_kick(pdev);
        return;
    }
    while (cdc_pop_rx(&ch)) {
        if (ch == '\r' || ch == '\n') {
            if (cdc_config_overflow) {
                probe_usb_emit("BEGIN\r\nERROR invalid_or_long_line\r\nEND\r\n");
            } else if (cdc_line_len) {
                cdc_line[cdc_line_len] = 0;
                probe_command(cdc_line, probe_usb_emit);
            }
            cdc_line_len = 0;
            cdc_config_overflow = 0;
            break;
        }
        if (ch < 32u || ch > 126u || cdc_line_len >= CDC_LINE_SIZE - 1u) {
            cdc_config_overflow = 1;
        } else if (!cdc_config_overflow) {
            cdc_line[cdc_line_len++] = (char)ch;
        }
    }
    cdc_tx_kick(pdev);
}
