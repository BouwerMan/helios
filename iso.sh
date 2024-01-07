#!/bin/sh
set -e
. ./build.sh

mkdir -p isodir
mkdir -p isodir/boot
mkdir -p isodir/boot/grub

cp sysroot/boot/$OSNAME.kernel isodir/boot/$OSNAME.kernel
cat >isodir/boot/grub/grub.cfg <<EOF
menuentry "$OSNAME" {
    multiboot /boot/$OSNAME.kernel
}
EOF
grub-mkrescue -o $OSNAME.iso isodir
