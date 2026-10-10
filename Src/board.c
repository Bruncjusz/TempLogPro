/*
 * Board layer: busy-wait delay, GPIO alternate functions, the two LEDs and the
 * fault handlers. LED pins: PB8 red, PC0 green.
 */
#include "board.h"
#include "board_regs.h"
#include "templogpro_hw_bridge.h"

#define FAULT_BLINK_SLOT_LOOPS  90000u

/* Last fault, kept for nonintrusive SWD inspection: 0xF00n0000. */
volatile uint32_t g_board_fault;

void board_delay(volatile uint32_t loops)
{
    while (loops--) {
        __asm volatile ("nop");
    }
}

void board_gpio_af(uint32_t port, uint32_t mask, uint32_t af)
{
    for (uint32_t pin = 0; pin < 16; pin++) {
        if ((mask & (1u << pin)) == 0) {
            continue;
        }
        GPIO_MODER(port) &= ~(3u << (pin * 2u));
        GPIO_MODER(port) |=  (2u << (pin * 2u));
        GPIO_OTYPER(port) &= ~(1u << pin);
        GPIO_OSPEEDR(port) |= (3u << (pin * 2u));
        GPIO_PUPDR(port) &= ~(3u << (pin * 2u));

        if (pin < 8) {
            GPIO_AFRL(port) &= ~(0xFu << (pin * 4u));
            GPIO_AFRL(port) |=  ((af & 0xFu) << (pin * 4u));
        } else {
            uint32_t shift = (pin - 8u) * 4u;
            GPIO_AFRH(port) &= ~(0xFu << shift);
            GPIO_AFRH(port) |=  ((af & 0xFu) << shift);
        }
    }
}

void board_leds_init(void)
{
    RCC_AHBENR |= (1u << 1) | (1u << 2); /* GPIOB + GPIOC */

    GPIO_MODER(GPIOB_BASE) &= ~(3u << (LED_RED_PIN * 2u));
    GPIO_MODER(GPIOB_BASE) |=  (1u << (LED_RED_PIN * 2u));
    GPIO_OTYPER(GPIOB_BASE) &= ~(1u << LED_RED_PIN);
    GPIO_PUPDR(GPIOB_BASE) &= ~(3u << (LED_RED_PIN * 2u));

    GPIO_MODER(GPIOC_BASE) &= ~(3u << (LED_GREEN_PIN * 2u));
    GPIO_MODER(GPIOC_BASE) |=  (1u << (LED_GREEN_PIN * 2u));
    GPIO_OTYPER(GPIOC_BASE) &= ~(1u << LED_GREEN_PIN);
    GPIO_PUPDR(GPIOC_BASE) &= ~(3u << (LED_GREEN_PIN * 2u));
}

void board_led_green_on(void)  { GPIO_BSRR(GPIOC_BASE) = (1u << LED_GREEN_PIN); }
void board_led_green_off(void) { GPIO_BSRR(GPIOC_BASE) = (1u << (LED_GREEN_PIN + 16u)); }
void board_led_red_on(void)    { GPIO_BSRR(GPIOB_BASE) = (1u << LED_RED_PIN); }
void board_led_red_off(void)   { GPIO_BSRR(GPIOB_BASE) = (1u << (LED_RED_PIN + 16u)); }

void templogpro_hw_init_leds(void)     { board_leds_init(); }
void templogpro_hw_led_green_on(void)  { board_led_green_on(); }
void templogpro_hw_led_green_off(void) { board_led_green_off(); }
void templogpro_hw_led_red_on(void)    { board_led_red_on(); }
void templogpro_hw_led_red_off(void)   { board_led_red_off(); }

/* Green steady, red blinks `code` times in a loop: 10 HardFault, 11 MemManage,
   12 BusFault, 13 UsageFault. */
static void fault_blink_forever(uint32_t code)
{
    board_leds_init();
    board_led_green_on();

    for (;;) {
        board_led_red_off();
        board_delay(FAULT_BLINK_SLOT_LOOPS * 3u);
        for (uint32_t i = 0u; i < code; i++) {
            board_led_red_on();
            board_delay(FAULT_BLINK_SLOT_LOOPS);
            board_led_red_off();
            board_delay(FAULT_BLINK_SLOT_LOOPS);
        }
        board_delay(FAULT_BLINK_SLOT_LOOPS * 11u);
    }
}

void HardFault_Handler(void)
{
    g_board_fault = 0xF0010000u;
    fault_blink_forever(10u);
}

void MemManage_Handler(void)
{
    g_board_fault = 0xF0020000u;
    fault_blink_forever(11u);
}

void BusFault_Handler(void)
{
    g_board_fault = 0xF0030000u;
    fault_blink_forever(12u);
}

void UsageFault_Handler(void)
{
    g_board_fault = 0xF0040000u;
    fault_blink_forever(13u);
}
