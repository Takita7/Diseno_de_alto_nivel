# Makefile — programa bare-metal AArch64 (guest) del prototipo virtual
#
# Requiere un toolchain cruzado AArch64. Con gcc-aarch64-linux-gnu (más fácil
# de conseguir vía apt) alcanza para bare-metal usando -ffreestanding y
# -nostdlib; si tienen a mano un toolchain "aarch64-none-elf" dedicado,
# ajusten CROSS_COMPILE.

CROSS_COMPILE ?= aarch64-linux-gnu-
CC       := $(CROSS_COMPILE)gcc
AS       := $(CROSS_COMPILE)gcc
LD       := $(CROSS_COMPILE)ld
OBJCOPY  := $(CROSS_COMPILE)objcopy

CFLAGS   := -ffreestanding -nostdlib -nostartfiles -O2 -Wall -Wextra \
            -mgeneral-regs-only
ASFLAGS  := -ffreestanding -nostdlib

# Ruta a la libm5 de gem5 (util/m5), compilada para AArch64 bare-metal.
# Ajustar según dónde la hayan construido.
M5_LIB   ?= ../../../gem5/util/m5/build/arm64/out/libm5.a
M5_INC   ?= ../../../gem5/include

TARGET   := accel_test.elf

all: $(TARGET)

$(TARGET): startup.o accel_test.o linker.ld
	$(LD) -T linker.ld -o $@ startup.o accel_test.o $(M5_LIB)

startup.o: startup.S
	$(AS) $(ASFLAGS) -c $< -o $@

accel_test.o: accel_test.c
	$(CC) $(CFLAGS) -I$(M5_INC) -c $< -o $@

clean:
	rm -f *.o $(TARGET)

.PHONY: all clean
