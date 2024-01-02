set -x

mkdir -p isodir/boot/grub
cp build/myos.bin isodir/boot/myos.bin
cp build/grub.cfg isodir/boot/grub/grub.cfg
grub-mkrescue -o myos.iso isodir
