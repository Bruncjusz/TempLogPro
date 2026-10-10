/* Board-level helpers shared by the drivers in Src/board.c and Src/drv_*.c. */
#ifndef TEMPLOGPRO_BOARD_H
#define TEMPLOGPRO_BOARD_H

#include <stdint.h>

#define LED_RED_PIN     8u   /* PB8 */
#define LED_GREEN_PIN   0u   /* PC0 */

#define BOARD_AF5_SPI1  0x5u
#define BOARD_AF11_LCD  0xBu

int HAL_Init(void);
uint32_t HAL_GetTick(void);

/* Busy-wait of roughly `loops` iterations; scales with the core clock. */
void board_delay(volatile uint32_t loops);
/* Puts every pin of `mask` into alternate function `af`, push-pull, very high speed. */
void board_gpio_af(uint32_t port, uint32_t mask, uint32_t af);

void board_leds_init(void);
void board_led_green_on(void);
void board_led_green_off(void);
void board_led_red_on(void);
void board_led_red_off(void);

#endif
