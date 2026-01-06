################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (13.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../App/app.c \
../App/hooks.c \
../App/led.c 

OBJS += \
./App/app.o \
./App/hooks.o \
./App/led.o 

C_DEPS += \
./App/app.d \
./App/hooks.d \
./App/led.d 


# Each subdirectory must supply rules for building sources it contributes
App/%.o App/%.su App/%.cyclo: ../App/%.c App/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m0plus -std=gnu11 -g3 -DDEBUG -DUSE_HAL_DRIVER -DSTM32G0B1xx -DUSE_FULL_LL_DRIVER -c -I../Core/Inc -I../Drivers/STM32G0xx_HAL_Driver/Inc -I../Drivers/STM32G0xx_HAL_Driver/Inc/Legacy -I../Drivers/CMSIS/Device/ST/STM32G0xx/Include -I../Drivers/CMSIS/Include -I../App -I"C:/Users/jorda/Documents/GitHub/openblt/Target/Source/ARMCM0_STM32G0/GCC" -I"C:/Users/jorda/Documents/GitHub/openblt/Target/Source" -I"C:/Users/jorda/Documents/GitHub/openblt/Target/Source/ARMCM0_STM32G0" -Os -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfloat-abi=soft -mthumb -o "$@"

clean: clean-App

clean-App:
	-$(RM) ./App/app.cyclo ./App/app.d ./App/app.o ./App/app.su ./App/hooks.cyclo ./App/hooks.d ./App/hooks.o ./App/hooks.su ./App/led.cyclo ./App/led.d ./App/led.o ./App/led.su

.PHONY: clean-App

