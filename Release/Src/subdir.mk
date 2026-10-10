################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (14.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../Src/board.c \
../Src/drv_adc.c \
../Src/drv_lcd.c \
../Src/drv_sensor_i2c.c \
../Src/drv_w25x40.c \
../Src/main.c \
../Src/syscalls.c \
../Src/sysmem.c \
../Src/templogpro_app.c \
../Src/templogpro_buttons.c \
../Src/templogpro_config.c \
../Src/templogpro_diag.c \
../Src/templogpro_logger.c \
../Src/templogpro_pdf.c \
../Src/templogpro_power.c \
../Src/templogpro_rtc.c \
../Src/templogpro_sensor.c \
../Src/templogpro_storage_layout.c \
../Src/templogpro_usb_files.c 

OBJS += \
./Src/board.o \
./Src/drv_adc.o \
./Src/drv_lcd.o \
./Src/drv_sensor_i2c.o \
./Src/drv_w25x40.o \
./Src/main.o \
./Src/syscalls.o \
./Src/sysmem.o \
./Src/templogpro_app.o \
./Src/templogpro_buttons.o \
./Src/templogpro_config.o \
./Src/templogpro_diag.o \
./Src/templogpro_logger.o \
./Src/templogpro_pdf.o \
./Src/templogpro_power.o \
./Src/templogpro_rtc.o \
./Src/templogpro_sensor.o \
./Src/templogpro_storage_layout.o \
./Src/templogpro_usb_files.o 

C_DEPS += \
./Src/board.d \
./Src/drv_adc.d \
./Src/drv_lcd.d \
./Src/drv_sensor_i2c.d \
./Src/drv_w25x40.d \
./Src/main.d \
./Src/syscalls.d \
./Src/sysmem.d \
./Src/templogpro_app.d \
./Src/templogpro_buttons.d \
./Src/templogpro_config.d \
./Src/templogpro_diag.d \
./Src/templogpro_logger.d \
./Src/templogpro_pdf.d \
./Src/templogpro_power.d \
./Src/templogpro_rtc.d \
./Src/templogpro_sensor.d \
./Src/templogpro_storage_layout.d \
./Src/templogpro_usb_files.d 


# Each subdirectory must supply rules for building sources it contributes
Src/%.o Src/%.su Src/%.cyclo: ../Src/%.c Src/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m3 -std=gnu11 -DSTM32L1 -DSTM32L152RCTx -DSTM32L152xC -DSTM32 -c -I../Inc -I../Drivers/STM32L1xx_HAL_Driver/Inc -I../Drivers/STM32L1xx_HAL_Driver/Inc/Legacy -I../Drivers/CMSIS/Device/ST/STM32L1xx/Include -I../Drivers/CMSIS/Include -I../Middlewares/ST/STM32_USB_Device_Library/Core/Inc -I../Middlewares/ST/STM32_USB_Device_Library/Class/HID/Inc -I../Middlewares/ST/STM32_USB_Device_Library/Class/MSC/Inc -Os -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfloat-abi=soft -mthumb -o "$@"

clean: clean-Src

clean-Src:
	-$(RM) ./Src/board.cyclo ./Src/board.d ./Src/board.o ./Src/board.su ./Src/drv_adc.cyclo ./Src/drv_adc.d ./Src/drv_adc.o ./Src/drv_adc.su ./Src/drv_lcd.cyclo ./Src/drv_lcd.d ./Src/drv_lcd.o ./Src/drv_lcd.su ./Src/drv_sensor_i2c.cyclo ./Src/drv_sensor_i2c.d ./Src/drv_sensor_i2c.o ./Src/drv_sensor_i2c.su ./Src/drv_w25x40.cyclo ./Src/drv_w25x40.d ./Src/drv_w25x40.o ./Src/drv_w25x40.su ./Src/main.cyclo ./Src/main.d ./Src/main.o ./Src/main.su ./Src/syscalls.cyclo ./Src/syscalls.d ./Src/syscalls.o ./Src/syscalls.su ./Src/sysmem.cyclo ./Src/sysmem.d ./Src/sysmem.o ./Src/sysmem.su ./Src/templogpro_app.cyclo ./Src/templogpro_app.d ./Src/templogpro_app.o ./Src/templogpro_app.su ./Src/templogpro_buttons.cyclo ./Src/templogpro_buttons.d ./Src/templogpro_buttons.o ./Src/templogpro_buttons.su ./Src/templogpro_config.cyclo ./Src/templogpro_config.d ./Src/templogpro_config.o ./Src/templogpro_config.su ./Src/templogpro_diag.cyclo ./Src/templogpro_diag.d ./Src/templogpro_diag.o ./Src/templogpro_diag.su ./Src/templogpro_logger.cyclo ./Src/templogpro_logger.d ./Src/templogpro_logger.o ./Src/templogpro_logger.su ./Src/templogpro_pdf.cyclo ./Src/templogpro_pdf.d ./Src/templogpro_pdf.o ./Src/templogpro_pdf.su ./Src/templogpro_power.cyclo ./Src/templogpro_power.d ./Src/templogpro_power.o ./Src/templogpro_power.su ./Src/templogpro_rtc.cyclo ./Src/templogpro_rtc.d ./Src/templogpro_rtc.o ./Src/templogpro_rtc.su ./Src/templogpro_sensor.cyclo ./Src/templogpro_sensor.d ./Src/templogpro_sensor.o ./Src/templogpro_sensor.su ./Src/templogpro_storage_layout.cyclo ./Src/templogpro_storage_layout.d ./Src/templogpro_storage_layout.o ./Src/templogpro_storage_layout.su ./Src/templogpro_usb_files.cyclo ./Src/templogpro_usb_files.d ./Src/templogpro_usb_files.o ./Src/templogpro_usb_files.su

.PHONY: clean-Src

