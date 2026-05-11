TARGET = stm32l152xb

PREFIX = arm-none-eabi-
CC = $(PREFIX)gcc
AS = $(PREFIX)gcc
LD = $(PREFIX)ld
OBJCOPY = $(PREFIX)objcopy

MCU = cortex-m3

CFLAGS = -mcpu=$(MCU) -mthumb -mfloat-abi=soft
CFLAGS += -Wall -Wextra -O0 -g3
CFLAGS += -Iinc -DSTM32L152xB

ASFLAGS = -mcpu=$(MCU) -mthumb -mfloat-abi=soft

LDSCRIPT = stm32l152xb.ld

SRC_C = main.c system_stm32l1xx.c
SRC_AS = startup_stm32l152xb.s

OBJ = $(SRC_C:.c=.o) $(SRC_AS:.s=.o)

all: $(TARGET).elf $(TARGET).bin

$(TARGET).elf: $(OBJ)
	$(CC) $(CFLAGS) -T$(LDSCRIPT) -o $@ $^ -nostartfiles -nodefaultlibs

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

%.o: %.s
	$(AS) $(ASFLAGS) -Iinc -x assembler-with-cpp -c -o $@ $<

$(TARGET).bin: $(TARGET).elf
	$(OBJCOPY) -O binary $< $@

clean:
	rm -f $(OBJ) $(TARGET).elf $(TARGET).bin

flash: $(TARGET).bin
	st-flash --reset write $< 0x08000000

.PHONY: all clean flash