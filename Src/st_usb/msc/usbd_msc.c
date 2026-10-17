/**
  ******************************************************************************
  * @file    usbd_msc.c
  * @author  MCD Application Team
  * @brief   This file provides all the MSC core functions.
  *
  * @verbatim
  *
  *          ===================================================================
  *                                MSC Class  Description
  *          ===================================================================
  *           This module manages the MSC class V1.0 following the "Universal
  *           Serial Bus Mass Storage Class (MSC) Bulk-Only Transport (BOT) Version 1.0
  *           Sep. 31, 1999".
  *           This driver implements the following aspects of the specification:
  *             - Bulk-Only Transport protocol
  *             - Subclass : SCSI transparent command set (ref. SCSI Primary Commands - 3 (SPC-3))
  *
  *  @endverbatim
  *
  ******************************************************************************
  * @attention
  *
  * <h2><center>&copy; Copyright (c) 2015 STMicroelectronics.
  * All rights reserved.</center></h2>
  *
  * This software component is licensed by ST under Ultimate Liberty license
  * SLA0044, the "License"; You may not use this file except in compliance with
  * the License. You may obtain a copy of the License at:
  *                      http://www.st.com/SLA0044
  *
  ******************************************************************************
  */

  /* BSPDependencies
  - "stm32xxxxx_{eval}{discovery}{nucleo_144}.c"
  - "stm32xxxxx_{eval}{discovery}_io.c"
  - "stm32xxxxx_{eval}{discovery}{adafruit}_sd.c"
  EndBSPDependencies */

/* Includes ------------------------------------------------------------------*/
#include "templogpro_app.h"
#include "templogpro_config.h"
#include "templogpro_logger.h"
#include "templogpro_power.h"
#include "templogpro_rtc.h"
#include "templogpro_tz.h"
#include "templogpro_usb_files.h"
#include "usbd_msc.h"
#include <string.h>


/** @addtogroup STM32_USB_DEVICE_LIBRARY
  * @{
  */


/** @defgroup MSC_CORE
  * @brief Mass storage core module
  * @{
  */

/** @defgroup MSC_CORE_Private_TypesDefinitions
  * @{
  */
/**
  * @}
  */


/** @defgroup MSC_CORE_Private_Defines
  * @{
  */

/**
  * @}
  */


/** @defgroup MSC_CORE_Private_Macros
  * @{
  */
/**
  * @}
  */


/** @defgroup MSC_CORE_Private_FunctionPrototypes
  * @{
  */
uint8_t  USBD_MSC_Init (USBD_HandleTypeDef *pdev,
                            uint8_t cfgidx);

uint8_t  USBD_MSC_DeInit (USBD_HandleTypeDef *pdev,
                              uint8_t cfgidx);

uint8_t  USBD_MSC_Setup (USBD_HandleTypeDef *pdev,
                             USBD_SetupReqTypedef *req);

uint8_t  USBD_MSC_DataIn (USBD_HandleTypeDef *pdev,
                              uint8_t epnum);


uint8_t  USBD_MSC_DataOut (USBD_HandleTypeDef *pdev,
                               uint8_t epnum);

uint8_t  USBD_MSC_EP0_RxReady (USBD_HandleTypeDef *pdev);

uint8_t  *USBD_MSC_GetHSCfgDesc (uint16_t *length);

uint8_t  *USBD_MSC_GetFSCfgDesc (uint16_t *length);

uint8_t  *USBD_MSC_GetOtherSpeedCfgDesc (uint16_t *length);

uint8_t  *USBD_MSC_GetDeviceQualifierDescriptor (uint16_t *length);


/**
  * @}
  */


/** @defgroup MSC_CORE_Private_Variables
  * @{
  */

#define CDC_INTERFACE_CONTROL        0U
#define CDC_INTERFACE_DATA           1U
#define MSC_INTERFACE_NUMBER         2U

#define CDC_SET_LINE_CODING          0x20U
#define CDC_GET_LINE_CODING          0x21U
#define CDC_SET_CONTROL_LINE_STATE   0x22U
#define CDC_SEND_BREAK               0x23U

#define CDC_RX_QUEUE_SIZE            256U
#define CDC_TX_QUEUE_SIZE            1024U
#define CDC_LINE_SIZE                128U
#define CDC_CONFIG_TEXT_SIZE         TEMPLOGPRO_CONFIG_TEXT_SIZE

static uint8_t cdc_line_coding[7] = {0x00U, 0xC2U, 0x01U, 0x00U, 0x00U, 0x00U, 0x08U};
static uint8_t cdc_ep_rx_buffer[CDC_DATA_FS_MAX_PACKET_SIZE];
static uint8_t cdc_ep_tx_buffer[CDC_DATA_FS_MAX_PACKET_SIZE];
static uint8_t cdc_rx_queue[CDC_RX_QUEUE_SIZE];
static uint8_t cdc_tx_queue[CDC_TX_QUEUE_SIZE];
static char cdc_line[CDC_LINE_SIZE];
static char cdc_config_text[CDC_CONFIG_TEXT_SIZE];
static volatile uint16_t cdc_rx_head;
static volatile uint16_t cdc_rx_tail;
static volatile uint16_t cdc_tx_head;
static volatile uint16_t cdc_tx_tail;
static uint16_t cdc_tx_inflight_len;
static uint16_t cdc_line_len;
static uint16_t cdc_config_len;
static volatile uint8_t cdc_tx_busy;
static uint8_t cdc_collect_config;
static uint8_t cdc_config_overflow;

static uint8_t cdc_setup(USBD_HandleTypeDef *pdev, USBD_SetupReqTypedef *req);
static void cdc_init_state(void);
static void cdc_push_rx(const uint8_t *data, uint32_t len);
static uint32_t cdc_pop_rx(uint8_t *ch);
static void cdc_tx_kick(USBD_HandleTypeDef *pdev);
static void cdc_tx_done(USBD_HandleTypeDef *pdev);
static void cdc_queue_bytes(USBD_HandleTypeDef *pdev, const char *text, uint32_t len);
static void cdc_queue_text(USBD_HandleTypeDef *pdev, const char *text);
static void cdc_process_char(USBD_HandleTypeDef *pdev, uint8_t ch);
static void cdc_process_line(USBD_HandleTypeDef *pdev, const char *line);
static void cdc_queue_u32(USBD_HandleTypeDef *pdev, uint32_t value);
static void cdc_queue_datetime(USBD_HandleTypeDef *pdev, uint32_t timestamp);
static void cdc_queue_time(USBD_HandleTypeDef *pdev);
static void cdc_queue_s32(USBD_HandleTypeDef *pdev, int32_t value);
static void cdc_queue_temp_x256(USBD_HandleTypeDef *pdev, int32_t temp_x256);
static void cdc_queue_logger_status(USBD_HandleTypeDef *pdev);
static void cdc_queue_power_status(USBD_HandleTypeDef *pdev);
static void cdc_queue_logger_action(USBD_HandleTypeDef *pdev, const char *name, uint32_t ok);
static const char *cdc_app_state_name(TempLogProState state);
static const char *cdc_logger_state_name(TempLogProLoggerState state);
static uint32_t cdc_text_equals(const char *text, const char *token);
static uint32_t cdc_text_starts(const char *text, const char *prefix);
static const char *cdc_skip_spaces(const char *text);
static uint32_t cdc_parse_datetime(const char *text, TempLogProDateTime *dt);
static uint32_t cdc_parse_digit(const char *text, uint32_t offset, uint32_t digits, uint32_t *value);
static void cdc_queue_config(USBD_HandleTypeDef *pdev);
static uint32_t cdc_trimmed_config_len(const char *text, uint32_t len);


USBD_ClassTypeDef  USBD_MSC =
{
  USBD_MSC_Init,
  USBD_MSC_DeInit,
  USBD_MSC_Setup,
  NULL, /*EP0_TxSent*/
  USBD_MSC_EP0_RxReady, /*EP0_RxReady*/
  USBD_MSC_DataIn,
  USBD_MSC_DataOut,
  NULL, /*SOF */
  NULL,
  NULL,
  USBD_MSC_GetHSCfgDesc,
  USBD_MSC_GetFSCfgDesc,
  USBD_MSC_GetOtherSpeedCfgDesc,
  USBD_MSC_GetDeviceQualifierDescriptor,
};

/* USB Composite CDC ACM + Mass Storage Configuration Descriptor */
__ALIGN_BEGIN uint8_t USBD_MSC_CfgHSDesc[USB_MSC_CONFIG_DESC_SIZ]  __ALIGN_END =
{
  0x09, USB_DESC_TYPE_CONFIGURATION, LOBYTE(USB_MSC_CONFIG_DESC_SIZ), HIBYTE(USB_MSC_CONFIG_DESC_SIZ),
  0x03, 0x01, 0x04, 0xC0, 0x32,

  0x08, 0x0BU, CDC_INTERFACE_CONTROL, 0x02, 0x02, 0x02, 0x01, 0x05,
  0x09, USB_DESC_TYPE_INTERFACE, CDC_INTERFACE_CONTROL, 0x00, 0x01, 0x02, 0x02, 0x01, 0x05,
  0x05, 0x24, 0x00, 0x10, 0x01,
  0x05, 0x24, 0x01, 0x00, CDC_INTERFACE_DATA,
  0x04, 0x24, 0x02, 0x02,
  0x05, 0x24, 0x06, CDC_INTERFACE_CONTROL, CDC_INTERFACE_DATA,
  0x07, USB_DESC_TYPE_ENDPOINT, CDC_CMD_EP, 0x03, LOBYTE(CDC_CMD_PACKET_SIZE), HIBYTE(CDC_CMD_PACKET_SIZE), 0x10,

  0x09, USB_DESC_TYPE_INTERFACE, CDC_INTERFACE_DATA, 0x00, 0x02, 0x0A, 0x00, 0x00, 0x05,
  0x07, USB_DESC_TYPE_ENDPOINT, CDC_OUT_EP, 0x02, LOBYTE(CDC_DATA_FS_MAX_PACKET_SIZE), HIBYTE(CDC_DATA_FS_MAX_PACKET_SIZE), 0x00,
  0x07, USB_DESC_TYPE_ENDPOINT, CDC_IN_EP, 0x02, LOBYTE(CDC_DATA_FS_MAX_PACKET_SIZE), HIBYTE(CDC_DATA_FS_MAX_PACKET_SIZE), 0x00,

  0x09, USB_DESC_TYPE_INTERFACE, MSC_INTERFACE_NUMBER, 0x00, 0x02, 0x08, 0x06, 0x50, 0x05,
  0x07, USB_DESC_TYPE_ENDPOINT, MSC_EPIN_ADDR, 0x02, LOBYTE(MSC_MAX_HS_PACKET), HIBYTE(MSC_MAX_HS_PACKET), 0x00,
  0x07, USB_DESC_TYPE_ENDPOINT, MSC_EPOUT_ADDR, 0x02, LOBYTE(MSC_MAX_HS_PACKET), HIBYTE(MSC_MAX_HS_PACKET), 0x00
};

__ALIGN_BEGIN uint8_t USBD_MSC_CfgFSDesc[USB_MSC_CONFIG_DESC_SIZ]  __ALIGN_END =
{
  0x09, USB_DESC_TYPE_CONFIGURATION, LOBYTE(USB_MSC_CONFIG_DESC_SIZ), HIBYTE(USB_MSC_CONFIG_DESC_SIZ),
  0x03, 0x01, 0x04, 0xC0, 0x32,

  0x08, 0x0BU, CDC_INTERFACE_CONTROL, 0x02, 0x02, 0x02, 0x01, 0x05,
  0x09, USB_DESC_TYPE_INTERFACE, CDC_INTERFACE_CONTROL, 0x00, 0x01, 0x02, 0x02, 0x01, 0x05,
  0x05, 0x24, 0x00, 0x10, 0x01,
  0x05, 0x24, 0x01, 0x00, CDC_INTERFACE_DATA,
  0x04, 0x24, 0x02, 0x02,
  0x05, 0x24, 0x06, CDC_INTERFACE_CONTROL, CDC_INTERFACE_DATA,
  0x07, USB_DESC_TYPE_ENDPOINT, CDC_CMD_EP, 0x03, LOBYTE(CDC_CMD_PACKET_SIZE), HIBYTE(CDC_CMD_PACKET_SIZE), 0x10,

  0x09, USB_DESC_TYPE_INTERFACE, CDC_INTERFACE_DATA, 0x00, 0x02, 0x0A, 0x00, 0x00, 0x05,
  0x07, USB_DESC_TYPE_ENDPOINT, CDC_OUT_EP, 0x02, LOBYTE(CDC_DATA_FS_MAX_PACKET_SIZE), HIBYTE(CDC_DATA_FS_MAX_PACKET_SIZE), 0x00,
  0x07, USB_DESC_TYPE_ENDPOINT, CDC_IN_EP, 0x02, LOBYTE(CDC_DATA_FS_MAX_PACKET_SIZE), HIBYTE(CDC_DATA_FS_MAX_PACKET_SIZE), 0x00,

  0x09, USB_DESC_TYPE_INTERFACE, MSC_INTERFACE_NUMBER, 0x00, 0x02, 0x08, 0x06, 0x50, 0x05,
  0x07, USB_DESC_TYPE_ENDPOINT, MSC_EPIN_ADDR, 0x02, LOBYTE(MSC_MAX_FS_PACKET), HIBYTE(MSC_MAX_FS_PACKET), 0x00,
  0x07, USB_DESC_TYPE_ENDPOINT, MSC_EPOUT_ADDR, 0x02, LOBYTE(MSC_MAX_FS_PACKET), HIBYTE(MSC_MAX_FS_PACKET), 0x00
};

__ALIGN_BEGIN uint8_t USBD_MSC_OtherSpeedCfgDesc[USB_MSC_CONFIG_DESC_SIZ]   __ALIGN_END =
{
  0x09, USB_DESC_TYPE_OTHER_SPEED_CONFIGURATION, LOBYTE(USB_MSC_CONFIG_DESC_SIZ), HIBYTE(USB_MSC_CONFIG_DESC_SIZ),
  0x03, 0x01, 0x04, 0xC0, 0x32,

  0x08, 0x0BU, CDC_INTERFACE_CONTROL, 0x02, 0x02, 0x02, 0x01, 0x05,
  0x09, USB_DESC_TYPE_INTERFACE, CDC_INTERFACE_CONTROL, 0x00, 0x01, 0x02, 0x02, 0x01, 0x05,
  0x05, 0x24, 0x00, 0x10, 0x01,
  0x05, 0x24, 0x01, 0x00, CDC_INTERFACE_DATA,
  0x04, 0x24, 0x02, 0x02,
  0x05, 0x24, 0x06, CDC_INTERFACE_CONTROL, CDC_INTERFACE_DATA,
  0x07, USB_DESC_TYPE_ENDPOINT, CDC_CMD_EP, 0x03, LOBYTE(CDC_CMD_PACKET_SIZE), HIBYTE(CDC_CMD_PACKET_SIZE), 0x10,

  0x09, USB_DESC_TYPE_INTERFACE, CDC_INTERFACE_DATA, 0x00, 0x02, 0x0A, 0x00, 0x00, 0x05,
  0x07, USB_DESC_TYPE_ENDPOINT, CDC_OUT_EP, 0x02, LOBYTE(CDC_DATA_FS_MAX_PACKET_SIZE), HIBYTE(CDC_DATA_FS_MAX_PACKET_SIZE), 0x00,
  0x07, USB_DESC_TYPE_ENDPOINT, CDC_IN_EP, 0x02, LOBYTE(CDC_DATA_FS_MAX_PACKET_SIZE), HIBYTE(CDC_DATA_FS_MAX_PACKET_SIZE), 0x00,

  0x09, USB_DESC_TYPE_INTERFACE, MSC_INTERFACE_NUMBER, 0x00, 0x02, 0x08, 0x06, 0x50, 0x05,
  0x07, USB_DESC_TYPE_ENDPOINT, MSC_EPIN_ADDR, 0x02, LOBYTE(MSC_MAX_FS_PACKET), HIBYTE(MSC_MAX_FS_PACKET), 0x00,
  0x07, USB_DESC_TYPE_ENDPOINT, MSC_EPOUT_ADDR, 0x02, LOBYTE(MSC_MAX_FS_PACKET), HIBYTE(MSC_MAX_FS_PACKET), 0x00
};

/* USB Standard Device Descriptor */
__ALIGN_BEGIN  uint8_t USBD_MSC_DeviceQualifierDesc[USB_LEN_DEV_QUALIFIER_DESC]  __ALIGN_END =
{
  USB_LEN_DEV_QUALIFIER_DESC,
  USB_DESC_TYPE_DEVICE_QUALIFIER,
  0x00,
  0x02,
  0xEF,
  0x02,
  0x01,
  MSC_MAX_FS_PACKET,
  0x01,
  0x00,
};
/**
  * @}
  */


/** @defgroup MSC_CORE_Private_Functions
  * @{
  */

/**
  * @brief  USBD_MSC_Init
  *         Initialize  the mass storage configuration
  * @param  pdev: device instance
  * @param  cfgidx: configuration index
  * @retval status
  */
uint8_t  USBD_MSC_Init (USBD_HandleTypeDef *pdev, uint8_t cfgidx)
{
  cdc_init_state();

  USBD_LL_OpenEP(pdev, CDC_CMD_EP, USBD_EP_TYPE_INTR, CDC_CMD_PACKET_SIZE);
  pdev->ep_in[CDC_CMD_EP & 0xFU].is_used = 1U;

  USBD_LL_OpenEP(pdev, CDC_IN_EP, USBD_EP_TYPE_BULK, CDC_DATA_FS_MAX_PACKET_SIZE);
  pdev->ep_in[CDC_IN_EP & 0xFU].is_used = 1U;

  USBD_LL_OpenEP(pdev, CDC_OUT_EP, USBD_EP_TYPE_BULK, CDC_DATA_FS_MAX_PACKET_SIZE);
  pdev->ep_out[CDC_OUT_EP & 0xFU].is_used = 1U;

  USBD_LL_PrepareReceive(pdev, CDC_OUT_EP, cdc_ep_rx_buffer, CDC_DATA_FS_MAX_PACKET_SIZE);

  if(pdev->dev_speed == USBD_SPEED_HIGH)
  {
    /* Open EP OUT */
    USBD_LL_OpenEP(pdev, MSC_EPOUT_ADDR, USBD_EP_TYPE_BULK, MSC_MAX_HS_PACKET);
    pdev->ep_out[MSC_EPOUT_ADDR & 0xFU].is_used = 1U;

    /* Open EP IN */
    USBD_LL_OpenEP(pdev, MSC_EPIN_ADDR, USBD_EP_TYPE_BULK, MSC_MAX_HS_PACKET);
    pdev->ep_in[MSC_EPIN_ADDR & 0xFU].is_used = 1U;
  }
  else
  {
    /* Open EP OUT */
    USBD_LL_OpenEP(pdev, MSC_EPOUT_ADDR, USBD_EP_TYPE_BULK, MSC_MAX_FS_PACKET);
    pdev->ep_out[MSC_EPOUT_ADDR & 0xFU].is_used = 1U;

    /* Open EP IN */
    USBD_LL_OpenEP(pdev, MSC_EPIN_ADDR, USBD_EP_TYPE_BULK, MSC_MAX_FS_PACKET);
    pdev->ep_in[MSC_EPIN_ADDR & 0xFU].is_used = 1U;
  }
  pdev->pClassData = USBD_malloc(sizeof (USBD_MSC_BOT_HandleTypeDef));

  if(pdev->pClassData == NULL)
  {
    return USBD_FAIL;
  }

  /* Init the BOT  layer */
  MSC_BOT_Init(pdev);

  return USBD_OK;
}

/**
  * @brief  USBD_MSC_DeInit
  *         DeInitilaize  the mass storage configuration
  * @param  pdev: device instance
  * @param  cfgidx: configuration index
  * @retval status
  */
uint8_t  USBD_MSC_DeInit (USBD_HandleTypeDef *pdev,
                              uint8_t cfgidx)
{
  USBD_LL_CloseEP(pdev, CDC_CMD_EP);
  pdev->ep_in[CDC_CMD_EP & 0xFU].is_used = 0U;

  USBD_LL_CloseEP(pdev, CDC_IN_EP);
  pdev->ep_in[CDC_IN_EP & 0xFU].is_used = 0U;

  USBD_LL_CloseEP(pdev, CDC_OUT_EP);
  pdev->ep_out[CDC_OUT_EP & 0xFU].is_used = 0U;

  /* Close MSC EPs */
  USBD_LL_CloseEP(pdev, MSC_EPOUT_ADDR);
  pdev->ep_out[MSC_EPOUT_ADDR & 0xFU].is_used = 0U;

  /* Close EP IN */
  USBD_LL_CloseEP(pdev, MSC_EPIN_ADDR);
  pdev->ep_in[MSC_EPIN_ADDR & 0xFU].is_used = 0U;

    /* De-Init the BOT layer */
  MSC_BOT_DeInit(pdev);

  /* Free MSC Class Resources */
  if(pdev->pClassData != NULL)
  {
    USBD_free(pdev->pClassData);
    pdev->pClassData  = NULL;
  }
  return USBD_OK;
}
/**
* @brief  USBD_MSC_Setup
*         Handle the MSC specific requests
* @param  pdev: device instance
* @param  req: USB request
* @retval status
*/
uint8_t  USBD_MSC_Setup (USBD_HandleTypeDef *pdev, USBD_SetupReqTypedef *req)
{
  USBD_MSC_BOT_HandleTypeDef *hmsc = (USBD_MSC_BOT_HandleTypeDef*) pdev->pClassData;
  uint8_t ret = USBD_OK;
  uint16_t status_info = 0U;
  uint8_t interface = (uint8_t)(req->wIndex & 0xFFU);

  if (((req->bmRequest & USB_REQ_TYPE_MASK) == USB_REQ_TYPE_CLASS) &&
      ((req->bmRequest & USB_REQ_RECIPIENT_MASK) == USB_REQ_RECIPIENT_INTERFACE) &&
      ((interface == CDC_INTERFACE_CONTROL) || (interface == CDC_INTERFACE_DATA))) {
    return cdc_setup(pdev, req);
  }

  if (((req->bmRequest & USB_REQ_TYPE_MASK) == USB_REQ_TYPE_STANDARD) &&
      ((req->bmRequest & USB_REQ_RECIPIENT_MASK) == USB_REQ_RECIPIENT_INTERFACE) &&
      ((interface == CDC_INTERFACE_CONTROL) || (interface == CDC_INTERFACE_DATA))) {
    switch (req->bRequest) {
    case USB_REQ_GET_STATUS:
      USBD_CtlSendData(pdev, (uint8_t *)(void *)&status_info, 2U);
      break;
    case USB_REQ_GET_INTERFACE:
      status_info = 0U;
      USBD_CtlSendData(pdev, (uint8_t *)(void *)&status_info, 1U);
      break;
    case USB_REQ_SET_INTERFACE:
      break;
    default:
      USBD_CtlError(pdev, req);
      ret = USBD_FAIL;
      break;
    }
    return ret;
  }

  switch (req->bmRequest & USB_REQ_TYPE_MASK)
  {

    /* Class request */
  case USB_REQ_TYPE_CLASS:
    switch (req->bRequest)
    {
    case BOT_GET_MAX_LUN:
      if((req->wValue  == 0U) && (req->wLength == 1U) &&
         ((req->bmRequest & 0x80U) == 0x80U))
      {
        hmsc->max_lun = (uint32_t)((USBD_StorageTypeDef *)pdev->pUserData)->GetMaxLun();
        USBD_CtlSendData (pdev, (uint8_t *)(void *)&hmsc->max_lun, 1U);
      }
      else
      {
        USBD_CtlError(pdev, req);
        ret = USBD_FAIL;
      }
      break;

    case BOT_RESET :
      if((req->wValue  == 0U) && (req->wLength == 0U) &&
         ((req->bmRequest & 0x80U) != 0x80U))
      {
        MSC_BOT_Reset(pdev);
      }
      else
      {
        USBD_CtlError(pdev , req);
        ret = USBD_FAIL;
      }
      break;

    default:
      USBD_CtlError(pdev , req);
      ret = USBD_FAIL;
      break;
    }
    break;
    /* Interface & Endpoint request */
  case USB_REQ_TYPE_STANDARD:
    switch (req->bRequest)
    {
    case USB_REQ_GET_STATUS:
      if (pdev->dev_state == USBD_STATE_CONFIGURED)
      {
        USBD_CtlSendData (pdev, (uint8_t *)(void *)&status_info, 2U);
      }
      else
      {
        USBD_CtlError (pdev, req);
        ret = USBD_FAIL;
      }
      break;

    case USB_REQ_GET_INTERFACE:
      if (pdev->dev_state == USBD_STATE_CONFIGURED)
      {
        USBD_CtlSendData (pdev, (uint8_t *)(void *)&hmsc->interface, 1U);
      }
      else
      {
        USBD_CtlError (pdev, req);
        ret = USBD_FAIL;
      }
      break;

    case USB_REQ_SET_INTERFACE:
      if (pdev->dev_state == USBD_STATE_CONFIGURED)
      {
        hmsc->interface = (uint8_t)(req->wValue);
      }
      else
      {
        USBD_CtlError (pdev, req);
        ret = USBD_FAIL;
      }
      break;

    case USB_REQ_CLEAR_FEATURE:

      /* Flush the FIFO and Clear the stall status */
      USBD_LL_FlushEP(pdev, (uint8_t)req->wIndex);

      /* Reactivate the EP */
      USBD_LL_CloseEP (pdev , (uint8_t)req->wIndex);
      if((((uint8_t)req->wIndex) & 0x80U) == 0x80U)
      {
        pdev->ep_in[(uint8_t)req->wIndex & 0xFU].is_used = 0U;
        if(pdev->dev_speed == USBD_SPEED_HIGH)
        {
          /* Open EP IN */
          USBD_LL_OpenEP(pdev, MSC_EPIN_ADDR, USBD_EP_TYPE_BULK,
                         MSC_MAX_HS_PACKET);
        }
        else
        {
          /* Open EP IN */
          USBD_LL_OpenEP(pdev, MSC_EPIN_ADDR, USBD_EP_TYPE_BULK,
                         MSC_MAX_FS_PACKET);
        }
        pdev->ep_in[MSC_EPIN_ADDR & 0xFU].is_used = 1U;
      }
      else
      {
        pdev->ep_out[(uint8_t)req->wIndex & 0xFU].is_used = 0U;
        if(pdev->dev_speed == USBD_SPEED_HIGH)
        {
          /* Open EP OUT */
          USBD_LL_OpenEP(pdev, MSC_EPOUT_ADDR, USBD_EP_TYPE_BULK,
                         MSC_MAX_HS_PACKET);
        }
        else
        {
          /* Open EP OUT */
          USBD_LL_OpenEP(pdev, MSC_EPOUT_ADDR, USBD_EP_TYPE_BULK,
                         MSC_MAX_FS_PACKET);
        }
        pdev->ep_out[MSC_EPOUT_ADDR & 0xFU].is_used = 1U;
      }

      /* Handle BOT error */
      MSC_BOT_CplClrFeature(pdev, (uint8_t)req->wIndex);
      break;

    default:
      USBD_CtlError (pdev, req);
      ret = USBD_FAIL;
      break;
    }
    break;

  default:
    USBD_CtlError (pdev, req);
    ret = USBD_FAIL;
    break;
  }

  return ret;
}

/**
* @brief  USBD_MSC_DataIn
*         handle data IN Stage
* @param  pdev: device instance
* @param  epnum: endpoint index
* @retval status
*/
uint8_t  USBD_MSC_DataIn (USBD_HandleTypeDef *pdev,
                              uint8_t epnum)
{
  if (epnum == (MSC_EPIN_ADDR & 0xFU))
  {
    MSC_BOT_DataIn(pdev , epnum);
  }
  else if (epnum == (CDC_IN_EP & 0xFU))
  {
    cdc_tx_done(pdev);
  }
  else if (epnum == (CDC_CMD_EP & 0xFU))
  {
  }
  return USBD_OK;
}

/**
* @brief  USBD_MSC_DataOut
*         handle data OUT Stage
* @param  pdev: device instance
* @param  epnum: endpoint index
* @retval status
*/
uint8_t  USBD_MSC_DataOut (USBD_HandleTypeDef *pdev,
                               uint8_t epnum)
{
  if (epnum == (MSC_EPOUT_ADDR & 0xFU))
  {
    MSC_BOT_DataOut(pdev , epnum);
  }
  else if (epnum == (CDC_OUT_EP & 0xFU))
  {
    uint32_t count = USBD_LL_GetRxDataSize(pdev, epnum);
    cdc_push_rx(cdc_ep_rx_buffer, count);
    USBD_LL_PrepareReceive(pdev, CDC_OUT_EP, cdc_ep_rx_buffer, CDC_DATA_FS_MAX_PACKET_SIZE);
  }
  return USBD_OK;
}

uint8_t USBD_MSC_EP0_RxReady(USBD_HandleTypeDef *pdev)
{
  (void)pdev;
  return USBD_OK;
}

void USBD_MSC_CDC_Process(USBD_HandleTypeDef *pdev)
{
  uint8_t ch;

  while (cdc_pop_rx(&ch) != 0U) {
    cdc_process_char(pdev, ch);
  }
  cdc_tx_kick(pdev);
}

uint32_t USBD_MSC_CDC_WorkPending(USBD_HandleTypeDef *pdev)
{
  if ((pdev == NULL) || (pdev->dev_state != USBD_STATE_CONFIGURED)) {
    return 0U;
  }
  if (cdc_rx_head != cdc_rx_tail) {
    return 1U;
  }
  /* In-flight TX completes in the USB IRQ; only an unsent packet needs CPU. */
  return ((cdc_tx_busy == 0U) && (cdc_tx_head != cdc_tx_tail)) ? 1U : 0U;
}

static uint8_t cdc_setup(USBD_HandleTypeDef *pdev, USBD_SetupReqTypedef *req)
{
  uint8_t ret = USBD_OK;

  switch (req->bRequest)
  {
  case CDC_SET_LINE_CODING:
    if (req->wLength == sizeof(cdc_line_coding))
    {
      USBD_CtlPrepareRx(pdev, cdc_line_coding, sizeof(cdc_line_coding));
    }
    else
    {
      USBD_CtlError(pdev, req);
      ret = USBD_FAIL;
    }
    break;

  case CDC_GET_LINE_CODING:
    USBD_CtlSendData(pdev, cdc_line_coding, sizeof(cdc_line_coding));
    break;

  case CDC_SET_CONTROL_LINE_STATE:
  case CDC_SEND_BREAK:
    break;

  default:
    USBD_CtlError(pdev, req);
    ret = USBD_FAIL;
    break;
  }

  return ret;
}

static void cdc_init_state(void)
{
  cdc_rx_head = 0U;
  cdc_rx_tail = 0U;
  cdc_tx_head = 0U;
  cdc_tx_tail = 0U;
  cdc_tx_inflight_len = 0U;
  cdc_line_len = 0U;
  cdc_config_len = 0U;
  cdc_tx_busy = 0U;
  cdc_collect_config = 0U;
  cdc_config_overflow = 0U;
}

static void cdc_push_rx(const uint8_t *data, uint32_t len)
{
  uint32_t i;

  for (i = 0U; i < len; i++) {
    uint16_t next = (uint16_t)((cdc_rx_head + 1U) % CDC_RX_QUEUE_SIZE);
    if (next == cdc_rx_tail) {
      break;
    }
    cdc_rx_queue[cdc_rx_head] = data[i];
    cdc_rx_head = next;
  }
}

static uint32_t cdc_pop_rx(uint8_t *ch)
{
  if (cdc_rx_tail == cdc_rx_head) {
    return 0U;
  }
  *ch = cdc_rx_queue[cdc_rx_tail];
  cdc_rx_tail = (uint16_t)((cdc_rx_tail + 1U) % CDC_RX_QUEUE_SIZE);
  return 1U;
}

static void cdc_tx_kick(USBD_HandleTypeDef *pdev)
{
  uint16_t len = 0U;

  if ((pdev == NULL) || (cdc_tx_busy != 0U) || (cdc_tx_tail == cdc_tx_head)) {
    return;
  }

  while ((cdc_tx_tail != cdc_tx_head) && (len < CDC_DATA_FS_MAX_PACKET_SIZE)) {
    cdc_ep_tx_buffer[len++] = cdc_tx_queue[cdc_tx_tail];
    cdc_tx_tail = (uint16_t)((cdc_tx_tail + 1U) % CDC_TX_QUEUE_SIZE);
  }

  if (len != 0U) {
    cdc_tx_inflight_len = len;
    cdc_tx_busy = 1U;
    USBD_LL_Transmit(pdev, CDC_IN_EP, cdc_ep_tx_buffer, len);
  }
}

static void cdc_tx_done(USBD_HandleTypeDef *pdev)
{
  cdc_tx_inflight_len = 0U;
  cdc_tx_busy = 0U;
  cdc_tx_kick(pdev);
}

static void cdc_queue_bytes(USBD_HandleTypeDef *pdev, const char *text, uint32_t len)
{
  uint32_t i;

  for (i = 0U; i < len; i++) {
    uint16_t next = (uint16_t)((cdc_tx_head + 1U) % CDC_TX_QUEUE_SIZE);
    if (next == cdc_tx_tail) {
      break;
    }
    cdc_tx_queue[cdc_tx_head] = (uint8_t)text[i];
    cdc_tx_head = next;
  }
  cdc_tx_kick(pdev);
}

/* "YYYY-MM-DD HH:MM:SS" for a timestamp in seconds since 1970. */
static void cdc_queue_datetime(USBD_HandleTypeDef *pdev, uint32_t timestamp)
{
  TempLogProDateTime dt;
  char text[20];
  uint32_t values[6];
  static const char separators[6] = {'-', '-', ' ', ':', ':', 0};
  uint32_t pos = 0U;
  uint32_t i;

  if (templogpro_rtc_timestamp_to_datetime(timestamp, &dt) == 0U) {
    cdc_queue_text(pdev, "invalid");
    return;
  }
  values[0] = dt.year;
  values[1] = dt.month;
  values[2] = dt.day;
  values[3] = dt.hour;
  values[4] = dt.minute;
  values[5] = dt.second;
  for (i = 0U; i < 6U; i++) {
    uint32_t div = (i == 0U) ? 1000U : 10U;
    while (div != 0U) {
      text[pos++] = (char)('0' + ((values[i] / div) % 10U));
      div /= 10U;
    }
    if (separators[i] != 0) {
      text[pos++] = separators[i];
    }
  }
  text[pos] = 0;
  cdc_queue_text(pdev, text);
}

/* GET TIME: device clock (UTC) and the local time of the configured zone. */
static void cdc_queue_time(USBD_HandleTypeDef *pdev)
{
  TempLogProDateTime now;
  TempLogProConfig config;
  uint32_t utc;
  int32_t offset;

  if ((templogpro_rtc_get(&now) == 0U) || (templogpro_rtc_datetime_to_timestamp(&now, &utc) == 0U)) {
    cdc_queue_text(pdev, "ERR E005 rtc read failed\n");
    return;
  }
  templogpro_config_get(&config);
  offset = templogpro_tz_offset_minutes(&config, utc);
  cdc_queue_text(pdev, "TIME utc=");
  cdc_queue_datetime(pdev, utc);
  cdc_queue_text(pdev, " local=");
  cdc_queue_datetime(pdev, (uint32_t)((int32_t)utc + (offset * 60)));
  cdc_queue_text(pdev, " offset=");
  cdc_queue_text(pdev, (offset < 0) ? "-" : "+");
  cdc_queue_u32(pdev, (uint32_t)((offset < 0) ? -offset : offset));
  cdc_queue_text(pdev, "\n");
}

static void cdc_queue_text(USBD_HandleTypeDef *pdev, const char *text)
{
  cdc_queue_bytes(pdev, text, (uint32_t)strlen(text));
}

static void cdc_queue_u32(USBD_HandleTypeDef *pdev, uint32_t value)
{
  char text[10];
  uint32_t pos = 0U;
  uint32_t div = 1000000000U;
  uint32_t started = 0U;

  while (div != 0U) {
    uint32_t digit = value / div;
    if ((digit != 0U) || (started != 0U) || (div == 1U)) {
      text[pos++] = (char)('0' + digit);
      started = 1U;
    }
    value %= div;
    div /= 10U;
  }

  cdc_queue_bytes(pdev, text, pos);
}

static void cdc_queue_s32(USBD_HandleTypeDef *pdev, int32_t value)
{
  uint32_t magnitude;

  if (value < 0) {
    cdc_queue_text(pdev, "-");
    magnitude = (uint32_t)(-(value + 1)) + 1U;
  } else {
    magnitude = (uint32_t)value;
  }
  cdc_queue_u32(pdev, magnitude);
}

static void cdc_queue_temp_x256(USBD_HandleTypeDef *pdev, int32_t temp_x256)
{
  int32_t centi;
  uint32_t frac;

  centi = (temp_x256 * 100);
  if (centi >= 0) {
    centi += 128;
  } else {
    centi -= 128;
  }
  centi /= 256;

  cdc_queue_s32(pdev, centi / 100);
  cdc_queue_text(pdev, ".");
  frac = (uint32_t)((centi < 0) ? -(centi % 100) : (centi % 100));
  cdc_queue_text(pdev, (frac < 10U) ? "0" : "");
  cdc_queue_u32(pdev, frac);
}

static void cdc_queue_logger_status(USBD_HandleTypeDef *pdev)
{
  TempLogProLoggerInfo info;
  TempLogProLogStats stats;
  TempLogProState app_state = templogpro_app_get_state();

  cdc_queue_text(pdev, "LOGGER STATUS app=");
  cdc_queue_text(pdev, cdc_app_state_name(app_state));

  if (templogpro_logger_get_info(&info) == 0U) {
    cdc_queue_text(pdev, " logger=ERROR records=0\n");
    return;
  }

  cdc_queue_text(pdev, " logger=");
  cdc_queue_text(pdev, cdc_logger_state_name(info.logger_state));
  cdc_queue_text(pdev, " records=");
  cdc_queue_u32(pdev, info.record_count);
  cdc_queue_text(pdev, " capacity=");
  cdc_queue_u32(pdev, info.capacity_records);
  cdc_queue_text(pdev, " interval=");
  cdc_queue_u32(pdev, info.logging_interval_seconds);
  cdc_queue_text(pdev, " active=");
  cdc_queue_u32(pdev, info.active_seconds);

  if ((templogpro_logger_get_stats(&stats) != 0U) && (stats.valid != 0U)) {
    cdc_queue_text(pdev, " samples=");
    cdc_queue_u32(pdev, stats.sample_count);
    cdc_queue_text(pdev, " min=");
    cdc_queue_temp_x256(pdev, stats.min_temperature_x256);
    cdc_queue_text(pdev, " max=");
    cdc_queue_temp_x256(pdev, stats.max_temperature_x256);
    cdc_queue_text(pdev, " avg=");
    cdc_queue_temp_x256(pdev, stats.avg_temperature_x256);
  } else {
    cdc_queue_text(pdev, " samples=0");
  }
  if (templogpro_app_power_off_armed() != 0U) {
    cdc_queue_text(pdev, " off=ARMED");
  }

  cdc_queue_text(pdev, "\n");
}

static void cdc_queue_power_status(USBD_HandleTypeDef *pdev)
{
  TempLogProPowerStats stats;
  uint16_t vdd_mv = 0U;
  uint32_t duty_permille = 0U;

  (void)templogpro_power_read_vdd_mv(&vdd_mv);
  templogpro_power_get_stats(&stats);
  if (stats.uptime_ms != 0U) {
    uint64_t duty = ((uint64_t)stats.active_cpu_ms * 1000U) / stats.uptime_ms;
    duty_permille = (duty > 1000U) ? 1000U : (uint32_t)duty;
  }

  cdc_queue_text(pdev, "POWER STATUS source=");
  cdc_queue_text(pdev, stats.usb_present ? "USB" : "BATTERY");
  cdc_queue_text(pdev, " vdd_mv=");
  cdc_queue_u32(pdev, vdd_mv);
  cdc_queue_text(pdev, " vdd_min_mv=");
  cdc_queue_u32(pdev, stats.vdd_min_millivolts);
  cdc_queue_text(pdev, " vdd_max_mv=");
  cdc_queue_u32(pdev, stats.vdd_max_millivolts);
  cdc_queue_text(pdev, " clock_hz=");
  cdc_queue_u32(pdev, stats.system_clock_hz);
  cdc_queue_text(pdev, " uptime_ms=");
  cdc_queue_u32(pdev, stats.uptime_ms);
  cdc_queue_text(pdev, " active_ms_est=");
  cdc_queue_u32(pdev, stats.active_cpu_ms);
  cdc_queue_text(pdev, " duty_permille_est=");
  cdc_queue_u32(pdev, duty_permille);
  cdc_queue_text(pdev, " idle_ms=");
  cdc_queue_u32(pdev, stats.idle_sleep_ms);
  cdc_queue_text(pdev, " stop_ms=");
  cdc_queue_u32(pdev, stats.stop_sleep_ms);
  cdc_queue_text(pdev, " idle_entries=");
  cdc_queue_u32(pdev, stats.idle_entries);
  cdc_queue_text(pdev, " stop_entries=");
  cdc_queue_u32(pdev, stats.stop_entries);
  cdc_queue_text(pdev, " rtc_wakeups=");
  cdc_queue_u32(pdev, stats.rtc_wakeups);
  cdc_queue_text(pdev, " button_wakeups=");
  cdc_queue_u32(pdev, stats.button_wakeups);
  cdc_queue_text(pdev, " stop_deferred=");
  cdc_queue_u32(pdev, stats.stop_deferred);
  cdc_queue_text(pdev, "\n");
}

static void cdc_queue_logger_action(USBD_HandleTypeDef *pdev, const char *name, uint32_t ok)
{
  if (ok != 0U) {
    templogpro_usb_files_snapshot_reset();
    cdc_queue_text(pdev, "OK LOGGER ");
    cdc_queue_text(pdev, name);
    cdc_queue_text(pdev, "\n");
  } else {
    cdc_queue_text(pdev, "ERR E003 logger ");
    cdc_queue_text(pdev, name);
    cdc_queue_text(pdev, " failed\n");
  }
}

static void cdc_process_char(USBD_HandleTypeDef *pdev, uint8_t ch)
{
  if (ch == '\r') {
    return;
  }
  if (ch == '\n') {
    cdc_line[cdc_line_len] = '\0';
    if (cdc_line_len != 0U) {
      cdc_process_line(pdev, cdc_line);
    }
    cdc_line_len = 0U;
    return;
  }
  if (cdc_line_len < (CDC_LINE_SIZE - 1U)) {
    cdc_line[cdc_line_len++] = (char)ch;
  } else {
    cdc_line_len = 0U;
    cdc_queue_text(pdev, "ERR E002 line too long\n");
  }
}

static void cdc_process_line(USBD_HandleTypeDef *pdev, const char *line)
{
  TempLogProDateTime dt;
  const char *arg;

  line = cdc_skip_spaces(line);

  if (cdc_collect_config != 0U) {
    if (cdc_text_equals(line, "CONFIG END") != 0U) {
      cdc_collect_config = 0U;
      if (cdc_config_overflow != 0U) {
        cdc_queue_text(pdev, "ERR E002 config too long\n");
      } else if (templogpro_config_write_text((const uint8_t *)cdc_config_text, cdc_config_len) != 0U) {
        templogpro_app_apply_config();
        templogpro_usb_files_snapshot_reset();
        cdc_queue_text(pdev, "OK CONFIG\n");
      } else {
        cdc_queue_text(pdev, "ERR E004 config write failed\n");
      }
      cdc_config_len = 0U;
      cdc_config_overflow = 0U;
      return;
    }

    if (cdc_config_len + strlen(line) + 1U < CDC_CONFIG_TEXT_SIZE) {
      uint32_t len = (uint32_t)strlen(line);
      memcpy(&cdc_config_text[cdc_config_len], line, len);
      cdc_config_len = (uint16_t)(cdc_config_len + len);
      cdc_config_text[cdc_config_len++] = '\n';
    } else {
      cdc_config_overflow = 1U;
    }
    return;
  }

  if (cdc_text_equals(line, "PING") != 0U) {
    cdc_queue_text(pdev, "OK PING\n");
  } else if (cdc_text_equals(line, "GET STATUS") != 0U) {
    cdc_queue_text(pdev, "STATUS usb=cdc msc=1\n");
  } else if ((cdc_text_equals(line, "LOGGER STATUS") != 0U) ||
             (cdc_text_equals(line, "GET LOGGER") != 0U)) {
    cdc_queue_logger_status(pdev);
  } else if ((cdc_text_equals(line, "POWER STATUS") != 0U) ||
             (cdc_text_equals(line, "GET POWER") != 0U)) {
    cdc_queue_power_status(pdev);
  } else if (cdc_text_equals(line, "LOGGER START") != 0U) {
    if (templogpro_app_usb_report_mode()) {
      cdc_queue_text(pdev, "ERR E007 USB report mode; enable usb_logging first\n");
    } else {
      cdc_queue_logger_action(pdev, "START", templogpro_app_start_logging());
    }
  } else if ((cdc_text_equals(line, "LOGGER PAUSE") != 0U) ||
             (cdc_text_equals(line, "LOGGER STOP") != 0U)) {
    cdc_queue_logger_action(pdev, "PAUSE", templogpro_app_stop_logging());
  } else if (cdc_text_equals(line, "LOGGER CLEAR") != 0U) {
    cdc_queue_text(pdev, "ERR E006 use LOGGER CLEAR CONFIRM\n");
  } else if (cdc_text_equals(line, "LOGGER CLEAR CONFIRM") != 0U) {
    cdc_queue_logger_action(pdev, "CLEAR", templogpro_app_clear_logger());
  } else if (cdc_text_equals(line, "POWER OFF") != 0U) {
    if (templogpro_app_power_off_arm() != 0U) {
      templogpro_usb_files_snapshot_reset();
      cdc_queue_text(pdev, "OK POWER OFF\n");
    } else {
      cdc_queue_text(pdev, "ERR E003 power off failed\n");
    }
  } else if (cdc_text_equals(line, "POWER OFF CANCEL") != 0U) {
    templogpro_app_power_off_cancel();
    cdc_queue_text(pdev, "OK POWER OFF CANCEL\n");
  } else if (cdc_text_equals(line, "GET CONFIG") != 0U) {
    cdc_queue_config(pdev);
  } else if (cdc_text_equals(line, "CONFIG BEGIN") != 0U) {
    cdc_collect_config = 1U;
    cdc_config_len = 0U;
    cdc_config_overflow = 0U;
    cdc_queue_text(pdev, "OK CONFIG BEGIN\n");
  } else if (cdc_text_equals(line, "GET TIME") != 0U) {
    cdc_queue_time(pdev);
  } else if (cdc_text_starts(line, "SET TIME ") != 0U) {
    arg = cdc_skip_spaces(line + 9);
    if ((cdc_parse_datetime(arg, &dt) != 0U) && (templogpro_rtc_set(&dt) != 0U)) {
      cdc_queue_text(pdev, "OK TIME\n");
    } else {
      cdc_queue_text(pdev, "ERR E005 rtc update failed\n");
    }
  } else {
    cdc_queue_text(pdev, "ERR E001 unknown command\n");
  }
}

static uint32_t cdc_text_equals(const char *text, const char *token)
{
  return (strcmp(text, token) == 0) ? 1U : 0U;
}

static uint32_t cdc_text_starts(const char *text, const char *prefix)
{
  while (*prefix != '\0') {
    if (*text != *prefix) {
      return 0U;
    }
    text++;
    prefix++;
  }
  return 1U;
}

static const char *cdc_skip_spaces(const char *text)
{
  while ((*text == ' ') || (*text == '\t')) {
    text++;
  }
  return text;
}

static uint32_t cdc_parse_datetime(const char *text, TempLogProDateTime *dt)
{
  uint32_t year;
  uint32_t month;
  uint32_t day;
  uint32_t hour;
  uint32_t minute;
  uint32_t second;

  if ((text == NULL) || (dt == NULL)) {
    return 0U;
  }
  if (strlen(text) < 19U) {
    return 0U;
  }
  if ((text[4] != '-') || (text[7] != '-') || (text[10] != ' ') ||
      (text[13] != ':') || (text[16] != ':')) {
    return 0U;
  }
  if ((cdc_parse_digit(text, 0U, 4U, &year) == 0U) ||
      (cdc_parse_digit(text, 5U, 2U, &month) == 0U) ||
      (cdc_parse_digit(text, 8U, 2U, &day) == 0U) ||
      (cdc_parse_digit(text, 11U, 2U, &hour) == 0U) ||
      (cdc_parse_digit(text, 14U, 2U, &minute) == 0U) ||
      (cdc_parse_digit(text, 17U, 2U, &second) == 0U)) {
    return 0U;
  }

  if ((year < 2024U) || (year > 2099U) || (month == 0U) || (month > 12U) ||
      (day == 0U) || (day > 31U) || (hour > 23U) || (minute > 59U) ||
      (second > 59U)) {
    return 0U;
  }

  dt->year = (uint16_t)year;
  dt->month = (uint8_t)month;
  dt->day = (uint8_t)day;
  dt->hour = (uint8_t)hour;
  dt->minute = (uint8_t)minute;
  dt->second = (uint8_t)second;
  return 1U;
}

static uint32_t cdc_parse_digit(const char *text, uint32_t offset, uint32_t digits, uint32_t *value)
{
  uint32_t i;
  uint32_t out = 0U;

  for (i = 0U; i < digits; i++) {
    char ch = text[offset + i];
    if ((ch < '0') || (ch > '9')) {
      return 0U;
    }
    out = (out * 10U) + (uint32_t)(ch - '0');
  }

  *value = out;
  return 1U;
}

static void cdc_queue_config(USBD_HandleTypeDef *pdev)
{
  uint8_t text[TEMPLOGPRO_CONFIG_TEXT_SIZE];
  uint32_t len;

  (void)templogpro_config_read_text(0U, text, sizeof(text));
  len = cdc_trimmed_config_len((const char *)text, sizeof(text));
  cdc_queue_text(pdev, "CONFIG BEGIN\n");
  cdc_queue_bytes(pdev, (const char *)text, len);
  cdc_queue_text(pdev, "\nCONFIG END\n");
}

static uint32_t cdc_trimmed_config_len(const char *text, uint32_t len)
{
  while ((len != 0U) &&
         ((text[len - 1U] == ' ') || (text[len - 1U] == '\0') ||
          (text[len - 1U] == '\r') || (text[len - 1U] == '\n'))) {
    len--;
  }
  return len;
}

static const char *cdc_app_state_name(TempLogProState state)
{
  switch (state) {
  case TEMPLOGPRO_STATE_READY:
    return "READY";
  case TEMPLOGPRO_STATE_RUN:
    return "RUN";
  case TEMPLOGPRO_STATE_PAUSE:
    return "PAUSE";
  case TEMPLOGPRO_STATE_STARTING:
    return "STARTING";
  case TEMPLOGPRO_STATE_STOPPING:
    return "STOPPING";
  case TEMPLOGPRO_STATE_CLEARING:
    return "CLEARING";
  case TEMPLOGPRO_STATE_ERROR:
  default:
    return "ERROR";
  }
}

static const char *cdc_logger_state_name(TempLogProLoggerState state)
{
  switch (state) {
  case TEMPLOGPRO_LOGGER_IDLE:
    return "IDLE";
  case TEMPLOGPRO_LOGGER_LOGGING:
    return "LOGGING";
  case TEMPLOGPRO_LOGGER_STOPPED:
    return "STOPPED";
  case TEMPLOGPRO_LOGGER_ERROR:
  default:
    return "ERROR";
  }
}

/**
* @brief  USBD_MSC_GetHSCfgDesc
*         return configuration descriptor
* @param  length : pointer data length
* @retval pointer to descriptor buffer
*/
uint8_t  *USBD_MSC_GetHSCfgDesc (uint16_t *length)
{
  *length = sizeof (USBD_MSC_CfgHSDesc);
  return USBD_MSC_CfgHSDesc;
}

/**
* @brief  USBD_MSC_GetFSCfgDesc
*         return configuration descriptor
* @param  length : pointer data length
* @retval pointer to descriptor buffer
*/
uint8_t  *USBD_MSC_GetFSCfgDesc (uint16_t *length)
{
  *length = sizeof (USBD_MSC_CfgFSDesc);
  return USBD_MSC_CfgFSDesc;
}

/**
* @brief  USBD_MSC_GetOtherSpeedCfgDesc
*         return other speed configuration descriptor
* @param  length : pointer data length
* @retval pointer to descriptor buffer
*/
uint8_t  *USBD_MSC_GetOtherSpeedCfgDesc (uint16_t *length)
{
  *length = sizeof (USBD_MSC_OtherSpeedCfgDesc);
  return USBD_MSC_OtherSpeedCfgDesc;
}
/**
* @brief  DeviceQualifierDescriptor
*         return Device Qualifier descriptor
* @param  length : pointer data length
* @retval pointer to descriptor buffer
*/
uint8_t  *USBD_MSC_GetDeviceQualifierDescriptor (uint16_t *length)
{
  *length = sizeof (USBD_MSC_DeviceQualifierDesc);
  return USBD_MSC_DeviceQualifierDesc;
}

/**
* @brief  USBD_MSC_RegisterStorage
* @param  fops: storage callback
* @retval status
*/
uint8_t  USBD_MSC_RegisterStorage  (USBD_HandleTypeDef   *pdev,
                                    USBD_StorageTypeDef *fops)
{
  if(fops != NULL)
  {
    pdev->pUserData = fops;
  }
  return USBD_OK;
}

/**
  * @}
  */


/**
  * @}
  */


/**
  * @}
  */

/************************ (C) COPYRIGHT STMicroelectronics *****END OF FILE****/
