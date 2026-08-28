#!/bin/bash
set -e

rm -rf out isodir imOS.bin imOS.iso
mkdir -p out/boot
mkdir -p out/kernel/lib

nasm -f elf64 kernel/lib/interrupts.asm -o out/kernel/lib/interrupts.o

x86_64-elf-gcc -c boot/boot.s -o out/boot/boot.o

x86_64-elf-gcc -ffreestanding -mcmodel=large -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -c kernel/kernel.c -o out/kernel/kernel.o
x86_64-elf-gcc -ffreestanding -mcmodel=large -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -c kernel/lib/vga.c -o out/kernel/lib/vga.o
x86_64-elf-gcc -ffreestanding -mcmodel=large -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -c kernel/lib/stdcon.c -o out/kernel/lib/stdcon.o
x86_64-elf-gcc -ffreestanding -mcmodel=large -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -c kernel/lib/idt.c -o out/kernel/lib/idt.o
x86_64-elf-gcc -ffreestanding -mcmodel=large -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -c kernel/lib/kbhandler.c -o out/kernel/lib/kbhandler.o

x86_64-elf-ld -m elf_x86_64 -z max-page-size=0x1000 -T \
 boot/linker.ld out/boot/boot.o out/kernel/kernel.o out/kernel/lib/vga.o out/kernel/lib/stdcon.o out/kernel/lib/kbhandler.o out/kernel/lib/idt.o out/kernel/lib/interrupts.o \
 -o imOS.bin -nostdlib

grub-file --is-x86-multiboot2 imOS.bin

mkdir -p isodir/boot/grub
cp imOS.bin isodir/boot/imOS.bin
cp boot/grub.cfg isodir/boot/grub/grub.cfg
grub-mkrescue -o imOS.iso isodir

echo "Build successful! Generated imOS.iso"