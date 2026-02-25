################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (14.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../Core/Src/applogic.c \
../Core/Src/can_operation.c \
../Core/Src/canopen_bridge.c \
../Core/Src/eeprom_driver.c \
../Core/Src/main.c \
../Core/Src/peripheral.c \
../Core/Src/stm32g0xx_hal_msp.c \
../Core/Src/stm32g0xx_it.c \
../Core/Src/syscalls.c \
../Core/Src/sysmem.c \
../Core/Src/system_stm32g0xx.c 

OBJS += \
./Core/Src/applogic.o \
./Core/Src/can_operation.o \
./Core/Src/canopen_bridge.o \
./Core/Src/eeprom_driver.o \
./Core/Src/main.o \
./Core/Src/peripheral.o \
./Core/Src/stm32g0xx_hal_msp.o \
./Core/Src/stm32g0xx_it.o \
./Core/Src/syscalls.o \
./Core/Src/sysmem.o \
./Core/Src/system_stm32g0xx.o 

C_DEPS += \
./Core/Src/applogic.d \
./Core/Src/can_operation.d \
./Core/Src/canopen_bridge.d \
./Core/Src/eeprom_driver.d \
./Core/Src/main.d \
./Core/Src/peripheral.d \
./Core/Src/stm32g0xx_hal_msp.d \
./Core/Src/stm32g0xx_it.d \
./Core/Src/syscalls.d \
./Core/Src/sysmem.d \
./Core/Src/system_stm32g0xx.d 


# Each subdirectory must supply rules for building sources it contributes
Core/Src/%.o Core/Src/%.su Core/Src/%.cyclo: ../Core/Src/%.c Core/Src/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m0plus -std=gnu11 -g3 -DDEBUG -DUSE_HAL_DRIVER -DSTM32G0B1xx -c -I../Core/Inc -I../Drivers/STM32G0xx_HAL_Driver/Inc -I../Drivers/STM32G0xx_HAL_Driver/Inc/Legacy -I../Drivers/CMSIS/Device/ST/STM32G0xx/Include -I../Drivers/CMSIS/Include -I"C:/Users/jorda/Documents/GitHub/STM32G0B1_Fabrica_Firmware/STM32G0B1_Applciationprog/STM32G0_LEDDRIVER_PROG/App" -I../canopen/Inc -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfloat-abi=soft -mthumb -o "$@"

clean: clean-Core-2f-Src

clean-Core-2f-Src:
	-$(RM) ./Core/Src/applogic.cyclo ./Core/Src/applogic.d ./Core/Src/applogic.o ./Core/Src/applogic.su ./Core/Src/can_operation.cyclo ./Core/Src/can_operation.d ./Core/Src/can_operation.o ./Core/Src/can_operation.su ./Core/Src/canopen_bridge.cyclo ./Core/Src/canopen_bridge.d ./Core/Src/canopen_bridge.o ./Core/Src/canopen_bridge.su ./Core/Src/eeprom_driver.cyclo ./Core/Src/eeprom_driver.d ./Core/Src/eeprom_driver.o ./Core/Src/eeprom_driver.su ./Core/Src/main.cyclo ./Core/Src/main.d ./Core/Src/main.o ./Core/Src/main.su ./Core/Src/peripheral.cyclo ./Core/Src/peripheral.d ./Core/Src/peripheral.o ./Core/Src/peripheral.su ./Core/Src/stm32g0xx_hal_msp.cyclo ./Core/Src/stm32g0xx_hal_msp.d ./Core/Src/stm32g0xx_hal_msp.o ./Core/Src/stm32g0xx_hal_msp.su ./Core/Src/stm32g0xx_it.cyclo ./Core/Src/stm32g0xx_it.d ./Core/Src/stm32g0xx_it.o ./Core/Src/stm32g0xx_it.su ./Core/Src/syscalls.cyclo ./Core/Src/syscalls.d ./Core/Src/syscalls.o ./Core/Src/syscalls.su ./Core/Src/sysmem.cyclo ./Core/Src/sysmem.d ./Core/Src/sysmem.o ./Core/Src/sysmem.su ./Core/Src/system_stm32g0xx.cyclo ./Core/Src/system_stm32g0xx.d ./Core/Src/system_stm32g0xx.o ./Core/Src/system_stm32g0xx.su

.PHONY: clean-Core-2f-Src

