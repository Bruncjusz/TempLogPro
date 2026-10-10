################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (14.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../Src/st_usb/templogpro_usb_msc_storage.c \
../Src/st_usb/templogpro_usb_st_hid.c \
../Src/st_usb/templogpro_usb_st_it.c \
../Src/st_usb/usbd_conf.c 

OBJS += \
./Src/st_usb/templogpro_usb_msc_storage.o \
./Src/st_usb/templogpro_usb_st_hid.o \
./Src/st_usb/templogpro_usb_st_it.o \
./Src/st_usb/usbd_conf.o 

C_DEPS += \
./Src/st_usb/templogpro_usb_msc_storage.d \
./Src/st_usb/templogpro_usb_st_hid.d \
./Src/st_usb/templogpro_usb_st_it.d \
./Src/st_usb/usbd_conf.d 


# Each subdirectory must supply rules for building sources it contributes
Src/st_usb/%.o Src/st_usb/%.su Src/st_usb/%.cyclo: ../Src/st_usb/%.c Src/st_usb/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m3 -std=gnu11 -DSTM32L1 -DSTM32L152RCTx -DSTM32L152xC -DSTM32 -c -I../Inc -I../Drivers/STM32L1xx_HAL_Driver/Inc -I../Drivers/STM32L1xx_HAL_Driver/Inc/Legacy -I../Drivers/CMSIS/Device/ST/STM32L1xx/Include -I../Drivers/CMSIS/Include -I../Middlewares/ST/STM32_USB_Device_Library/Core/Inc -I../Middlewares/ST/STM32_USB_Device_Library/Class/HID/Inc -I../Middlewares/ST/STM32_USB_Device_Library/Class/MSC/Inc -Os -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfloat-abi=soft -mthumb -o "$@"

clean: clean-Src-2f-st_usb

clean-Src-2f-st_usb:
	-$(RM) ./Src/st_usb/templogpro_usb_msc_storage.cyclo ./Src/st_usb/templogpro_usb_msc_storage.d ./Src/st_usb/templogpro_usb_msc_storage.o ./Src/st_usb/templogpro_usb_msc_storage.su ./Src/st_usb/templogpro_usb_st_hid.cyclo ./Src/st_usb/templogpro_usb_st_hid.d ./Src/st_usb/templogpro_usb_st_hid.o ./Src/st_usb/templogpro_usb_st_hid.su ./Src/st_usb/templogpro_usb_st_it.cyclo ./Src/st_usb/templogpro_usb_st_it.d ./Src/st_usb/templogpro_usb_st_it.o ./Src/st_usb/templogpro_usb_st_it.su ./Src/st_usb/usbd_conf.cyclo ./Src/st_usb/usbd_conf.d ./Src/st_usb/usbd_conf.o ./Src/st_usb/usbd_conf.su

.PHONY: clean-Src-2f-st_usb

