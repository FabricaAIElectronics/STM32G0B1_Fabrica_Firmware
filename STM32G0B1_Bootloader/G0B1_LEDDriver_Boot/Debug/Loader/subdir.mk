################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (13.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
C:/Users/jorda/Documents/GitHub/openblt/Target/Source/asserts.c \
C:/Users/jorda/Documents/GitHub/openblt/Target/Source/backdoor.c \
C:/Users/jorda/Documents/GitHub/openblt/Target/Source/boot.c \
C:/Users/jorda/Documents/GitHub/openblt/Target/Source/com.c \
C:/Users/jorda/Documents/GitHub/openblt/Target/Source/cop.c \
C:/Users/jorda/Documents/GitHub/openblt/Target/Source/file.c \
C:/Users/jorda/Documents/GitHub/openblt/Target/Source/infotable.c \
C:/Users/jorda/Documents/GitHub/openblt/Target/Source/mb.c \
C:/Users/jorda/Documents/GitHub/openblt/Target/Source/net.c \
C:/Users/jorda/Documents/GitHub/openblt/Target/Source/xcp.c 

OBJS += \
./Loader/asserts.o \
./Loader/backdoor.o \
./Loader/boot.o \
./Loader/com.o \
./Loader/cop.o \
./Loader/file.o \
./Loader/infotable.o \
./Loader/mb.o \
./Loader/net.o \
./Loader/xcp.o 

C_DEPS += \
./Loader/asserts.d \
./Loader/backdoor.d \
./Loader/boot.d \
./Loader/com.d \
./Loader/cop.d \
./Loader/file.d \
./Loader/infotable.d \
./Loader/mb.d \
./Loader/net.d \
./Loader/xcp.d 


# Each subdirectory must supply rules for building sources it contributes
Loader/asserts.o: C:/Users/jorda/Documents/GitHub/openblt/Target/Source/asserts.c Loader/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m0plus -std=gnu11 -g3 -DDEBUG -DUSE_HAL_DRIVER -DSTM32G0B1xx -DUSE_FULL_LL_DRIVER -c -I../Core/Inc -I../Drivers/STM32G0xx_HAL_Driver/Inc -I../Drivers/STM32G0xx_HAL_Driver/Inc/Legacy -I../Drivers/CMSIS/Device/ST/STM32G0xx/Include -I../Drivers/CMSIS/Include -I../App -I"C:/Users/jorda/Documents/GitHub/openblt/Target/Source/ARMCM0_STM32G0/GCC" -I"C:/Users/jorda/Documents/GitHub/openblt/Target/Source" -I"C:/Users/jorda/Documents/GitHub/openblt/Target/Source/ARMCM0_STM32G0" -Os -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfloat-abi=soft -mthumb -o "$@"
Loader/backdoor.o: C:/Users/jorda/Documents/GitHub/openblt/Target/Source/backdoor.c Loader/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m0plus -std=gnu11 -g3 -DDEBUG -DUSE_HAL_DRIVER -DSTM32G0B1xx -DUSE_FULL_LL_DRIVER -c -I../Core/Inc -I../Drivers/STM32G0xx_HAL_Driver/Inc -I../Drivers/STM32G0xx_HAL_Driver/Inc/Legacy -I../Drivers/CMSIS/Device/ST/STM32G0xx/Include -I../Drivers/CMSIS/Include -I../App -I"C:/Users/jorda/Documents/GitHub/openblt/Target/Source/ARMCM0_STM32G0/GCC" -I"C:/Users/jorda/Documents/GitHub/openblt/Target/Source" -I"C:/Users/jorda/Documents/GitHub/openblt/Target/Source/ARMCM0_STM32G0" -Os -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfloat-abi=soft -mthumb -o "$@"
Loader/boot.o: C:/Users/jorda/Documents/GitHub/openblt/Target/Source/boot.c Loader/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m0plus -std=gnu11 -g3 -DDEBUG -DUSE_HAL_DRIVER -DSTM32G0B1xx -DUSE_FULL_LL_DRIVER -c -I../Core/Inc -I../Drivers/STM32G0xx_HAL_Driver/Inc -I../Drivers/STM32G0xx_HAL_Driver/Inc/Legacy -I../Drivers/CMSIS/Device/ST/STM32G0xx/Include -I../Drivers/CMSIS/Include -I../App -I"C:/Users/jorda/Documents/GitHub/openblt/Target/Source/ARMCM0_STM32G0/GCC" -I"C:/Users/jorda/Documents/GitHub/openblt/Target/Source" -I"C:/Users/jorda/Documents/GitHub/openblt/Target/Source/ARMCM0_STM32G0" -Os -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfloat-abi=soft -mthumb -o "$@"
Loader/com.o: C:/Users/jorda/Documents/GitHub/openblt/Target/Source/com.c Loader/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m0plus -std=gnu11 -g3 -DDEBUG -DUSE_HAL_DRIVER -DSTM32G0B1xx -DUSE_FULL_LL_DRIVER -c -I../Core/Inc -I../Drivers/STM32G0xx_HAL_Driver/Inc -I../Drivers/STM32G0xx_HAL_Driver/Inc/Legacy -I../Drivers/CMSIS/Device/ST/STM32G0xx/Include -I../Drivers/CMSIS/Include -I../App -I"C:/Users/jorda/Documents/GitHub/openblt/Target/Source/ARMCM0_STM32G0/GCC" -I"C:/Users/jorda/Documents/GitHub/openblt/Target/Source" -I"C:/Users/jorda/Documents/GitHub/openblt/Target/Source/ARMCM0_STM32G0" -Os -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfloat-abi=soft -mthumb -o "$@"
Loader/cop.o: C:/Users/jorda/Documents/GitHub/openblt/Target/Source/cop.c Loader/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m0plus -std=gnu11 -g3 -DDEBUG -DUSE_HAL_DRIVER -DSTM32G0B1xx -DUSE_FULL_LL_DRIVER -c -I../Core/Inc -I../Drivers/STM32G0xx_HAL_Driver/Inc -I../Drivers/STM32G0xx_HAL_Driver/Inc/Legacy -I../Drivers/CMSIS/Device/ST/STM32G0xx/Include -I../Drivers/CMSIS/Include -I../App -I"C:/Users/jorda/Documents/GitHub/openblt/Target/Source/ARMCM0_STM32G0/GCC" -I"C:/Users/jorda/Documents/GitHub/openblt/Target/Source" -I"C:/Users/jorda/Documents/GitHub/openblt/Target/Source/ARMCM0_STM32G0" -Os -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfloat-abi=soft -mthumb -o "$@"
Loader/file.o: C:/Users/jorda/Documents/GitHub/openblt/Target/Source/file.c Loader/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m0plus -std=gnu11 -g3 -DDEBUG -DUSE_HAL_DRIVER -DSTM32G0B1xx -DUSE_FULL_LL_DRIVER -c -I../Core/Inc -I../Drivers/STM32G0xx_HAL_Driver/Inc -I../Drivers/STM32G0xx_HAL_Driver/Inc/Legacy -I../Drivers/CMSIS/Device/ST/STM32G0xx/Include -I../Drivers/CMSIS/Include -I../App -I"C:/Users/jorda/Documents/GitHub/openblt/Target/Source/ARMCM0_STM32G0/GCC" -I"C:/Users/jorda/Documents/GitHub/openblt/Target/Source" -I"C:/Users/jorda/Documents/GitHub/openblt/Target/Source/ARMCM0_STM32G0" -Os -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfloat-abi=soft -mthumb -o "$@"
Loader/infotable.o: C:/Users/jorda/Documents/GitHub/openblt/Target/Source/infotable.c Loader/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m0plus -std=gnu11 -g3 -DDEBUG -DUSE_HAL_DRIVER -DSTM32G0B1xx -DUSE_FULL_LL_DRIVER -c -I../Core/Inc -I../Drivers/STM32G0xx_HAL_Driver/Inc -I../Drivers/STM32G0xx_HAL_Driver/Inc/Legacy -I../Drivers/CMSIS/Device/ST/STM32G0xx/Include -I../Drivers/CMSIS/Include -I../App -I"C:/Users/jorda/Documents/GitHub/openblt/Target/Source/ARMCM0_STM32G0/GCC" -I"C:/Users/jorda/Documents/GitHub/openblt/Target/Source" -I"C:/Users/jorda/Documents/GitHub/openblt/Target/Source/ARMCM0_STM32G0" -Os -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfloat-abi=soft -mthumb -o "$@"
Loader/mb.o: C:/Users/jorda/Documents/GitHub/openblt/Target/Source/mb.c Loader/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m0plus -std=gnu11 -g3 -DDEBUG -DUSE_HAL_DRIVER -DSTM32G0B1xx -DUSE_FULL_LL_DRIVER -c -I../Core/Inc -I../Drivers/STM32G0xx_HAL_Driver/Inc -I../Drivers/STM32G0xx_HAL_Driver/Inc/Legacy -I../Drivers/CMSIS/Device/ST/STM32G0xx/Include -I../Drivers/CMSIS/Include -I../App -I"C:/Users/jorda/Documents/GitHub/openblt/Target/Source/ARMCM0_STM32G0/GCC" -I"C:/Users/jorda/Documents/GitHub/openblt/Target/Source" -I"C:/Users/jorda/Documents/GitHub/openblt/Target/Source/ARMCM0_STM32G0" -Os -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfloat-abi=soft -mthumb -o "$@"
Loader/net.o: C:/Users/jorda/Documents/GitHub/openblt/Target/Source/net.c Loader/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m0plus -std=gnu11 -g3 -DDEBUG -DUSE_HAL_DRIVER -DSTM32G0B1xx -DUSE_FULL_LL_DRIVER -c -I../Core/Inc -I../Drivers/STM32G0xx_HAL_Driver/Inc -I../Drivers/STM32G0xx_HAL_Driver/Inc/Legacy -I../Drivers/CMSIS/Device/ST/STM32G0xx/Include -I../Drivers/CMSIS/Include -I../App -I"C:/Users/jorda/Documents/GitHub/openblt/Target/Source/ARMCM0_STM32G0/GCC" -I"C:/Users/jorda/Documents/GitHub/openblt/Target/Source" -I"C:/Users/jorda/Documents/GitHub/openblt/Target/Source/ARMCM0_STM32G0" -Os -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfloat-abi=soft -mthumb -o "$@"
Loader/xcp.o: C:/Users/jorda/Documents/GitHub/openblt/Target/Source/xcp.c Loader/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m0plus -std=gnu11 -g3 -DDEBUG -DUSE_HAL_DRIVER -DSTM32G0B1xx -DUSE_FULL_LL_DRIVER -c -I../Core/Inc -I../Drivers/STM32G0xx_HAL_Driver/Inc -I../Drivers/STM32G0xx_HAL_Driver/Inc/Legacy -I../Drivers/CMSIS/Device/ST/STM32G0xx/Include -I../Drivers/CMSIS/Include -I../App -I"C:/Users/jorda/Documents/GitHub/openblt/Target/Source/ARMCM0_STM32G0/GCC" -I"C:/Users/jorda/Documents/GitHub/openblt/Target/Source" -I"C:/Users/jorda/Documents/GitHub/openblt/Target/Source/ARMCM0_STM32G0" -Os -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfloat-abi=soft -mthumb -o "$@"

clean: clean-Loader

clean-Loader:
	-$(RM) ./Loader/asserts.cyclo ./Loader/asserts.d ./Loader/asserts.o ./Loader/asserts.su ./Loader/backdoor.cyclo ./Loader/backdoor.d ./Loader/backdoor.o ./Loader/backdoor.su ./Loader/boot.cyclo ./Loader/boot.d ./Loader/boot.o ./Loader/boot.su ./Loader/com.cyclo ./Loader/com.d ./Loader/com.o ./Loader/com.su ./Loader/cop.cyclo ./Loader/cop.d ./Loader/cop.o ./Loader/cop.su ./Loader/file.cyclo ./Loader/file.d ./Loader/file.o ./Loader/file.su ./Loader/infotable.cyclo ./Loader/infotable.d ./Loader/infotable.o ./Loader/infotable.su ./Loader/mb.cyclo ./Loader/mb.d ./Loader/mb.o ./Loader/mb.su ./Loader/net.cyclo ./Loader/net.d ./Loader/net.o ./Loader/net.su ./Loader/xcp.cyclo ./Loader/xcp.d ./Loader/xcp.o ./Loader/xcp.su

.PHONY: clean-Loader

