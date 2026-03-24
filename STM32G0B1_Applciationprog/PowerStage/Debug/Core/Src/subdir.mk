################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (14.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../Core/Src/RunningHorseDisplay.c \
../Core/Src/can_operation.c \
../Core/Src/display_scheduler.c \
../Core/Src/eeprom_driver.c \
../Core/Src/fan_ctrl.c \
../Core/Src/fonts.c \
../Core/Src/io_module.c \
../Core/Src/main.c \
../Core/Src/powerstage_app.c \
../Core/Src/ssd1306.c \
../Core/Src/stm32g0xx_hal_msp.c \
../Core/Src/stm32g0xx_it.c \
../Core/Src/syscalls.c \
../Core/Src/sysmem.c \
../Core/Src/system_stm32g0xx.c \
../Core/Src/ui_display.c 

OBJS += \
./Core/Src/RunningHorseDisplay.o \
./Core/Src/can_operation.o \
./Core/Src/display_scheduler.o \
./Core/Src/eeprom_driver.o \
./Core/Src/fan_ctrl.o \
./Core/Src/fonts.o \
./Core/Src/io_module.o \
./Core/Src/main.o \
./Core/Src/powerstage_app.o \
./Core/Src/ssd1306.o \
./Core/Src/stm32g0xx_hal_msp.o \
./Core/Src/stm32g0xx_it.o \
./Core/Src/syscalls.o \
./Core/Src/sysmem.o \
./Core/Src/system_stm32g0xx.o \
./Core/Src/ui_display.o 

C_DEPS += \
./Core/Src/RunningHorseDisplay.d \
./Core/Src/can_operation.d \
./Core/Src/display_scheduler.d \
./Core/Src/eeprom_driver.d \
./Core/Src/fan_ctrl.d \
./Core/Src/fonts.d \
./Core/Src/io_module.d \
./Core/Src/main.d \
./Core/Src/powerstage_app.d \
./Core/Src/ssd1306.d \
./Core/Src/stm32g0xx_hal_msp.d \
./Core/Src/stm32g0xx_it.d \
./Core/Src/syscalls.d \
./Core/Src/sysmem.d \
./Core/Src/system_stm32g0xx.d \
./Core/Src/ui_display.d 


# Each subdirectory must supply rules for building sources it contributes
Core/Src/%.o Core/Src/%.su Core/Src/%.cyclo: ../Core/Src/%.c Core/Src/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m0plus -std=gnu11 -g3 -DDEBUG -DUSE_HAL_DRIVER -DSTM32G0B1xx -c -I../Core/Inc -I../Drivers/STM32G0xx_HAL_Driver/Inc -I../Drivers/STM32G0xx_HAL_Driver/Inc/Legacy -I../Drivers/CMSIS/Device/ST/STM32G0xx/Include -I../Drivers/CMSIS/Include -I"C:/Users/jorda/Documents/GitHub/STM32G0B1_Fabrica_Firmware/STM32G0B1_Applciationprog/PowerStage/App" -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfloat-abi=soft -mthumb -o "$@"

clean: clean-Core-2f-Src

clean-Core-2f-Src:
	-$(RM) ./Core/Src/RunningHorseDisplay.cyclo ./Core/Src/RunningHorseDisplay.d ./Core/Src/RunningHorseDisplay.o ./Core/Src/RunningHorseDisplay.su ./Core/Src/can_operation.cyclo ./Core/Src/can_operation.d ./Core/Src/can_operation.o ./Core/Src/can_operation.su ./Core/Src/display_scheduler.cyclo ./Core/Src/display_scheduler.d ./Core/Src/display_scheduler.o ./Core/Src/display_scheduler.su ./Core/Src/eeprom_driver.cyclo ./Core/Src/eeprom_driver.d ./Core/Src/eeprom_driver.o ./Core/Src/eeprom_driver.su ./Core/Src/fan_ctrl.cyclo ./Core/Src/fan_ctrl.d ./Core/Src/fan_ctrl.o ./Core/Src/fan_ctrl.su ./Core/Src/fonts.cyclo ./Core/Src/fonts.d ./Core/Src/fonts.o ./Core/Src/fonts.su ./Core/Src/io_module.cyclo ./Core/Src/io_module.d ./Core/Src/io_module.o ./Core/Src/io_module.su ./Core/Src/main.cyclo ./Core/Src/main.d ./Core/Src/main.o ./Core/Src/main.su ./Core/Src/powerstage_app.cyclo ./Core/Src/powerstage_app.d ./Core/Src/powerstage_app.o ./Core/Src/powerstage_app.su ./Core/Src/ssd1306.cyclo ./Core/Src/ssd1306.d ./Core/Src/ssd1306.o ./Core/Src/ssd1306.su ./Core/Src/stm32g0xx_hal_msp.cyclo ./Core/Src/stm32g0xx_hal_msp.d ./Core/Src/stm32g0xx_hal_msp.o ./Core/Src/stm32g0xx_hal_msp.su ./Core/Src/stm32g0xx_it.cyclo ./Core/Src/stm32g0xx_it.d ./Core/Src/stm32g0xx_it.o ./Core/Src/stm32g0xx_it.su ./Core/Src/syscalls.cyclo ./Core/Src/syscalls.d ./Core/Src/syscalls.o ./Core/Src/syscalls.su ./Core/Src/sysmem.cyclo ./Core/Src/sysmem.d ./Core/Src/sysmem.o ./Core/Src/sysmem.su ./Core/Src/system_stm32g0xx.cyclo ./Core/Src/system_stm32g0xx.d ./Core/Src/system_stm32g0xx.o ./Core/Src/system_stm32g0xx.su ./Core/Src/ui_display.cyclo ./Core/Src/ui_display.d ./Core/Src/ui_display.o ./Core/Src/ui_display.su

.PHONY: clean-Core-2f-Src

