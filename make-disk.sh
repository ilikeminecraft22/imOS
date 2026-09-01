#!/bin/bash

set -e

rm -f disk.img

dd if=/dev/zero of=disk.img bs=1M count=64

mkfs.fat -F 32 disk.img

mkdir -p /tmp/imos-fat

sudo mount -o loop disk.img /tmp/imos-fat

echo "Hello from imOS!" | sudo tee /tmp/imos-fat/HELLO.TXT > /dev/null

sudo umount /tmp/imos-fat

sudo chown "$USER:$USER" disk.img

echo "FAT32 disk created successfully!"