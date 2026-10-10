################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (14.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../Src/st_usb/hal/stm32l1xx_hal.c \
../Src/st_usb/hal/stm32l1xx_hal_cortex.c \
../Src/st_usb/hal/stm32l1xx_hal_gpio.c \
../Src/st_usb/hal/stm32l1xx_hal_pcd.c \
../Src/st_usb/hal/stm32l1xx_hal_pcd_ex.c \
../Src/st_usb/hal/stm32l1xx_hal_pwr.c \
../Src/st_usb/hal/stm32l1xx_hal_rcc.c \
../Src/st_usb/hal/stm32l1xx_ll_usb.c \
../Src/st_usb/hal/system_stm32l1xx.c 

OBJS += \
./Src/st_usb/hal/stm32l1xx_hal.o \
./Src/st_usb/hal/stm32l1xx_hal_cortex.o \
./Src/st_usb/hal/stm32l1xx_hal_gpio.o \
./Src/st_usb/hal/stm32l1xx_hal_pcd.o \
./Src/st_usb/hal/stm32l1xx_hal_pcd_ex.o \
./Src/st_usb/hal/stm32l1xx_hal_pwr.o \
./Src/st_usb/hal/stm32l1xx_hal_rcc.o \
./Src/st_usb/hal/stm32l1xx_ll_usb.o \
./Src/st_usb/hal/system_stm32l1xx.o 

C_DEPS += \
./Src/st_usb/hal/stm32l1xx_hal.d \
./Src/st_usb/hal/stm32l1xx_hal_cortex.d \
./Src/st_usb/hal/stm32l1xx_hal_gpio.d \
./Src/st_usb/hal/stm32l1xx_hal_pcd.d \
./Src/st_usb/hal/stm32l1xx_hal_pcd_ex.d \
./Src/st_usb/hal/stm32l1xx_hal_pwr.d \
./Src/st_usb/hal/stm32l1xx_hal_rcc.d \
./Src/st_usb/hal/stm32l1xx_ll_usb.d \
./Src/st_usb/hal/system_stm32l1xx.d 


# Each subdirectory must supply rules for building sources it contributes
Src/st_usb/hal/%.o Src/st_usb/hal/%.su Src/st_usb/hal/%.cyclo: ../Src/st_usb/hal/%.c Src/st_usb/hal/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m3 -std=gnu11 -DSTM32L1 -DSTM32L152RCTx -DSTM32L152xC -DSTM32 -c -I../Inc -I../Drivers/STM32L1xx_HAL_Driver/Inc -I../Drivers/STM32L1xx_HAL_Driver/Inc/Legacy -I../Drivers/CMSIS/Device/ST/STM32L1xx/Include -I../Drivers/CMSIS/Include -I../Middlewares/ST/STM32_USB_Device_Library/Core/Inc -I../Middlewares/ST/STM32_USB_Device_Library/Class/HID/Inc -I../Middlewares/ST/STM32_USB_Device_Library/Class/MSC/Inc -Os -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfloat-abi=soft -mthumb -o "$@"

clean: clean-Src-2f-st_usb-2f-hal

clean-Src-2f-st_usb-2f-hal:
	-$(RM) ./Src/st_usb/hal/stm32l1xx_hal.cyclo ./Src/st_usb/hal/stm32l1xx_hal.d ./Src/st_usb/hal/stm32l1xx_hal.o ./Src/st_usb/hal/stm32l1xx_hal.su ./Src/st_usb/hal/stm32l1xx_hal_cortex.cyclo ./Src/st_usb/hal/stm32l1xx_hal_cortex.d ./Src/st_usb/hal/stm32l1xx_hal_cortex.o ./Src/st_usb/hal/stm32l1xx_hal_cortex.su ./Src/st_usb/hal/stm32l1xx_hal_gpio.cyclo ./Src/st_usb/hal/stm32l1xx_hal_gpio.d ./Src/st_usb/hal/stm32l1xx_hal_gpio.o ./Src/st_usb/hal/stm32l1xx_hal_gpio.su ./Src/st_usb/hal/stm32l1xx_hal_pcd.cyclo ./Src/st_usb/hal/stm32l1xx_hal_pcd.d ./Src/st_usb/hal/stm32l1xx_hal_pcd.o ./Src/st_usb/hal/stm32l1xx_hal_pcd.su ./Src/st_usb/hal/stm32l1xx_hal_pcd_ex.cyclo ./Src/st_usb/hal/stm32l1xx_hal_pcd_ex.d ./Src/st_usb/hal/stm32l1xx_hal_pcd_ex.o ./Src/st_usb/hal/stm32l1xx_hal_pcd_ex.su ./Src/st_usb/hal/stm32l1xx_hal_pwr.cyclo ./Src/st_usb/hal/stm32l1xx_hal_pwr.d ./Src/st_usb/hal/stm32l1xx_hal_pwr.o ./Src/st_usb/hal/stm32l1xx_hal_pwr.su ./Src/st_usb/hal/stm32l1xx_hal_rcc.cyclo ./Src/st_usb/hal/stm32l1xx_hal_rcc.d ./Src/st_usb/hal/stm32l1xx_hal_rcc.o ./Src/st_usb/hal/stm32l1xx_hal_rcc.su ./Src/st_usb/hal/stm32l1xx_ll_usb.cyclo ./Src/st_usb/hal/stm32l1xx_ll_usb.d ./Src/st_usb/hal/stm32l1xx_ll_usb.o ./Src/st_usb/hal/stm32l1xx_ll_usb.su ./Src/st_usb/hal/system_stm32l1xx.cyclo ./Src/st_usb/hal/system_stm32l1xx.d ./Src/st_usb/hal/system_stm32l1xx.o ./Src/st_usb/hal/system_stm32l1xx.su

.PHONY: clean-Src-2f-st_usb-2f-hal

