#include "usbd_conf.h"
#include "usbd_core.h"

PCD_HandleTypeDef hpcd;
volatile uint32_t templogpro_usb_st_status;
volatile uint32_t templogpro_usb_bus_event_count;
volatile uint32_t templogpro_usb_last_bus_activity_ms;

static void templogpro_usb_note_bus_activity(void)
{
    if (templogpro_usb_bus_event_count != 0xFFFFFFFFU) {
        templogpro_usb_bus_event_count++;
    }
    templogpro_usb_last_bus_activity_ms = HAL_GetTick();
}

void HAL_PCD_MspInit(PCD_HandleTypeDef *pcd)
{
    GPIO_InitTypeDef gpio;

    (void)pcd;

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_SYSCFG_CLK_ENABLE();
    __HAL_RCC_USB_CLK_ENABLE();

    gpio.Pin = GPIO_PIN_11 | GPIO_PIN_12;
    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    HAL_GPIO_Init(GPIOA, &gpio);

    HAL_NVIC_SetPriority(USB_LP_IRQn, 0U, 0U);
    HAL_NVIC_EnableIRQ(USB_LP_IRQn);
}

void HAL_PCD_MspDeInit(PCD_HandleTypeDef *pcd)
{
    GPIO_InitTypeDef gpio;

    (void)pcd;

    HAL_NVIC_DisableIRQ(USB_LP_IRQn);
    __HAL_RCC_USB_CLK_DISABLE();

    gpio.Pin = GPIO_PIN_11 | GPIO_PIN_12;
    gpio.Mode = GPIO_MODE_ANALOG;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOA, &gpio);
}

void HAL_PCD_SetupStageCallback(PCD_HandleTypeDef *pcd)
{
    templogpro_usb_st_status = 7U;
    templogpro_usb_note_bus_activity();
    USBD_LL_SetupStage((USBD_HandleTypeDef *)pcd->pData, (uint8_t *)pcd->Setup);
}

void HAL_PCD_DataOutStageCallback(PCD_HandleTypeDef *pcd, uint8_t epnum)
{
    USBD_LL_DataOutStage((USBD_HandleTypeDef *)pcd->pData, epnum, pcd->OUT_ep[epnum].xfer_buff);
}

void HAL_PCD_DataInStageCallback(PCD_HandleTypeDef *pcd, uint8_t epnum)
{
    USBD_LL_DataInStage((USBD_HandleTypeDef *)pcd->pData, epnum, pcd->IN_ep[epnum].xfer_buff);
}

void HAL_PCD_SOFCallback(PCD_HandleTypeDef *pcd)
{
    templogpro_usb_note_bus_activity();
    USBD_LL_SOF((USBD_HandleTypeDef *)pcd->pData);
}

void HAL_PCD_ResetCallback(PCD_HandleTypeDef *pcd)
{
    templogpro_usb_st_status = 6U;
    templogpro_usb_note_bus_activity();
    USBD_LL_SetSpeed((USBD_HandleTypeDef *)pcd->pData, USBD_SPEED_FULL);
    USBD_LL_Reset((USBD_HandleTypeDef *)pcd->pData);
}

void HAL_PCD_SuspendCallback(PCD_HandleTypeDef *pcd)
{
    USBD_LL_Suspend((USBD_HandleTypeDef *)pcd->pData);
}

void HAL_PCD_ResumeCallback(PCD_HandleTypeDef *pcd)
{
    templogpro_usb_note_bus_activity();
    USBD_LL_Resume((USBD_HandleTypeDef *)pcd->pData);
}

void HAL_PCD_ISOOUTIncompleteCallback(PCD_HandleTypeDef *pcd, uint8_t epnum)
{
    USBD_LL_IsoOUTIncomplete((USBD_HandleTypeDef *)pcd->pData, epnum);
}

void HAL_PCD_ISOINIncompleteCallback(PCD_HandleTypeDef *pcd, uint8_t epnum)
{
    USBD_LL_IsoINIncomplete((USBD_HandleTypeDef *)pcd->pData, epnum);
}

void HAL_PCD_ConnectCallback(PCD_HandleTypeDef *pcd)
{
    templogpro_usb_note_bus_activity();
    USBD_LL_DevConnected((USBD_HandleTypeDef *)pcd->pData);
}

void HAL_PCD_DisconnectCallback(PCD_HandleTypeDef *pcd)
{
    USBD_LL_DevDisconnected((USBD_HandleTypeDef *)pcd->pData);
}

USBD_StatusTypeDef USBD_LL_Init(USBD_HandleTypeDef *pdev)
{
    hpcd.Instance = USB;
    hpcd.Init.dev_endpoints = 8U;
    hpcd.Init.speed = PCD_SPEED_FULL;
    hpcd.Init.phy_itface = PCD_PHY_EMBEDDED;
    hpcd.Init.low_power_enable = 0U;
    hpcd.Init.lpm_enable = 0U;

    hpcd.pData = pdev;
    pdev->pData = &hpcd;

    if (HAL_PCD_Init(&hpcd) != HAL_OK) {
        return USBD_FAIL;
    }

    HAL_PCDEx_PMAConfig(&hpcd, 0x00U, PCD_SNG_BUF, 0x18U);
    HAL_PCDEx_PMAConfig(&hpcd, 0x80U, PCD_SNG_BUF, 0x58U);
    HAL_PCDEx_PMAConfig(&hpcd, 0x81U, PCD_SNG_BUF, 0x98U);
    HAL_PCDEx_PMAConfig(&hpcd, 0x01U, PCD_SNG_BUF, 0xD8U);
    HAL_PCDEx_PMAConfig(&hpcd, 0x83U, PCD_SNG_BUF, 0x118U);
    HAL_PCDEx_PMAConfig(&hpcd, 0x02U, PCD_SNG_BUF, 0x138U);
    HAL_PCDEx_PMAConfig(&hpcd, 0x82U, PCD_SNG_BUF, 0x178U);

    return USBD_OK;
}

USBD_StatusTypeDef USBD_LL_DeInit(USBD_HandleTypeDef *pdev)
{
    HAL_PCD_DeInit((PCD_HandleTypeDef *)pdev->pData);
    return USBD_OK;
}

USBD_StatusTypeDef USBD_LL_Start(USBD_HandleTypeDef *pdev)
{
    HAL_PCD_Start((PCD_HandleTypeDef *)pdev->pData);
    return USBD_OK;
}

USBD_StatusTypeDef USBD_LL_Stop(USBD_HandleTypeDef *pdev)
{
    HAL_PCD_Stop((PCD_HandleTypeDef *)pdev->pData);
    return USBD_OK;
}

USBD_StatusTypeDef USBD_LL_OpenEP(USBD_HandleTypeDef *pdev, uint8_t ep_addr, uint8_t ep_type, uint16_t ep_mps)
{
    HAL_PCD_EP_Open((PCD_HandleTypeDef *)pdev->pData, ep_addr, ep_mps, ep_type);
    return USBD_OK;
}

USBD_StatusTypeDef USBD_LL_CloseEP(USBD_HandleTypeDef *pdev, uint8_t ep_addr)
{
    HAL_PCD_EP_Close((PCD_HandleTypeDef *)pdev->pData, ep_addr);
    return USBD_OK;
}

USBD_StatusTypeDef USBD_LL_FlushEP(USBD_HandleTypeDef *pdev, uint8_t ep_addr)
{
    HAL_PCD_EP_Flush((PCD_HandleTypeDef *)pdev->pData, ep_addr);
    return USBD_OK;
}

USBD_StatusTypeDef USBD_LL_StallEP(USBD_HandleTypeDef *pdev, uint8_t ep_addr)
{
    HAL_PCD_EP_SetStall((PCD_HandleTypeDef *)pdev->pData, ep_addr);
    return USBD_OK;
}

USBD_StatusTypeDef USBD_LL_ClearStallEP(USBD_HandleTypeDef *pdev, uint8_t ep_addr)
{
    HAL_PCD_EP_ClrStall((PCD_HandleTypeDef *)pdev->pData, ep_addr);
    return USBD_OK;
}

uint8_t USBD_LL_IsStallEP(USBD_HandleTypeDef *pdev, uint8_t ep_addr)
{
    PCD_HandleTypeDef *pcd = (PCD_HandleTypeDef *)pdev->pData;

    if ((ep_addr & 0x80U) != 0U) {
        return pcd->IN_ep[ep_addr & 0x7FU].is_stall;
    }

    return pcd->OUT_ep[ep_addr & 0x7FU].is_stall;
}

USBD_StatusTypeDef USBD_LL_SetUSBAddress(USBD_HandleTypeDef *pdev, uint8_t dev_addr)
{
    HAL_PCD_SetAddress((PCD_HandleTypeDef *)pdev->pData, dev_addr);
    return USBD_OK;
}

USBD_StatusTypeDef USBD_LL_Transmit(USBD_HandleTypeDef *pdev, uint8_t ep_addr, uint8_t *pbuf, uint16_t size)
{
    HAL_PCD_EP_Transmit((PCD_HandleTypeDef *)pdev->pData, ep_addr, pbuf, size);
    return USBD_OK;
}

USBD_StatusTypeDef USBD_LL_PrepareReceive(USBD_HandleTypeDef *pdev, uint8_t ep_addr, uint8_t *pbuf, uint16_t size)
{
    HAL_PCD_EP_Receive((PCD_HandleTypeDef *)pdev->pData, ep_addr, pbuf, size);
    return USBD_OK;
}

uint32_t USBD_LL_GetRxDataSize(USBD_HandleTypeDef *pdev, uint8_t ep_addr)
{
    return HAL_PCD_EP_GetRxCount((PCD_HandleTypeDef *)pdev->pData, ep_addr);
}

void USBD_LL_Delay(uint32_t delay)
{
    HAL_Delay(delay);
}

void *USBD_static_malloc(uint32_t size)
{
    static uint32_t mem[(MAX_STATIC_ALLOC_SIZE + 3U) / 4U];

    if (size > MAX_STATIC_ALLOC_SIZE) {
        return NULL;
    }

    return mem;
}

void USBD_static_free(void *p)
{
    (void)p;
}

void HAL_PCDEx_SetConnectionState(PCD_HandleTypeDef *pcd, uint8_t state)
{
    (void)pcd;

    if (state != 0U) {
        __HAL_SYSCFG_USBPULLUP_ENABLE();
        templogpro_usb_st_status = 5U;
    } else {
        __HAL_SYSCFG_USBPULLUP_DISABLE();
    }
}
