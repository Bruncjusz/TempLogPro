################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (14.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../Src/st_usb/msc/usbd_msc.c \
../Src/st_usb/msc/usbd_msc_bot.c \
../Src/st_usb/msc/usbd_msc_data.c \
../Src/st_usb/msc/usbd_msc_scsi.c 

OBJS += \
./Src/st_usb/msc/usbd_msc.o \
./Src/st_usb/msc/usbd_msc_bot.o \
./Src/st_usb/msc/usbd_msc_data.o \
./Src/st_usb/msc/usbd_msc_scsi.o 

C_DEPS += \
./Src/st_usb/msc/usbd_msc.d \
./Src/st_usb/msc/usbd_msc_bot.d \
./Src/st_usb/msc/usbd_msc_data.d \
./Src/st_usb/msc/usbd_msc_scsi.d 


# Each subdirectory must supply rules for building sources it contributes
Src/st_usb/msc/%.o Src/st_usb/msc/%.su Src/st_usb/msc/%.cyclo: ../Src/st_usb/msc/%.c Src/st_usb/msc/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m3 -std=gnu11 -DSTM32L1 -DSTM32L152RCTx -DSTM32L152xC -DSTM32 -c -I../Inc -I../Drivers/STM32L1xx_HAL_Driver/Inc -I../Drivers/STM32L1xx_HAL_Driver/Inc/Legacy -I../Drivers/CMSIS/Device/ST/STM32L1xx/Include -I../Drivers/CMSIS/Include -I../Middlewares/ST/STM32_USB_Device_Library/Core/Inc -I../Middlewares/ST/STM32_USB_Device_Library/Class/HID/Inc -I../Middlewares/ST/STM32_USB_Device_Library/Class/MSC/Inc -Os -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfloat-abi=soft -mthumb -o "$@"

clean: clean-Src-2f-st_usb-2f-msc

clean-Src-2f-st_usb-2f-msc:
	-$(RM) ./Src/st_usb/msc/usbd_msc.cyclo ./Src/st_usb/msc/usbd_msc.d ./Src/st_usb/msc/usbd_msc.o ./Src/st_usb/msc/usbd_msc.su ./Src/st_usb/msc/usbd_msc_bot.cyclo ./Src/st_usb/msc/usbd_msc_bot.d ./Src/st_usb/msc/usbd_msc_bot.o ./Src/st_usb/msc/usbd_msc_bot.su ./Src/st_usb/msc/usbd_msc_data.cyclo ./Src/st_usb/msc/usbd_msc_data.d ./Src/st_usb/msc/usbd_msc_data.o ./Src/st_usb/msc/usbd_msc_data.su ./Src/st_usb/msc/usbd_msc_scsi.cyclo ./Src/st_usb/msc/usbd_msc_scsi.d ./Src/st_usb/msc/usbd_msc_scsi.o ./Src/st_usb/msc/usbd_msc_scsi.su

.PHONY: clean-Src-2f-st_usb-2f-msc

