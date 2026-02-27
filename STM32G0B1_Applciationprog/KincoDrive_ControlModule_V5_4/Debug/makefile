################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (13.3.rel1)
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


BUILD_ARTIFACT_NAME := Actuation_IO_Distribution_Board_Embedded
BUILD_ARTIFACT_EXTENSION := elf
BUILD_ARTIFACT_PREFIX :=
BUILD_ARTIFACT := $(BUILD_ARTIFACT_PREFIX)$(BUILD_ARTIFACT_NAME)$(if $(BUILD_ARTIFACT_EXTENSION),.$(BUILD_ARTIFACT_EXTENSION),)

# Add inputs and outputs from these tool invocations to the build variables 
EXECUTABLES += \
Actuation_IO_Distribution_Board_Embedded.elf \

MAP_FILES += \
Actuation_IO_Distribution_Board_Embedded.map \

SIZE_OUTPUT += \
default.size.stdout \

OBJDUMP_LIST += \
Actuation_IO_Distribution_Board_Embedded.list \

OBJCOPY_BIN += \
Actuation_IO_Distribution_Board_Embedded.bin \

OBJCOPY_SREC += \
Actuation_IO_Distribution_Board_Embedded.srec \


# All Target
all: main-build

# Main-build Target
main-build: Actuation_IO_Distribution_Board_Embedded.elf secondary-outputs

# Tool invocations
Actuation_IO_Distribution_Board_Embedded.elf Actuation_IO_Distribution_Board_Embedded.map: $(OBJS) $(USER_OBJS) C:\MASTERFOLDER\04_work\00_new\ main\08_STM32_CustomBoard_Release\Actuation_IO_Distribution_Board_Embedded\STM32G0B1RETX_FLASH.ld makefile objects.list $(OPTIONAL_TOOL_DEPS)
	arm-none-eabi-gcc -o "Actuation_IO_Distribution_Board_Embedded.elf" @"objects.list" $(USER_OBJS) $(LIBS) -mcpu=cortex-m0plus -T"C:\MASTERFOLDER\04_work\00_new main\08_STM32_CustomBoard_Release\Actuation_IO_Distribution_Board_Embedded\STM32G0B1RETX_FLASH.ld" --specs=nosys.specs -Wl,-Map="Actuation_IO_Distribution_Board_Embedded.map" -Wl,--gc-sections -static --specs=nano.specs -mfloat-abi=soft -mthumb -Wl,--start-group -lc -lm -Wl,--end-group
	@echo 'Finished building target: $@'
	@echo ' '

default.size.stdout: $(EXECUTABLES) makefile objects.list $(OPTIONAL_TOOL_DEPS)
	arm-none-eabi-size  $(EXECUTABLES)
	@echo 'Finished building: $@'
	@echo ' '

Actuation_IO_Distribution_Board_Embedded.list: $(EXECUTABLES) makefile objects.list $(OPTIONAL_TOOL_DEPS)
	arm-none-eabi-objdump -h -S $(EXECUTABLES) > "Actuation_IO_Distribution_Board_Embedded.list"
	@echo 'Finished building: $@'
	@echo ' '

Actuation_IO_Distribution_Board_Embedded.bin: $(EXECUTABLES) makefile objects.list $(OPTIONAL_TOOL_DEPS)
	arm-none-eabi-objcopy  -O binary $(EXECUTABLES) "Actuation_IO_Distribution_Board_Embedded.bin"
	@echo 'Finished building: $@'
	@echo ' '

Actuation_IO_Distribution_Board_Embedded.srec: $(EXECUTABLES) makefile objects.list $(OPTIONAL_TOOL_DEPS)
	arm-none-eabi-objcopy  -O srec $(EXECUTABLES) "Actuation_IO_Distribution_Board_Embedded.srec"
	@echo 'Finished building: $@'
	@echo ' '

# Other Targets
clean:
	-$(RM) Actuation_IO_Distribution_Board_Embedded.bin Actuation_IO_Distribution_Board_Embedded.elf Actuation_IO_Distribution_Board_Embedded.list Actuation_IO_Distribution_Board_Embedded.map Actuation_IO_Distribution_Board_Embedded.srec default.size.stdout
	-@echo ' '

secondary-outputs: $(SIZE_OUTPUT) $(OBJDUMP_LIST) $(OBJCOPY_BIN) $(OBJCOPY_SREC)

fail-specified-linker-script-missing:
	@echo 'Error: Cannot find the specified linker script. Check the linker settings in the build configuration.'
	@exit 2

warn-no-linker-script-specified:
	@echo 'Warning: No linker script specified. Check the linker settings in the build configuration.'

.PHONY: all clean dependents main-build fail-specified-linker-script-missing warn-no-linker-script-specified

-include ../makefile.targets
