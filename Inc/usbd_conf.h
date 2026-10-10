#ifndef __USBD_CONF_H
#define __USBD_CONF_H

#include "stm32l1xx_hal.h"
#include <stdint.h>
#include <string.h>

#define USBD_MAX_NUM_INTERFACES              3U
#define USBD_MAX_NUM_CONFIGURATION           1U
#define USBD_MAX_STR_DESC_SIZ                0x100U
#define USBD_SUPPORT_USER_STRING             0U
#define USBD_SELF_POWERED                    0U
#define USBD_DEBUG_LEVEL                     0U

void *USBD_static_malloc(uint32_t size);
void USBD_static_free(void *p);

#define MAX_STATIC_ALLOC_SIZE                1024U
#define USBD_malloc                          (uint32_t *)USBD_static_malloc
#define USBD_free                            USBD_static_free
#define USBD_memset                          memset
#define USBD_memcpy                          memcpy

#if (USBD_DEBUG_LEVEL > 0U)
#define USBD_UsrLog(...)
#else
#define USBD_UsrLog(...)
#endif

#if (USBD_DEBUG_LEVEL > 1U)
#define USBD_ErrLog(...)
#else
#define USBD_ErrLog(...)
#endif

#if (USBD_DEBUG_LEVEL > 2U)
#define USBD_DbgLog(...)
#else
#define USBD_DbgLog(...)
#endif

#endif
