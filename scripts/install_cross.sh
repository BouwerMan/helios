#!/bin/sh -x

export PREFIX="$HOME/opt/cross"
export TARGET=x86_64-elf
export PATH="$PREFIX/bin:$PATH"

BINUTILS=binutils-2.44
GCC=gcc-15.1.0

# Setup src
mkdir -p "$HOME/src"
cd "$HOME/src" || exit
# Going to just go ahead and use the xz file as the way to tell if I should rebuild
if ! [ -f "$HOME/src/$BINUTILS.tar.xz" ]; then
	curl -o "$HOME/src/$BINUTILS.tar.xz" "https://ftp.gnu.org/gnu/binutils/$BINUTILS.tar.xz"
	# TODO: Do checksum
	tar -xf $BINUTILS.tar.xz
	mkdir -p build-binutils
	cd build-binutils || exit
	../$BINUTILS/configure --target=$TARGET --prefix="$PREFIX" --with-sysroot --disable-nls --disable-werror
	make -j"$(nproc)"
	make install
fi

cd "$HOME/src" || exit

if ! [ -f "$HOME/src/$GCC.tar.xz" ]; then
	# The $PREFIX/bin dir _must_ be in the PATH. We did that above.
	which -- $TARGET-as || echo $TARGET-as is not in the PATH

	curl -o "$HOME/src/$GCC.tar.xz" "https://ftp.gnu.org/gnu/gcc/$GCC/$GCC.tar.xz"
	tar -xf $GCC.tar.xz

	# no redzone
	mkdir -p "$HOME/src/${GCC}/config/i386"
	cat >"$HOME/src/${GCC}/config/i386/t-x86_64-elf" <<EOL
MULTILIB_OPTIONS += mno-red-zone
MULTILIB_DIRNAMES += no-red-zone
EOL

	# Make sure to use single quotes since ${tmake_file} is in the gcc config not here
	sed -i '/x86_64-\*-elf\*)/{
n
s|^\(\s*\)tm_file="|\1tmake_file="${tmake_file} i386/t-x86_64-elf" # include the new multilib configuration\
\1tm_file="|
}' "$HOME/src/${GCC}/gcc/config.gcc"

	mkdir -p build-gcc
	cd build-gcc || exit

	../$GCC/configure --target=$TARGET --prefix="$PREFIX" --disable-nls --enable-languages=c,c++ --without-headers --disable-hosted-libstdcxx
	make all-gcc -j"$(nproc)"
	make all-target-libgcc -j"$(nproc)"
	make all-target-libstdc++-v3 -j"$(nproc)"
	make install-gcc
	make install-target-libgcc
	make install-target-libstdc++-v3
fi
