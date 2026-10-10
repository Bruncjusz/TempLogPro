################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (14.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../Src/st_usb/usbd/usbd_core.c \
../Src/st_usb/usbd/usbd_ctlreq.c \
../Src/st_usb/usbd/usbd_desc.c \
../Src/st_usb/usbd/usbd_hid.c \
../Src/st_usb/usbd/usbd_ioreq.c 

OBJS += \
./Src/st_usb/usbd/usbd_core.o \
./Src/st_usb/usbd/usbd_ctlreq.o \
./Src/st_usb/usbd/usbd_desc.o \
./Src/st_usb/usbd/usbd_hid.o \
./Src/st_usb/usbd/usbd_ioreq.o 

C_DEPS += \
./Src/st_usb/usbd/usbd_core.d \
./Src/st_usb/usbd/usbd_ctlreq.d \
./Src/st_usb/usbd/usbd_desc.d \
./Src/st_usb/usbd/usbd_hid.d \
./Src/st_usb/usbd/usbd_ioreq.d 


# Each subdirectory must supply rules for building sources it contributes
Src/st_usb/usbd/%.o Src/st_usb/usbd/%.su Src/st_usb/usbd/%.cyclo: ../Src/st_usb/usbd/%.c Src/st_usb/usbd/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m3 -std=gnu11 -DSTM32L1 -DSTM32L152RCTx -DSTM32L152xC -DSTM32 -c -I../Inc -I../Drivers/STM32L1xx_HAL_Driver/Inc -I../Drivers/STM32L1xx_HAL_Driver/Inc/Legacy -I../Drivers/CMSIS/Device/ST/STM32L1xx/Include -I../Drivers/CMSIS/Include -I../Middlewares/ST/STM32_USB_Device_Library/Core/Inc -I../Middlewares/ST/STM32_USB_Device_Library/Class/HID/Inc -I../Middlewares/ST/STM32_USB_Device_Library/Class/MSC/Inc -Os -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfloat-abi=soft -mthumb -o "$@"

clean: clean-Src-2f-st_usb-2f-usbd

clean-Src-2f-st_usb-2f-usbd:
	-$(RM) ./Src/st_usb/usbd/usbd_core.cyclo ./Src/st_usb/usbd/usbd_core.d ./Src/st_usb/usbd/usbd_core.o ./Src/st_usb/usbd/usbd_core.su ./Src/st_usb/usbd/usbd_ctlreq.cyclo ./Src/st_usb/usbd/usbd_ctlreq.d ./Src/st_usb/usbd/usbd_ctlreq.o ./Src/st_usb/usbd/usbd_ctlreq.su ./Src/st_usb/usbd/usbd_desc.cyclo ./Src/st_usb/usbd/usbd_desc.d ./Src/st_usb/usbd/usbd_desc.o ./Src/st_usb/usbd/usbd_desc.su ./Src/st_usb/usbd/usbd_hid.cyclo ./Src/st_usb/usbd/usbd_hid.d ./Src/st_usb/usbd/usbd_hid.o ./Src/st_usb/usbd/usbd_hid.su ./Src/st_usb/usbd/usbd_ioreq.cyclo ./Src/st_usb/usbd/usbd_ioreq.d ./Src/st_usb/usbd/usbd_ioreq.o ./Src/st_usb/usbd/usbd_ioreq.su

.PHONY: clean-Src-2f-st_usb-2f-usbd

