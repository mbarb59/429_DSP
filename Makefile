TARGET = 431_DSP

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
-DSTM32G431xx

C_INCLUDES = \
-ICore/Inc \
-IDrivers/CMSIS/Core/Include \
-IDrivers/CMSIS/Device/ST/STM32G4xx/Include

C_SOURCES = \
Core/Src/main.c \
Core/Src/system_stm32g4xx.c \
Core/Src/uart.c \
Core/Src/console.c \
Core/Src/adc.c \
Core/Src/capture.c


ASM_SOURCES = \
Startup/startup_stm32g431rbtx.s

CFLAGS = \
$(MCU) \
$(C_DEFS) \
$(C_INCLUDES) \
-Wall \
-g

LDFLAGS = \
$(MCU) \
-TLinker/STM32G431RBTX_FLASH.ld \
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