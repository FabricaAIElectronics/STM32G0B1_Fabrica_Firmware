################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (14.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../canopen/Src/canopen_buffer.c \
../canopen/Src/canopen_frame.c \
../canopen/Src/canopen_heartbeat.c \
../canopen/Src/canopen_nmt.c \
../canopen/Src/canopen_obj_dict.c \
../canopen/Src/canopen_pdo.c \
../canopen/Src/canopen_sdo.c 

OBJS += \
./canopen/Src/canopen_buffer.o \
./canopen/Src/canopen_frame.o \
./canopen/Src/canopen_heartbeat.o \
./canopen/Src/canopen_nmt.o \
./canopen/Src/canopen_obj_dict.o \
./canopen/Src/canopen_pdo.o \
./canopen/Src/canopen_sdo.o 

C_DEPS += \
./canopen/Src/canopen_buffer.d \
./canopen/Src/canopen_frame.d \
./canopen/Src/canopen_heartbeat.d \
./canopen/Src/canopen_nmt.d \
./canopen/Src/canopen_obj_dict.d \
./canopen/Src/canopen_pdo.d \
./canopen/Src/canopen_sdo.d 


# Each subdirectory must supply rules for building sources it contributes
canopen/Src/%.o canopen/Src/%.su canopen/Src/%.cyclo: ../canopen/Src/%.c canopen/Src/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m0plus -std=gnu11 -g3 -DDEBUG -DUSE_HAL_DRIVER -DSTM32G0B1xx -c -I../Core/Inc -I../Drivers/STM32G0xx_HAL_Driver/Inc -I../Drivers/STM32G0xx_HAL_Driver/Inc/Legacy -I../Drivers/CMSIS/Device/ST/STM32G0xx/Include -I../Drivers/CMSIS/Include -I"C:/Users/jorda/Documents/GitHub/STM32G0B1_Fabrica_Firmware/STM32G0B1_Applciationprog/STM32G0_LEDDRIVER_PROG/App" -I../canopen/Inc -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfloat-abi=soft -mthumb -o "$@"

clean: clean-canopen-2f-Src

clean-canopen-2f-Src:
	-$(RM) ./canopen/Src/canopen_buffer.cyclo ./canopen/Src/canopen_buffer.d ./canopen/Src/canopen_buffer.o ./canopen/Src/canopen_buffer.su ./canopen/Src/canopen_frame.cyclo ./canopen/Src/canopen_frame.d ./canopen/Src/canopen_frame.o ./canopen/Src/canopen_frame.su ./canopen/Src/canopen_heartbeat.cyclo ./canopen/Src/canopen_heartbeat.d ./canopen/Src/canopen_heartbeat.o ./canopen/Src/canopen_heartbeat.su ./canopen/Src/canopen_nmt.cyclo ./canopen/Src/canopen_nmt.d ./canopen/Src/canopen_nmt.o ./canopen/Src/canopen_nmt.su ./canopen/Src/canopen_obj_dict.cyclo ./canopen/Src/canopen_obj_dict.d ./canopen/Src/canopen_obj_dict.o ./canopen/Src/canopen_obj_dict.su ./canopen/Src/canopen_pdo.cyclo ./canopen/Src/canopen_pdo.d ./canopen/Src/canopen_pdo.o ./canopen/Src/canopen_pdo.su ./canopen/Src/canopen_sdo.cyclo ./canopen/Src/canopen_sdo.d ./canopen/Src/canopen_sdo.o ./canopen/Src/canopen_sdo.su

.PHONY: clean-canopen-2f-Src

