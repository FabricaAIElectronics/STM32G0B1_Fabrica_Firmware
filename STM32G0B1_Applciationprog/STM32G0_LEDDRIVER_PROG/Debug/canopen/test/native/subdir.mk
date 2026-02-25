################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (14.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../canopen/test/native/test_canopen_buffer.c \
../canopen/test/native/test_canopen_frame.c \
../canopen/test/native/test_canopen_heartbeat.c \
../canopen/test/native/test_canopen_nmt.c \
../canopen/test/native/test_canopen_obj_dict.c \
../canopen/test/native/test_canopen_pdo.c \
../canopen/test/native/test_canopen_sdo.c \
../canopen/test/native/test_sanity.c 

OBJS += \
./canopen/test/native/test_canopen_buffer.o \
./canopen/test/native/test_canopen_frame.o \
./canopen/test/native/test_canopen_heartbeat.o \
./canopen/test/native/test_canopen_nmt.o \
./canopen/test/native/test_canopen_obj_dict.o \
./canopen/test/native/test_canopen_pdo.o \
./canopen/test/native/test_canopen_sdo.o \
./canopen/test/native/test_sanity.o 

C_DEPS += \
./canopen/test/native/test_canopen_buffer.d \
./canopen/test/native/test_canopen_frame.d \
./canopen/test/native/test_canopen_heartbeat.d \
./canopen/test/native/test_canopen_nmt.d \
./canopen/test/native/test_canopen_obj_dict.d \
./canopen/test/native/test_canopen_pdo.d \
./canopen/test/native/test_canopen_sdo.d \
./canopen/test/native/test_sanity.d 


# Each subdirectory must supply rules for building sources it contributes
canopen/test/native/%.o canopen/test/native/%.su canopen/test/native/%.cyclo: ../canopen/test/native/%.c canopen/test/native/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m0plus -std=gnu11 -g3 -DDEBUG -DUSE_HAL_DRIVER -DSTM32G0B1xx -c -I../Core/Inc -I../Drivers/STM32G0xx_HAL_Driver/Inc -I../Drivers/STM32G0xx_HAL_Driver/Inc/Legacy -I../Drivers/CMSIS/Device/ST/STM32G0xx/Include -I../Drivers/CMSIS/Include -I"C:/Users/jorda/Documents/GitHub/STM32G0B1_Fabrica_Firmware/STM32G0B1_Applciationprog/STM32G0_LEDDRIVER_PROG/App" -I../canopen/Inc -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfloat-abi=soft -mthumb -o "$@"

clean: clean-canopen-2f-test-2f-native

clean-canopen-2f-test-2f-native:
	-$(RM) ./canopen/test/native/test_canopen_buffer.cyclo ./canopen/test/native/test_canopen_buffer.d ./canopen/test/native/test_canopen_buffer.o ./canopen/test/native/test_canopen_buffer.su ./canopen/test/native/test_canopen_frame.cyclo ./canopen/test/native/test_canopen_frame.d ./canopen/test/native/test_canopen_frame.o ./canopen/test/native/test_canopen_frame.su ./canopen/test/native/test_canopen_heartbeat.cyclo ./canopen/test/native/test_canopen_heartbeat.d ./canopen/test/native/test_canopen_heartbeat.o ./canopen/test/native/test_canopen_heartbeat.su ./canopen/test/native/test_canopen_nmt.cyclo ./canopen/test/native/test_canopen_nmt.d ./canopen/test/native/test_canopen_nmt.o ./canopen/test/native/test_canopen_nmt.su ./canopen/test/native/test_canopen_obj_dict.cyclo ./canopen/test/native/test_canopen_obj_dict.d ./canopen/test/native/test_canopen_obj_dict.o ./canopen/test/native/test_canopen_obj_dict.su ./canopen/test/native/test_canopen_pdo.cyclo ./canopen/test/native/test_canopen_pdo.d ./canopen/test/native/test_canopen_pdo.o ./canopen/test/native/test_canopen_pdo.su ./canopen/test/native/test_canopen_sdo.cyclo ./canopen/test/native/test_canopen_sdo.d ./canopen/test/native/test_canopen_sdo.o ./canopen/test/native/test_canopen_sdo.su ./canopen/test/native/test_sanity.cyclo ./canopen/test/native/test_sanity.d ./canopen/test/native/test_sanity.o ./canopen/test/native/test_sanity.su

.PHONY: clean-canopen-2f-test-2f-native

