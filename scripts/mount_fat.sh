#!/bin/sh
sudo losetup --offset $((512 * 2048)) --sizelimit $((512 * 1046528)) --show --find fat.img
sudo mount /dev/loop0 ./mnt/fatimg
