TARGET = 429_DSP

PREFIX = arm-none-eabi-

CC = $(PREFIX)gcc
AS = $(PREFIX)gcc
CP = $(PREFIX)objcopy
SZ = $(PREFIX)size

CPU = -mcpu=cortex-m4
FPU = -mfpu=fpv4-sp-d16
FLOAT_ABI = -mfloat-abi=hard

MCU = $(CPU) -mthumb $(FPU) $(FLOAT_ABI)

C_DEFS = \
-DSTM32F429xx

C_INCLUDES = \
-ICore/Inc \
-IDrivers/CMSIS/Core/Include \
-IDrivers/CMSIS/Device/ST/STM32F4xx/Include \
-IDrivers/STM32F4xx_HAL_Driver/Inc \
-ICore/Inc

C_SOURCES = \
Core/Src/main.c \
Core/Src/system_stm32f4xx.c \
Core/Src/uart.c \
Core/Src/ltdc_ili9341.c \
Core/Src/f429_sdram.c \
Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal.c \
Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_gpio.c \
Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_spi.c \
Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_ltdc.c \
Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_sdram.c \
Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_rcc.c \
Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_rcc_ex.c \
Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_cortex.c

ASM_SOURCES = \
Startup/startup_stm32f429xx.s

CFLAGS = \
$(MCU) \
$(C_DEFS) \
$(C_INCLUDES) \
-Wall \
-g

LDFLAGS = \
$(MCU) \
-TLinker/STM32F429ZITX_FLASH.ld\
-Wl,-Map=build/$(TARGET).map \
-Wl,--gc-sections

OBJECTS = $(C_SOURCES:.c=.o)
OBJECTS += $(ASM_SOURCES:.s=.o)

all: build/$(TARGET).elf build/$(TARGET).bin

build/$(TARGET).elf: $(OBJECTS)
	mkdir -p build
	$(CC) $(OBJECTS) $(LDFLAGS) -o $@
	$(SZ) $@

build/$(TARGET).bin: build/$(TARGET).elf
	$(CP) -O binary $< $@

%.o: %.c
	$(CC) -c $< $(CFLAGS) -o $@

%.o: %.s
	$(AS) -c $< $(MCU) -o $@

clean:
	find . -name "*.o" -delete
	rm -rf build/*

.PHONY: all clean
