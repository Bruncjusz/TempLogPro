/*
 * STM32L152 register map used by the board drivers (board.c, drv_*.c) and the
 * module-probe diagnostic image. Raw addresses on purpose: the names clash with
 * the CMSIS/HAL headers, so never include this file next to stm32l1xx.h.
 */
#ifndef TEMPLOGPRO_BOARD_REGS_H
#define TEMPLOGPRO_BOARD_REGS_H

#include <stdint.h>

#define REG32(a)        (*(volatile uint32_t *)(a))
#define REG16(a)        (*(volatile uint16_t *)(a))
#define REG8(a)         (*(volatile uint8_t *)(a))

#define FLASH_ACR       REG32(0x40023C00u)
#define RCC_CR          REG32(0x40023800u)
#define RCC_CFGR        REG32(0x40023808u)
#define RCC_CSR         REG32(0x40023834u)
#define RCC_AHBENR      REG32(0x4002381Cu)
#define RCC_APB2ENR     REG32(0x40023820u)
#define RCC_APB1ENR     REG32(0x40023824u)
#define SCB_SCR         REG32(0xE000ED10u)

#define PWR_CR          REG32(0x40007000u)
#define PWR_CSR         REG32(0x40007004u)

#define SYSCFG_BASE     0x40010000u
#define SYSCFG_PMC      REG32(SYSCFG_BASE + 0x04u)

#define GPIOA_BASE      0x40020000u
#define GPIOB_BASE      0x40020400u
#define GPIOC_BASE      0x40020800u
#define GPIOD_BASE      0x40020C00u
#define GPIOE_BASE      0x40021000u

#define GPIO_MODER(b)   REG32((b) + 0x00u)
#define GPIO_OTYPER(b)  REG32((b) + 0x04u)
#define GPIO_OSPEEDR(b) REG32((b) + 0x08u)
#define GPIO_PUPDR(b)   REG32((b) + 0x0Cu)
#define GPIO_IDR(b)     REG32((b) + 0x10u)
#define GPIO_ODR(b)     REG32((b) + 0x14u)
#define GPIO_AFRL(b)    REG32((b) + 0x20u)
#define GPIO_AFRH(b)    REG32((b) + 0x24u)
#define GPIO_BSRR(b)    REG32((b) + 0x18u)

#define LCD_BASE        0x40002400u
#define LCD_CR          REG32(LCD_BASE + 0x00u)
#define LCD_FCR         REG32(LCD_BASE + 0x04u)
#define LCD_SR          REG32(LCD_BASE + 0x08u)
#define LCD_CLR         REG32(LCD_BASE + 0x0Cu)
#define LCD_RAM(i)      REG32(LCD_BASE + 0x14u + ((i) * 4u))

#define LCD_SR_ENS_BIT   (1u << 0)
#define LCD_SR_UDR_BIT   (1u << 2)
#define LCD_SR_UDD_BIT   (1u << 3)
#define LCD_SR_RDY_BIT   (1u << 4)
#define LCD_SR_FCRSF_BIT (1u << 5)

#define SPI1_BASE       0x40013000u
#define SPI1_CR1        REG32(SPI1_BASE + 0x00u)
#define SPI1_CR2        REG32(SPI1_BASE + 0x04u)
#define SPI1_SR         REG32(SPI1_BASE + 0x08u)
#define SPI1_DR         REG32(SPI1_BASE + 0x0Cu)
#define SPI1_DR8        REG8(SPI1_BASE + 0x0Cu)

#define ADC1_BASE       0x40012400u
#define ADC1_SR         REG32(ADC1_BASE + 0x00u)
#define ADC1_CR1        REG32(ADC1_BASE + 0x04u)
#define ADC1_CR2        REG32(ADC1_BASE + 0x08u)
#define ADC1_SMPR1      REG32(ADC1_BASE + 0x0Cu)
#define ADC1_SMPR2      REG32(ADC1_BASE + 0x10u)
#define ADC1_SMPR3      REG32(ADC1_BASE + 0x14u)
#define ADC1_SQR1       REG32(ADC1_BASE + 0x30u)
#define ADC1_SQR2       REG32(ADC1_BASE + 0x34u)
#define ADC1_SQR3       REG32(ADC1_BASE + 0x38u)
#define ADC1_SQR4       REG32(ADC1_BASE + 0x3Cu)
#define ADC1_SQR5       REG32(ADC1_BASE + 0x40u)
#define ADC1_DR         REG32(ADC1_BASE + 0x58u)

#define ADC_COMMON_BASE 0x40012700u
#define ADC_CCR         REG32(ADC_COMMON_BASE + 0x04u)

#define RTC_BASE        0x40002800u

#define USB_BASE        0x40005C00u
#define USB_CNTR        REG16(USB_BASE + 0x40u)
#define USB_CNTR_FRES   (1u << 0)
#define USB_CNTR_PDWN   (1u << 1)

#define NVIC_ISER0      REG32(0xE000E100u)
#define NVIC_ICER0      REG32(0xE000E180u)
#define NVIC_USB_LP_BIT (1u << 20)   /* STM32L152 USB_LP is IRQ20 */

#define SPI_CR1_MSTR    (1u << 2)
#define SPI_CR1_BR_0    (1u << 3)
#define SPI_CR1_BR_1    (1u << 4)
#define SPI_CR1_BR_2    (1u << 5)
#define SPI_CR1_SPE     (1u << 6)
#define SPI_CR1_SSI     (1u << 8)
#define SPI_CR1_SSM     (1u << 9)
#define SPI_SR_RXNE     (1u << 0)
#define SPI_SR_TXE      (1u << 1)
#define SPI_SR_BSY      (1u << 7)

#define ADC_SR_EOC      (1u << 1)
#define ADC_SR_ADONS    (1u << 6)
#define ADC_CR1_PDD     (1u << 16)
#define ADC_CR1_PDI     (1u << 17)
#define ADC_CR2_ADON    (1u << 0)
#define ADC_CR2_EOCS    (1u << 10)
#define ADC_CR2_SWSTART (1u << 30)
#define ADC_CCR_TSVREFE (1u << 23)

#define RCC_CR_HSION           (1u << 0)
#define RCC_CR_HSIRDY          (1u << 1)
#define RCC_CR_HSEON           (1u << 16)
#define RCC_CR_HSERDY          (1u << 17)
#define RCC_CR_PLLON           (1u << 24)
#define RCC_CR_PLLRDY          (1u << 25)
#define RCC_CFGR_SWS_MASK      0x0000000Cu
#define RCC_CFGR_SWS_HSI       0x00000004u
#define RCC_CFGR_SWS_HSE       0x00000008u
#define RCC_CFGR_SWS_PLL       0x0000000Cu
#define RCC_CFGR_PLLSRC_HSE    0x00010000u

#define RCC_APB2ENR_SYSCFGEN   (1u << 0)
#define RCC_APB2ENR_ADC1EN     (1u << 9)
#define RCC_APB2ENR_SPI1EN     (1u << 12)
#define RCC_APB1ENR_LCDEN      (1u << 9)
#define RCC_APB1ENR_USBEN      (1u << 23)
#define RCC_APB1ENR_PWREN      (1u << 28)
#define PWR_CR_DBP             (1u << 8)
#define PWR_CSR_VREFINTRDYF    (1u << 3)

#endif
