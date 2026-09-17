TARGET = epaper_bootloader
BUILD_DIR = build_bootloader
DEBUG = 1
OPT = -Os

C_SOURCES = \
Bootloader/Src/boot_main.c \
Bootloader/Src/ota_bootloader.c \
Bootloader/Src/ota_crypto.c \
Drivers/STM32G0xx_HAL_Driver/Src/stm32g0xx_hal.c \
Drivers/STM32G0xx_HAL_Driver/Src/stm32g0xx_hal_cortex.c \
Drivers/STM32G0xx_HAL_Driver/Src/stm32g0xx_hal_flash.c \
Drivers/STM32G0xx_HAL_Driver/Src/stm32g0xx_hal_flash_ex.c \
Drivers/STM32G0xx_HAL_Driver/Src/stm32g0xx_hal_gpio.c \
Drivers/STM32G0xx_HAL_Driver/Src/stm32g0xx_hal_pwr.c \
Drivers/STM32G0xx_HAL_Driver/Src/stm32g0xx_hal_pwr_ex.c \
Drivers/STM32G0xx_HAL_Driver/Src/stm32g0xx_hal_rcc.c \
Drivers/STM32G0xx_HAL_Driver/Src/stm32g0xx_hal_rcc_ex.c \
Drivers/STM32G0xx_HAL_Driver/Src/stm32g0xx_hal_uart.c \
Drivers/STM32G0xx_HAL_Driver/Src/stm32g0xx_hal_uart_ex.c \
Core/Src/system_stm32g0xx.c

ASM_SOURCES = startup_stm32g070xx.s
PREFIX = arm-none-eabi-
ifdef GCC_PATH
CC = "$(GCC_PATH)/$(PREFIX)gcc"
AS = "$(GCC_PATH)/$(PREFIX)gcc" -x assembler-with-cpp
CP = "$(GCC_PATH)/$(PREFIX)objcopy"
SZ = "$(GCC_PATH)/$(PREFIX)size"
else
CC = $(PREFIX)gcc
AS = $(PREFIX)gcc -x assembler-with-cpp
CP = $(PREFIX)objcopy
SZ = $(PREFIX)size
endif

CPU = -mcpu=cortex-m0plus
MCU = $(CPU) -mthumb
C_DEFS = -DUSE_HAL_DRIVER -DSTM32G070xx
C_INCLUDES = \
-IBootloader/Inc \
-ICore/Inc \
-IDrivers/STM32G0xx_HAL_Driver/Inc \
-IDrivers/STM32G0xx_HAL_Driver/Inc/Legacy \
-IDrivers/CMSIS/Device/ST/STM32G0xx/Include \
-IDrivers/CMSIS/Include
CFLAGS = $(MCU) $(C_DEFS) $(C_INCLUDES) $(OPT) -Wall -Wextra \
         -fdata-sections -ffunction-sections -MMD -MP \
         -MF"$(@:%.o=%.d)"
ASFLAGS = $(MCU) $(OPT) -Wall -fdata-sections -ffunction-sections
ifeq ($(DEBUG),1)
CFLAGS += -g -gdwarf-2
ASFLAGS += -g -gdwarf-2
endif

LDSCRIPT = Bootloader/STM32G070CBTx_BOOT.ld
LIBS = -lc -lm -lnosys
LDFLAGS = $(MCU) -specs=nano.specs -T$(LDSCRIPT) $(LIBS) \
          -Wl,-Map=$(BUILD_DIR)/$(TARGET).map,--cref \
          -Wl,--gc-sections

OBJECTS = $(addprefix $(BUILD_DIR)/,$(notdir $(C_SOURCES:.c=.o)))
OBJECTS += $(addprefix $(BUILD_DIR)/,$(notdir $(ASM_SOURCES:.s=.o)))
vpath %.c $(sort $(dir $(C_SOURCES)))
vpath %.s $(sort $(dir $(ASM_SOURCES)))

all: $(BUILD_DIR)/$(TARGET).elf $(BUILD_DIR)/$(TARGET).hex \
     $(BUILD_DIR)/$(TARGET).bin

$(BUILD_DIR)/%.o: %.c Bootloader/Makefile | $(BUILD_DIR)
	$(CC) -c $(CFLAGS) $< -o $@

$(BUILD_DIR)/%.o: %.s Bootloader/Makefile | $(BUILD_DIR)
	$(AS) -c $(ASFLAGS) $< -o $@

$(BUILD_DIR)/$(TARGET).elf: $(OBJECTS) Bootloader/Makefile
	$(CC) $(OBJECTS) $(LDFLAGS) -o $@
	$(SZ) $@

$(BUILD_DIR)/%.hex: $(BUILD_DIR)/%.elf | $(BUILD_DIR)
	$(CP) -O ihex $< $@

$(BUILD_DIR)/%.bin: $(BUILD_DIR)/%.elf | $(BUILD_DIR)
	$(CP) -O binary -S $< $@

$(BUILD_DIR):
ifeq ($(OS),Windows_NT)
	powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools/build-directory.ps1 -Path "$(BUILD_DIR)"
else
	mkdir -p $@
endif

clean:
ifeq ($(OS),Windows_NT)
	powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools/build-directory.ps1 -Path "$(BUILD_DIR)" -Clean
else
	-rm -fR $(BUILD_DIR)
endif

.PHONY: all clean
-include $(wildcard $(BUILD_DIR)/*.d)
