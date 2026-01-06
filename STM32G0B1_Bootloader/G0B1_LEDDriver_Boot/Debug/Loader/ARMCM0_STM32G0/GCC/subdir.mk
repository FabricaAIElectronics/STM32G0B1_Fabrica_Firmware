################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (13.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
C:/Users/jorda/Documents/GitHub/openblt/Target/Source/ARMCM0_STM32G0/GCC/cpu_comp.c 

OBJS += \
./Loader/ARMCM0_STM32G0/GCC/cpu_comp.o 

C_DEPS += \
./Loader/ARMCM0_STM32G0/GCC/cpu_comp.d 


# Each subdirectory must supply rules for building sources it contributes
Loader/ARMCM0_STM32G0/GCC/cpu_comp.o: C:/Users/jorda/Documents/GitHub/openblt/Target/Source/ARMCM0_STM32G0/GCC/cpu_comp.c Loader/ARMCM0_STM32G0/GCC/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m0plus -std=gnu11 -g3 -DDEBUG -DUSE_HAL_DRIVER -DSTM32G0B1xx -DUSE_FULL_LL_DRIVER -c -I../Core/Inc -I../Drivers/STM32G0xx_HAL_Driver/Inc -I../Drivers/STM32G0xx_HAL_Driver/Inc/Legacy -I../Drivers/CMSIS/Device/ST/STM32G0xx/Include -I../Drivers/CMSIS/Include -I../App -I"C:/Users/jorda/Documents/GitHub/openblt/Target/Source/ARMCM0_STM32G0/GCC" -I"C:/Users/jorda/Documents/GitHub/openblt/Target/Source" -I"C:/Users/jorda/Documents/GitHub/openblt/Target/Source/ARMCM0_STM32G0" -Os -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfloat-abi=soft -mthumb -o "$@"

clean: clean-Loader-2f-ARMCM0_STM32G0-2f-GCC

clean-Loader-2f-ARMCM0_STM32G0-2f-GCC:
	-$(RM) ./Loader/ARMCM0_STM32G0/GCC/cpu_comp.cyclo ./Loader/ARMCM0_STM32G0/GCC/cpu_comp.d ./Loader/ARMCM0_STM32G0/GCC/cpu_comp.o ./Loader/ARMCM0_STM32G0/GCC/cpu_comp.su

.PHONY: clean-Loader-2f-ARMCM0_STM32G0-2f-GCC

