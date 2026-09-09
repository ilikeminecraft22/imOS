CC = x86_64-elf-gcc
LD = x86_64-elf-ld
NASM = nasm

CFLAGS = -ffreestanding -mcmodel=large -mno-red-zone -O2 \
         -mno-mmx -mno-sse -mno-sse2

LDFLAGS = -m elf_x86_64 -z max-page-size=0x1000 \
          -T boot/linker.ld -nostdlib

OUT = out

C_SOURCES = \
	kernel/kernel.c \
	kernel/lib/vga.c \
	kernel/lib/stdcon.c \
	kernel/lib/idt.c \
	kernel/lib/kbhandler.c \
	kernel/lib/pit.c \
	kernel/lib/shell.c \
	kernel/drivers/ata/ata.c \
	kernel/fs/fat32/fat32.c

C_OBJECTS = $(C_SOURCES:%.c=$(OUT)/%.o)

ASM_OBJECTS = \
	$(OUT)/kernel/lib/interrupts.o \
	$(OUT)/boot/boot.o

OBJECTS = $(C_OBJECTS) $(ASM_OBJECTS)

.PHONY: all clean iso

all: imOS.iso

$(OUT)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(OUT)/kernel/lib/interrupts.o: kernel/lib/interrupts.asm
	@mkdir -p $(dir $@)
	$(NASM) -f elf64 $< -o $@

$(OUT)/boot/boot.o: boot/boot.s
	@mkdir -p $(dir $@)
	$(CC) -c $< -o $@

imOS.bin: $(OBJECTS)
	$(LD) $(LDFLAGS) $(OBJECTS) -o $@
	grub-file --is-x86-multiboot2 $@

imOS.iso: imOS.bin boot/grub.cfg
	mkdir -p isodir/boot/grub
	cp imOS.bin isodir/boot/imOS.bin
	cp boot/grub.cfg isodir/boot/grub/grub.cfg
	grub-mkrescue -o $@ isodir
	@echo "Build successful! Generated imOS.iso"

clean:
	rm -rf $(OUT) isodir imOS.bin imOS.iso