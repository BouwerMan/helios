#!/bin/sh
set -e
. ./config.sh

for PROJECT in $PROJECTS; do
	(cd $PROJECT && $MAKE clean)
done

rm -rvf sysroot
rm -rvf isodir
rm -rvf *.iso
