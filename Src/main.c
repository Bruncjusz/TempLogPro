/*
 * TempLogPro - temperature logger firmware for the TempTale Ultra board
 * (STM32L152RC). Hardware drivers live in board.c and drv_*.c, the
 * application in templogpro_app.c; this file only starts the modules and runs
 * the main loop with idle/STOP sleep.
 */
#include <stdint.h>
#include "templogpro_app.h"
#include "templogpro_diag.h"
#include "templogpro_power.h"
#include "templogpro_usb.h"

int HAL_Init(void);

int main(void)
{
    templogpro_diag_boot_capture_reset();
    templogpro_diag_boot_stage(TEMPLOGPRO_BOOT_STAGE_RESET);
    (void)HAL_Init();
    templogpro_diag_boot_stage(TEMPLOGPRO_BOOT_STAGE_HAL);
    templogpro_diag_boot_stage(TEMPLOGPRO_BOOT_STAGE_CLOCK);
    (void)templogpro_app_init(APP_MODE_LOGGER);
    templogpro_diag_boot_stage(TEMPLOGPRO_BOOT_STAGE_USB);
    (void)templogpro_usb_st_msc_init();
    (void)templogpro_power_init();
    templogpro_diag_boot_stage(TEMPLOGPRO_BOOT_STAGE_MAIN_LOOP);

    for (;;) {
        templogpro_power_work_begin();
        templogpro_usb_st_msc_process();
        templogpro_app_process();
        templogpro_power_process();
        templogpro_power_work_end();
        if (templogpro_app_should_stop_sleep() != 0u) {
            if (templogpro_power_enter_stop() == 0u) {
                templogpro_power_enter_idle();
            }
        } else if (templogpro_app_should_idle_sleep() != 0u) {
            templogpro_power_enter_idle();
        }
    }
}
