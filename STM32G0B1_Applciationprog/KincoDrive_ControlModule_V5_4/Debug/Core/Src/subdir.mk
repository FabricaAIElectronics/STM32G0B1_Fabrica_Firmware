################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (13.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../Core/Src/CAN_Handler.c \
../Core/Src/Core_System.c \
../Core/Src/ESTOP.c \
../Core/Src/Endstop.c \
../Core/Src/Fan_PWM.c \
../Core/Src/Power_Electronic.c \
../Core/Src/adc.c \
../Core/Src/dma.c \
../Core/Src/eeprom_driver.c \
../Core/Src/error_manager.c \
../Core/Src/fdcan.c \
../Core/Src/gpio.c \
../Core/Src/i2c.c \
../Core/Src/main.c \
../Core/Src/stm32g0xx_hal_msp.c \
../Core/Src/stm32g0xx_it.c \
../Core/Src/syscalls.c \
../Core/Src/sysmem.c \
../Core/Src/system_stm32g0xx.c \
../Core/Src/tim.c 

OBJS += \
./Core/Src/CAN_Handler.o \
./Core/Src/Core_System.o \
./Core/Src/ESTOP.o \
./Core/Src/Endstop.o \
./Core/Src/Fan_PWM.o \
./Core/Src/Power_Electronic.o \
./Core/Src/adc.o \
./Core/Src/dma.o \
./Core/Src/eeprom_driver.o \
./Core/Src/error_manager.o \
./Core/Src/fdcan.o \
./Core/Src/gpio.o \
./Core/Src/i2c.o \
./Core/Src/main.o \
./Core/Src/stm32g0xx_hal_msp.o \
./Core/Src/stm32g0xx_it.o \
./Core/Src/syscalls.o \
./Core/Src/sysmem.o \
./Core/Src/system_stm32g0xx.o \
./Core/Src/tim.o 

C_DEPS += \
./Core/Src/CAN_Handler.d \
./Core/Src/Core_System.d \
./Core/Src/ESTOP.d \
./Core/Src/Endstop.d \
./Core/Src/Fan_PWM.d \
./Core/Src/Power_Electronic.d \
./Core/Src/adc.d \
./Core/Src/dma.d \
./Core/Src/eeprom_driver.d \
./Core/Src/error_manager.d \
./Core/Src/fdcan.d \
./Core/Src/gpio.d \
./Core/Src/i2c.d \
./Core/Src/main.d \
./Core/Src/stm32g0xx_hal_msp.d \
./Core/Src/stm32g0xx_it.d \
./Core/Src/syscalls.d \
./Core/Src/sysmem.d \
./Core/Src/system_stm32g0xx.d \
./Core/Src/tim.d 


# Each subdirectory must supply rules for building sources it contributes
Core/Src/%.o Core/Src/%.su Core/Src/%.cyclo: ../Core/Src/%.c Core/Src/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m0plus -std=gnu11 -g3 -DDEBUG -DUSE_HAL_DRIVER -DSTM32G0B1xx -c -I../Core/Inc -I../Drivers/STM32G0xx_HAL_Driver/Inc -I../Drivers/STM32G0xx_HAL_Driver/Inc/Legacy -I../Drivers/CMSIS/Device/ST/STM32G0xx/Include -I../Drivers/CMSIS/Include -I../App -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfloat-abi=soft -mthumb -o "$@"

clean: clean-Core-2f-Src

clean-Core-2f-Src:
	-$(RM) ./Core/Src/CAN_Handler.cyclo ./Core/Src/CAN_Handler.d ./Core/Src/CAN_Handler.o ./Core/Src/CAN_Handler.su ./Core/Src/Core_System.cyclo ./Core/Src/Core_System.d ./Core/Src/Core_System.o ./Core/Src/Core_System.su ./Core/Src/ESTOP.cyclo ./Core/Src/ESTOP.d ./Core/Src/ESTOP.o ./Core/Src/ESTOP.su ./Core/Src/Endstop.cyclo ./Core/Src/Endstop.d ./Core/Src/Endstop.o ./Core/Src/Endstop.su ./Core/Src/Fan_PWM.cyclo ./Core/Src/Fan_PWM.d ./Core/Src/Fan_PWM.o ./Core/Src/Fan_PWM.su ./Core/Src/Power_Electronic.cyclo ./Core/Src/Power_Electronic.d ./Core/Src/Power_Electronic.o ./Core/Src/Power_Electronic.su ./Core/Src/adc.cyclo ./Core/Src/adc.d ./Core/Src/adc.o ./Core/Src/adc.su ./Core/Src/dma.cyclo ./Core/Src/dma.d ./Core/Src/dma.o ./Core/Src/dma.su ./Core/Src/eeprom_driver.cyclo ./Core/Src/eeprom_driver.d ./Core/Src/eeprom_driver.o ./Core/Src/eeprom_driver.su ./Core/Src/error_manager.cyclo ./Core/Src/error_manager.d ./Core/Src/error_manager.o ./Core/Src/error_manager.su ./Core/Src/fdcan.cyclo ./Core/Src/fdcan.d ./Core/Src/fdcan.o ./Core/Src/fdcan.su ./Core/Src/gpio.cyclo ./Core/Src/gpio.d ./Core/Src/gpio.o ./Core/Src/gpio.su ./Core/Src/i2c.cyclo ./Core/Src/i2c.d ./Core/Src/i2c.o ./Core/Src/i2c.su ./Core/Src/main.cyclo ./Core/Src/main.d ./Core/Src/main.o ./Core/Src/main.su ./Core/Src/stm32g0xx_hal_msp.cyclo ./Core/Src/stm32g0xx_hal_msp.d ./Core/Src/stm32g0xx_hal_msp.o ./Core/Src/stm32g0xx_hal_msp.su ./Core/Src/stm32g0xx_it.cyclo ./Core/Src/stm32g0xx_it.d ./Core/Src/stm32g0xx_it.o ./Core/Src/stm32g0xx_it.su ./Core/Src/syscalls.cyclo ./Core/Src/syscalls.d ./Core/Src/syscalls.o ./Core/Src/syscalls.su ./Core/Src/sysmem.cyclo ./Core/Src/sysmem.d ./Core/Src/sysmem.o ./Core/Src/sysmem.su ./Core/Src/system_stm32g0xx.cyclo ./Core/Src/system_stm32g0xx.d ./Core/Src/system_stm32g0xx.o ./Core/Src/system_stm32g0xx.su ./Core/Src/tim.cyclo ./Core/Src/tim.d ./Core/Src/tim.o ./Core/Src/tim.su

.PHONY: clean-Core-2f-Src

