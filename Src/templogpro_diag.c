#include "templogpro_diag.h"

#define REG32(a)                    (*(volatile uint32_t *)(a))

#define RCC_CSR_REG                 REG32(0x40023834u)
#define RCC_APB1ENR_REG             REG32(0x40023824u)
#define PWR_CR_REG                  REG32(0x40007000u)
#define RTC_BKP1R_REG               REG32(0x40002854u)

#define RCC_APB1ENR_PWREN_BIT       (1u << 28)
#define PWR_CR_DBP_BIT              (1u << 8)
#define RCC_CSR_RMVF_BIT            (1u << 24)
#define RCC_CSR_RESET_FLAGS_MASK    0xFE000000u

static TempLogProDiagnostics g_diag;
static uint32_t g_boot_capture_done;

void templogpro_diag_init(void)
{
    g_diag.rtc_ok = 0u;
    g_diag.sensor_ok = 0u;
    g_diag.flash_ok = 0u;
    g_diag.usb_ok = 0u;
    g_diag.usb_vbus_present = 0u;
    g_diag.last_error = TEMPLOGPRO_ERROR_NONE;
    g_diag.logger_state = 0u;
    g_diag.record_count = 0u;
}

void templogpro_diag_boot_capture_reset(void)
{
    if (g_boot_capture_done != 0u) {
        return;
    }

    RCC_APB1ENR_REG |= RCC_APB1ENR_PWREN_BIT;
    PWR_CR_REG |= PWR_CR_DBP_BIT;

    g_diag.reset_flags = RCC_CSR_REG & RCC_CSR_RESET_FLAGS_MASK;
    g_diag.last_boot_stage = RTC_BKP1R_REG;
    RCC_CSR_REG |= RCC_CSR_RMVF_BIT;
    g_boot_capture_done = 1u;
}

void templogpro_diag_boot_stage(uint32_t stage)
{
    g_diag.boot_stage = stage;

    RCC_APB1ENR_REG |= RCC_APB1ENR_PWREN_BIT;
    PWR_CR_REG |= PWR_CR_DBP_BIT;
    RTC_BKP1R_REG = stage;
}

void templogpro_diag_set_rtc(uint32_t ok)
{
    g_diag.rtc_ok = ok ? 1u : 0u;
}

void templogpro_diag_set_sensor(uint32_t ok)
{
    g_diag.sensor_ok = ok ? 1u : 0u;
}

void templogpro_diag_set_flash(uint32_t ok)
{
    g_diag.flash_ok = ok ? 1u : 0u;
}

void templogpro_diag_set_usb(uint32_t ok)
{
    g_diag.usb_ok = ok ? 1u : 0u;
}

void templogpro_diag_set_usb_vbus(uint32_t present)
{
    g_diag.usb_vbus_present = present ? 1u : 0u;
}

void templogpro_diag_set_error(TempLogProError error)
{
    g_diag.last_error = error;
}

void templogpro_diag_set_logger(uint32_t state, uint32_t record_count)
{
    g_diag.logger_state = state;
    g_diag.record_count = record_count;
}

void templogpro_diag_get(TempLogProDiagnostics *diag)
{
    if (diag != 0) {
        *diag = g_diag;
    }
}

const char *templogpro_diag_error_name(TempLogProError error)
{
    switch (error) {
    case TEMPLOGPRO_ERROR_NONE: return "NONE";
    case TEMPLOGPRO_ERROR_RTC_LSE: return "RTC_LSE";
    case TEMPLOGPRO_ERROR_RTC_INVALID: return "RTC_INVALID";
    case TEMPLOGPRO_ERROR_SENSOR: return "SENSOR";
    case TEMPLOGPRO_ERROR_FLASH_READ: return "FLASH_READ";
    case TEMPLOGPRO_ERROR_FLASH_WRITE: return "FLASH_WRITE";
    case TEMPLOGPRO_ERROR_FLASH_CRC: return "FLASH_CRC";
    case TEMPLOGPRO_ERROR_LOG_FULL: return "LOG_FULL";
    case TEMPLOGPRO_ERROR_LOG_METADATA: return "LOG_METADATA";
    case TEMPLOGPRO_ERROR_LOG_RECORD: return "LOG_RECORD";
    default: return "UNKNOWN";
    }
}

const char *templogpro_diag_reset_flags_name(uint32_t flags)
{
    if ((flags & (1u << 31)) != 0u) {
        return "LOW_POWER";
    }
    if ((flags & (1u << 30)) != 0u) {
        return "WWDG";
    }
    if ((flags & (1u << 29)) != 0u) {
        return "IWDG";
    }
    if ((flags & (1u << 28)) != 0u) {
        return "SOFTWARE";
    }
    if ((flags & (1u << 27)) != 0u) {
        return "POR_PDR";
    }
    if ((flags & (1u << 26)) != 0u) {
        return "PIN";
    }
    if ((flags & (1u << 25)) != 0u) {
        return "OBL";
    }
    return "UNKNOWN";
}
