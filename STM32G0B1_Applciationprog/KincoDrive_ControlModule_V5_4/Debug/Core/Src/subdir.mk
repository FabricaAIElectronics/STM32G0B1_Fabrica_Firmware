################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (14.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../Core/Src/CAN_Handler.c \
../Core/Src/Fan_PWM.c \
../Core/Src/adc_driver.c \
../Core/Src/applogic.c \
../Core/Src/eeprom_driver.c \
../Core/Src/hs_switch.c \
../Core/Src/main.c \
../Core/Src/power_monitor.c \
../Core/Src/stm32g0xx_hal_msp.c \
../Core/Src/stm32g0xx_it.c \
../Core/Src/syscalls.c \
../Core/Src/sysmem.c \
../Core/Src/system_stm32g0xx.c \
../Core/Src/thermistor.c 

OBJS += \
./Core/Src/CAN_Handler.o \
./Core/Src/Fan_PWM.o \
./Core/Src/adc_driver.o \
./Core/Src/applogic.o \
./Core/Src/eeprom_driver.o \
./Core/Src/hs_switch.o \
./Core/Src/main.o \
./Core/Src/power_monitor.o \
./Core/Src/stm32g0xx_hal_msp.o \
./Core/Src/stm32g0xx_it.o \
./Core/Src/syscalls.o \
./Core/Src/sysmem.o \
./Core/Src/system_stm32g0xx.o \
./Core/Src/thermistor.o 

C_DEPS += \
./Core/Src/CAN_Handler.d \
./Core/Src/Fan_PWM.d \
./Core/Src/adc_driver.d \
./Core/Src/applogic.d \
./Core/Src/eeprom_driver.d \
./Core/Src/hs_switch.d \
./Core/Src/main.d \
./Core/Src/power_monitor.d \
./Core/Src/stm32g0xx_hal_msp.d \
./Core/Src/stm32g0xx_it.d \
./Core/Src/syscalls.d \
./Core/Src/sysmem.d \
./Core/Src/system_stm32g0xx.d \
./Core/Src/thermistor.d 


# Each subdirectory must supply rules for building sources it contributes
Core/Src/%.o Core/Src/%.su Core/Src/%.cyclo: ../Core/Src/%.c Core/Src/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m0plus -std=gnu11 -g3 -DDEBUG -DUSE_HAL_DRIVER -DSTM32G0B1xx -c -I../Core/Inc -I../Drivers/STM32G0xx_HAL_Driver/Inc -I../Drivers/STM32G0xx_HAL_Driver/Inc/Legacy -I../Drivers/CMSIS/Device/ST/STM32G0xx/Include -I../Drivers/CMSIS/Include -I../App -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfloat-abi=soft -mthumb -o "$@"

clean: clean-Core-2f-Src

clean-Core-2f-Src:
	-$(RM) ./Core/Src/CAN_Handler.cyclo ./Core/Src/CAN_Handler.d ./Core/Src/CAN_Handler.o ./Core/Src/CAN_Handler.su ./Core/Src/Fan_PWM.cyclo ./Core/Src/Fan_PWM.d ./Core/Src/Fan_PWM.o ./Core/Src/Fan_PWM.su ./Core/Src/adc_driver.cyclo ./Core/Src/adc_driver.d ./Core/Src/adc_driver.o ./Core/Src/adc_driver.su ./Core/Src/applogic.cyclo ./Core/Src/applogic.d ./Core/Src/applogic.o ./Core/Src/applogic.su ./Core/Src/eeprom_driver.cyclo ./Core/Src/eeprom_driver.d ./Core/Src/eeprom_driver.o ./Core/Src/eeprom_driver.su ./Core/Src/hs_switch.cyclo ./Core/Src/hs_switch.d ./Core/Src/hs_switch.o ./Core/Src/hs_switch.su ./Core/Src/main.cyclo ./Core/Src/main.d ./Core/Src/main.o ./Core/Src/main.su ./Core/Src/power_monitor.cyclo ./Core/Src/power_monitor.d ./Core/Src/power_monitor.o ./Core/Src/power_monitor.su ./Core/Src/stm32g0xx_hal_msp.cyclo ./Core/Src/stm32g0xx_hal_msp.d ./Core/Src/stm32g0xx_hal_msp.o ./Core/Src/stm32g0xx_hal_msp.su ./Core/Src/stm32g0xx_it.cyclo ./Core/Src/stm32g0xx_it.d ./Core/Src/stm32g0xx_it.o ./Core/Src/stm32g0xx_it.su ./Core/Src/syscalls.cyclo ./Core/Src/syscalls.d ./Core/Src/syscalls.o ./Core/Src/syscalls.su ./Core/Src/sysmem.cyclo ./Core/Src/sysmem.d ./Core/Src/sysmem.o ./Core/Src/sysmem.su ./Core/Src/system_stm32g0xx.cyclo ./Core/Src/system_stm32g0xx.d ./Core/Src/system_stm32g0xx.o ./Core/Src/system_stm32g0xx.su ./Core/Src/thermistor.cyclo ./Core/Src/thermistor.d ./Core/Src/thermistor.o ./Core/Src/thermistor.su

.PHONY: clean-Core-2f-Src

