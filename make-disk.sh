#!/bin/bash

set -e

rm -f disk.img

dd if=/dev/zero of=disk.img bs=1M count=64

mkfs.fat -F 32 disk.img

mkdir -p /tmp/imos-fat

sudo mount -o loop disk.img /tmp/imos-fat

echo "This is the original TEST.TXT file." | sudo tee /tmp/imos-fat/TEST.TXT > /dev/null
sudo mkdir -p /tmp/imos-fat/TESTDIR
echo "HELLO, WORLD!" | sudo tee /tmp/imos-fat/TESTDIR/HELLO.TXT > /dev/null
echo "HELLO, WORLD LONG NESTED FILENAME!" | sudo tee "/tmp/imos-fat/TESTDIR/LONG NESTED FILENAME.TXT" > /dev/null
echo "HELLO, WORLD LONG FILENAME!" | sudo tee "/tmp/imos-fat/LONG FILENAME TEST.TXT" > /dev/null


sudo umount /tmp/imos-fat

sudo chown "$USER:$USER" disk.img

echo "FAT32 disk created successfully!"