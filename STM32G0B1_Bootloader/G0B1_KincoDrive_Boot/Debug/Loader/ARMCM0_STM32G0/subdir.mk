################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (14.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
C:/Users/jorda/Documents/GitHub/openblt/Target/Source/ARMCM0_STM32G0/can.c \
C:/Users/jorda/Documents/GitHub/openblt/Target/Source/ARMCM0_STM32G0/cpu.c \
C:/Users/jorda/Documents/GitHub/openblt/Target/Source/ARMCM0_STM32G0/flash.c \
C:/Users/jorda/Documents/GitHub/openblt/Target/Source/ARMCM0_STM32G0/mbrtu.c \
C:/Users/jorda/Documents/GitHub/openblt/Target/Source/ARMCM0_STM32G0/nvm.c \
C:/Users/jorda/Documents/GitHub/openblt/Target/Source/ARMCM0_STM32G0/rs232.c \
C:/Users/jorda/Documents/GitHub/openblt/Target/Source/ARMCM0_STM32G0/timer.c \
C:/Users/jorda/Documents/GitHub/openblt/Target/Source/ARMCM0_STM32G0/usb.c 

OBJS += \
./Loader/ARMCM0_STM32G0/can.o \
./Loader/ARMCM0_STM32G0/cpu.o \
./Loader/ARMCM0_STM32G0/flash.o \
./Loader/ARMCM0_STM32G0/mbrtu.o \
./Loader/ARMCM0_STM32G0/nvm.o \
./Loader/ARMCM0_STM32G0/rs232.o \
./Loader/ARMCM0_STM32G0/timer.o \
./Loader/ARMCM0_STM32G0/usb.o 

C_DEPS += \
./Loader/ARMCM0_STM32G0/can.d \
./Loader/ARMCM0_STM32G0/cpu.d \
./Loader/ARMCM0_STM32G0/flash.d \
./Loader/ARMCM0_STM32G0/mbrtu.d \
./Loader/ARMCM0_STM32G0/nvm.d \
./Loader/ARMCM0_STM32G0/rs232.d \
./Loader/ARMCM0_STM32G0/timer.d \
./Loader/ARMCM0_STM32G0/usb.d 


# Each subdirectory must supply rules for building sources it contributes
Loader/ARMCM0_STM32G0/can.o: C:/Users/jorda/Documents/GitHub/openblt/Target/Source/ARMCM0_STM32G0/can.c Loader/ARMCM0_STM32G0/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m0plus -std=gnu11 -g3 -DDEBUG -DUSE_HAL_DRIVER -DSTM32G0B1xx -DUSE_FULL_LL_DRIVER -c -I../Core/Inc -I../Drivers/STM32G0xx_HAL_Driver/Inc -I../Drivers/STM32G0xx_HAL_Driver/Inc/Legacy -I../Drivers/CMSIS/Device/ST/STM32G0xx/Include -I../Drivers/CMSIS/Include -I../App -I"C:/Users/jorda/Documents/GitHub/openblt/Target/Source/ARMCM0_STM32G0/GCC" -I"C:/Users/jorda/Documents/GitHub/openblt/Target/Source" -I"C:/Users/jorda/Documents/GitHub/openblt/Target/Source/ARMCM0_STM32G0" -Os -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfloat-abi=soft -mthumb -o "$@"
Loader/ARMCM0_STM32G0/cpu.o: C:/Users/jorda/Documents/GitHub/openblt/Target/Source/ARMCM0_STM32G0/cpu.c Loader/ARMCM0_STM32G0/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m0plus -std=gnu11 -g3 -DDEBUG -DUSE_HAL_DRIVER -DSTM32G0B1xx -DUSE_FULL_LL_DRIVER -c -I../Core/Inc -I../Drivers/STM32G0xx_HAL_Driver/Inc -I../Drivers/STM32G0xx_HAL_Driver/Inc/Legacy -I../Drivers/CMSIS/Device/ST/STM32G0xx/Include -I../Drivers/CMSIS/Include -I../App -I"C:/Users/jorda/Documents/GitHub/openblt/Target/Source/ARMCM0_STM32G0/GCC" -I"C:/Users/jorda/Documents/GitHub/openblt/Target/Source" -I"C:/Users/jorda/Documents/GitHub/openblt/Target/Source/ARMCM0_STM32G0" -Os -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfloat-abi=soft -mthumb -o "$@"
Loader/ARMCM0_STM32G0/flash.o: C:/Users/jorda/Documents/GitHub/openblt/Target/Source/ARMCM0_STM32G0/flash.c Loader/ARMCM0_STM32G0/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m0plus -std=gnu11 -g3 -DDEBUG -DUSE_HAL_DRIVER -DSTM32G0B1xx -DUSE_FULL_LL_DRIVER -c -I../Core/Inc -I../Drivers/STM32G0xx_HAL_Driver/Inc -I../Drivers/STM32G0xx_HAL_Driver/Inc/Legacy -I../Drivers/CMSIS/Device/ST/STM32G0xx/Include -I../Drivers/CMSIS/Include -I../App -I"C:/Users/jorda/Documents/GitHub/openblt/Target/Source/ARMCM0_STM32G0/GCC" -I"C:/Users/jorda/Documents/GitHub/openblt/Target/Source" -I"C:/Users/jorda/Documents/GitHub/openblt/Target/Source/ARMCM0_STM32G0" -Os -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfloat-abi=soft -mthumb -o "$@"
Loader/ARMCM0_STM32G0/mbrtu.o: C:/Users/jorda/Documents/GitHub/openblt/Target/Source/ARMCM0_STM32G0/mbrtu.c Loader/ARMCM0_STM32G0/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m0plus -std=gnu11 -g3 -DDEBUG -DUSE_HAL_DRIVER -DSTM32G0B1xx -DUSE_FULL_LL_DRIVER -c -I../Core/Inc -I../Drivers/STM32G0xx_HAL_Driver/Inc -I../Drivers/STM32G0xx_HAL_Driver/Inc/Legacy -I../Drivers/CMSIS/Device/ST/STM32G0xx/Include -I../Drivers/CMSIS/Include -I../App -I"C:/Users/jorda/Documents/GitHub/openblt/Target/Source/ARMCM0_STM32G0/GCC" -I"C:/Users/jorda/Documents/GitHub/openblt/Target/Source" -I"C:/Users/jorda/Documents/GitHub/openblt/Target/Source/ARMCM0_STM32G0" -Os -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfloat-abi=soft -mthumb -o "$@"
Loader/ARMCM0_STM32G0/nvm.o: C:/Users/jorda/Documents/GitHub/openblt/Target/Source/ARMCM0_STM32G0/nvm.c Loader/ARMCM0_STM32G0/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m0plus -std=gnu11 -g3 -DDEBUG -DUSE_HAL_DRIVER -DSTM32G0B1xx -DUSE_FULL_LL_DRIVER -c -I../Core/Inc -I../Drivers/STM32G0xx_HAL_Driver/Inc -I../Drivers/STM32G0xx_HAL_Driver/Inc/Legacy -I../Drivers/CMSIS/Device/ST/STM32G0xx/Include -I../Drivers/CMSIS/Include -I../App -I"C:/Users/jorda/Documents/GitHub/openblt/Target/Source/ARMCM0_STM32G0/GCC" -I"C:/Users/jorda/Documents/GitHub/openblt/Target/Source" -I"C:/Users/jorda/Documents/GitHub/openblt/Target/Source/ARMCM0_STM32G0" -Os -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfloat-abi=soft -mthumb -o "$@"
Loader/ARMCM0_STM32G0/rs232.o: C:/Users/jorda/Documents/GitHub/openblt/Target/Source/ARMCM0_STM32G0/rs232.c Loader/ARMCM0_STM32G0/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m0plus -std=gnu11 -g3 -DDEBUG -DUSE_HAL_DRIVER -DSTM32G0B1xx -DUSE_FULL_LL_DRIVER -c -I../Core/Inc -I../Drivers/STM32G0xx_HAL_Driver/Inc -I../Drivers/STM32G0xx_HAL_Driver/Inc/Legacy -I../Drivers/CMSIS/Device/ST/STM32G0xx/Include -I../Drivers/CMSIS/Include -I../App -I"C:/Users/jorda/Documents/GitHub/openblt/Target/Source/ARMCM0_STM32G0/GCC" -I"C:/Users/jorda/Documents/GitHub/openblt/Target/Source" -I"C:/Users/jorda/Documents/GitHub/openblt/Target/Source/ARMCM0_STM32G0" -Os -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfloat-abi=soft -mthumb -o "$@"
Loader/ARMCM0_STM32G0/timer.o: C:/Users/jorda/Documents/GitHub/openblt/Target/Source/ARMCM0_STM32G0/timer.c Loader/ARMCM0_STM32G0/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m0plus -std=gnu11 -g3 -DDEBUG -DUSE_HAL_DRIVER -DSTM32G0B1xx -DUSE_FULL_LL_DRIVER -c -I../Core/Inc -I../Drivers/STM32G0xx_HAL_Driver/Inc -I../Drivers/STM32G0xx_HAL_Driver/Inc/Legacy -I../Drivers/CMSIS/Device/ST/STM32G0xx/Include -I../Drivers/CMSIS/Include -I../App -I"C:/Users/jorda/Documents/GitHub/openblt/Target/Source/ARMCM0_STM32G0/GCC" -I"C:/Users/jorda/Documents/GitHub/openblt/Target/Source" -I"C:/Users/jorda/Documents/GitHub/openblt/Target/Source/ARMCM0_STM32G0" -Os -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfloat-abi=soft -mthumb -o "$@"
Loader/ARMCM0_STM32G0/usb.o: C:/Users/jorda/Documents/GitHub/openblt/Target/Source/ARMCM0_STM32G0/usb.c Loader/ARMCM0_STM32G0/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m0plus -std=gnu11 -g3 -DDEBUG -DUSE_HAL_DRIVER -DSTM32G0B1xx -DUSE_FULL_LL_DRIVER -c -I../Core/Inc -I../Drivers/STM32G0xx_HAL_Driver/Inc -I../Drivers/STM32G0xx_HAL_Driver/Inc/Legacy -I../Drivers/CMSIS/Device/ST/STM32G0xx/Include -I../Drivers/CMSIS/Include -I../App -I"C:/Users/jorda/Documents/GitHub/openblt/Target/Source/ARMCM0_STM32G0/GCC" -I"C:/Users/jorda/Documents/GitHub/openblt/Target/Source" -I"C:/Users/jorda/Documents/GitHub/openblt/Target/Source/ARMCM0_STM32G0" -Os -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfloat-abi=soft -mthumb -o "$@"

clean: clean-Loader-2f-ARMCM0_STM32G0

clean-Loader-2f-ARMCM0_STM32G0:
	-$(RM) ./Loader/ARMCM0_STM32G0/can.cyclo ./Loader/ARMCM0_STM32G0/can.d ./Loader/ARMCM0_STM32G0/can.o ./Loader/ARMCM0_STM32G0/can.su ./Loader/ARMCM0_STM32G0/cpu.cyclo ./Loader/ARMCM0_STM32G0/cpu.d ./Loader/ARMCM0_STM32G0/cpu.o ./Loader/ARMCM0_STM32G0/cpu.su ./Loader/ARMCM0_STM32G0/flash.cyclo ./Loader/ARMCM0_STM32G0/flash.d ./Loader/ARMCM0_STM32G0/flash.o ./Loader/ARMCM0_STM32G0/flash.su ./Loader/ARMCM0_STM32G0/mbrtu.cyclo ./Loader/ARMCM0_STM32G0/mbrtu.d ./Loader/ARMCM0_STM32G0/mbrtu.o ./Loader/ARMCM0_STM32G0/mbrtu.su ./Loader/ARMCM0_STM32G0/nvm.cyclo ./Loader/ARMCM0_STM32G0/nvm.d ./Loader/ARMCM0_STM32G0/nvm.o ./Loader/ARMCM0_STM32G0/nvm.su ./Loader/ARMCM0_STM32G0/rs232.cyclo ./Loader/ARMCM0_STM32G0/rs232.d ./Loader/ARMCM0_STM32G0/rs232.o ./Loader/ARMCM0_STM32G0/rs232.su ./Loader/ARMCM0_STM32G0/timer.cyclo ./Loader/ARMCM0_STM32G0/timer.d ./Loader/ARMCM0_STM32G0/timer.o ./Loader/ARMCM0_STM32G0/timer.su ./Loader/ARMCM0_STM32G0/usb.cyclo ./Loader/ARMCM0_STM32G0/usb.d ./Loader/ARMCM0_STM32G0/usb.o ./Loader/ARMCM0_STM32G0/usb.su

.PHONY: clean-Loader-2f-ARMCM0_STM32G0

