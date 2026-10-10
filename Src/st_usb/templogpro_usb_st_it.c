#include "stm32l1xx_hal.h"

extern PCD_HandleTypeDef hpcd;

void SysTick_Handler(void)
{
    HAL_IncTick();
}

void USB_LP_IRQHandler(void)
{
    HAL_PCD_IRQHandler(&hpcd);
}

void USB_FS_WKUP_IRQHandler(void)
{
    __HAL_USB_WAKEUP_EXTI_CLEAR_FLAG();
}
