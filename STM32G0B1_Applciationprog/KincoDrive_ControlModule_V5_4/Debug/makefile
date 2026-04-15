################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (14.3.rel1)
################################################################################

-include ../makefile.init

RM := rm -rf

# All of the sources participating in the build are defined here
-include sources.mk
-include Drivers/STM32G0xx_HAL_Driver/Src/subdir.mk
-include Core/Startup/subdir.mk
-include Core/Src/subdir.mk
-include objects.mk

ifneq ($(MAKECMDGOALS),clean)
ifneq ($(strip $(S_DEPS)),)
-include $(S_DEPS)
endif
ifneq ($(strip $(S_UPPER_DEPS)),)
-include $(S_UPPER_DEPS)
endif
ifneq ($(strip $(C_DEPS)),)
-include $(C_DEPS)
endif
endif

-include ../makefile.defs

OPTIONAL_TOOL_DEPS := \
$(wildcard ../makefile.defs) \
$(wildcard ../makefile.init) \
$(wildcard ../makefile.targets) \


BUILD_ARTIFACT_NAME := KincoDrive_ControlModule_V5_4
BUILD_ARTIFACT_EXTENSION := elf
BUILD_ARTIFACT_PREFIX :=
BUILD_ARTIFACT := $(BUILD_ARTIFACT_PREFIX)$(BUILD_ARTIFACT_NAME)$(if $(BUILD_ARTIFACT_EXTENSION),.$(BUILD_ARTIFACT_EXTENSION),)

# Add inputs and outputs from these tool invocations to the build variables 
EXECUTABLES += \
KincoDrive_ControlModule_V5_4.elf \

MAP_FILES += \
KincoDrive_ControlModule_V5_4.map \

SIZE_OUTPUT += \
default.size.stdout \

OBJDUMP_LIST += \
KincoDrive_ControlModule_V5_4.list \

OBJCOPY_BIN += \
KincoDrive_ControlModule_V5_4.bin \

OBJCOPY_SREC += \
KincoDrive_ControlModule_V5_4.srec \


# All Target
all: main-build

# Main-build Target
main-build: KincoDrive_ControlModule_V5_4.elf secondary-outputs

# Tool invocations
KincoDrive_ControlModule_V5_4.elf KincoDrive_ControlModule_V5_4.map: $(OBJS) $(USER_OBJS) C:\Users\jorda\Documents\GitHub\STM32G0B1_Fabrica_Firmware\STM32G0B1_Applciationprog\KincoDrive_ControlModule_V5_4\STM32G0B1RETX_FLASH.ld makefile objects.list $(OPTIONAL_TOOL_DEPS)
	arm-none-eabi-gcc -o "KincoDrive_ControlModule_V5_4.elf" @"objects.list" $(USER_OBJS) $(LIBS) -mcpu=cortex-m0plus -T"C:\Users\jorda\Documents\GitHub\STM32G0B1_Fabrica_Firmware\STM32G0B1_Applciationprog\KincoDrive_ControlModule_V5_4\STM32G0B1RETX_FLASH.ld" --specs=nosys.specs -Wl,-Map="KincoDrive_ControlModule_V5_4.map" -Wl,--gc-sections -static --specs=nano.specs -mfloat-abi=soft -mthumb -Wl,--start-group -lc -lm -Wl,--end-group
	@echo 'Finished building target: $@'
	@echo ' '

default.size.stdout: $(EXECUTABLES) makefile objects.list $(OPTIONAL_TOOL_DEPS)
	arm-none-eabi-size  $(EXECUTABLES)
	@echo 'Finished building: $@'
	@echo ' '

KincoDrive_ControlModule_V5_4.list: $(EXECUTABLES) makefile objects.list $(OPTIONAL_TOOL_DEPS)
	arm-none-eabi-objdump -h -S $(EXECUTABLES) > "KincoDrive_ControlModule_V5_4.list"
	@echo 'Finished building: $@'
	@echo ' '

KincoDrive_ControlModule_V5_4.bin: $(EXECUTABLES) makefile objects.list $(OPTIONAL_TOOL_DEPS)
	arm-none-eabi-objcopy  -O binary $(EXECUTABLES) "KincoDrive_ControlModule_V5_4.bin"
	@echo 'Finished building: $@'
	@echo ' '

KincoDrive_ControlModule_V5_4.srec: $(EXECUTABLES) makefile objects.list $(OPTIONAL_TOOL_DEPS)
	arm-none-eabi-objcopy  -O srec $(EXECUTABLES) "KincoDrive_ControlModule_V5_4.srec"
	@echo 'Finished building: $@'
	@echo ' '

# Other Targets
clean:
	-$(RM) KincoDrive_ControlModule_V5_4.bin KincoDrive_ControlModule_V5_4.elf KincoDrive_ControlModule_V5_4.list KincoDrive_ControlModule_V5_4.map KincoDrive_ControlModule_V5_4.srec default.size.stdout
	-@echo ' '

secondary-outputs: $(SIZE_OUTPUT) $(OBJDUMP_LIST) $(OBJCOPY_BIN) $(OBJCOPY_SREC)

fail-specified-linker-script-missing:
	@echo 'Error: Cannot find the specified linker script. Check the linker settings in the build configuration.'
	@exit 2

warn-no-linker-script-specified:
	@echo 'Warning: No linker script specified. Check the linker settings in the build configuration.'

.PHONY: all clean dependents main-build fail-specified-linker-script-missing warn-no-linker-script-specified

-include ../makefile.targets
