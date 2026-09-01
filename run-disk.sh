#!/bin/bash

qemu-system-x86_64 \
    -machine pc \
    -drive id=hd0,file=disk.img,format=raw,if=ide,bus=0,unit=0 \
    -cdrom imOS.iso \
    -boot order=d \
    -monitor stdio