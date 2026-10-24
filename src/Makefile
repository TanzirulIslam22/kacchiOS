# Makefile for kacchiOS
CC = gcc
LD = ld
AS = as

CFLAGS = -m32 -ffreestanding -O2 -Wall -Wextra -nostdinc \
         -fno-builtin -fno-stack-protector -I. \
         -mpreferred-stack-boundary=2 -mno-sse -mno-mmx
# -mpreferred-stack-boundary=2 : assume only 4-byte stack alignment.
#   Interrupts can arrive at any instruction, so a process stack pointer
#   can be any 4-byte value.  With this flag gcc never emits code that
#   requires 16-byte alignment (movaps etc.).
# -mno-sse -mno-mmx : never use floating point / SIMD registers, which
#   the context switch below does not save.
ASFLAGS = --32
LDFLAGS = -m elf_i386

OBJS = boot.o kernel.o serial.o string.o memory.o process.o scheduler.o \
       interrupts.o isr.o

all: kernel.elf

kernel.elf: $(OBJS)
	$(LD) $(LDFLAGS) -T link.ld -o $@ $^

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.S
	$(AS) $(ASFLAGS) $< -o $@

run: kernel.elf
	qemu-system-i386 -kernel kernel.elf -m 64M -serial stdio -display none

run-vga: kernel.elf
	qemu-system-i386 -kernel kernel.elf -m 64M -serial mon:stdio

debug: kernel.elf
	qemu-system-i386 -kernel kernel.elf -m 64M -serial stdio -display none -s -S &
	@echo "Waiting for GDB connection on port 1234..."
	@echo "In another terminal run: gdb -ex 'target remote localhost:1234' -ex 'symbol-file kernel.elf'"

clean:
	rm -f *.o kernel.elf

.PHONY: all run run-vga debug clean