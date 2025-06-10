#!/bin/sh -ex

export PREFIX="$PWD/tools"
export TARGET=x86_64-elf
export PATH="$PREFIX/bin:$PATH"
SRCDIR="$PREFIX/src"

BINUTILS=binutils-2.44
GCC=gcc-15.1.0

echo "PREFIX: $PREFIX"

# Setup source directory
mkdir -p "$SRCDIR"

# Build Binutils
if [ ! -f "$SRCDIR/$BINUTILS.tar.xz" ]; then
	curl --fail -o "$SRCDIR/$BINUTILS.tar.xz" "https://ftp.gnu.org/gnu/binutils/$BINUTILS.tar.xz"
	tar -xf "$SRCDIR/$BINUTILS.tar.xz" -C "$SRCDIR"
	(
		mkdir -p "$SRCDIR/build-binutils"
		cd "$SRCDIR/build-binutils"
		../$BINUTILS/configure --target=$TARGET --prefix="$PREFIX" --with-sysroot --disable-nls --disable-werror
		make -j"$(nproc)" install
	)
fi

# Build GCC
if [ ! -f "$SRCDIR/$GCC.tar.xz" ]; then
	which -- $TARGET-as || echo "$TARGET-as is not in the PATH"

	curl --fail -o "$SRCDIR/$GCC.tar.xz" "https://ftp.gnu.org/gnu/gcc/$GCC/$GCC.tar.xz"
	tar -xf "$SRCDIR/$GCC.tar.xz" -C "$SRCDIR"

	# Add no-red-zone configuration
	mkdir -p "$SRCDIR/$GCC/config/i386"
	cat >"$SRCDIR/$GCC/config/i386/t-x86_64-elf" <<EOL
MULTILIB_OPTIONS += mno-red-zone
MULTILIB_DIRNAMES += no-red-zone
EOL

	# Update GCC configuration
	sed -i '/x86_64-\*-elf\*)/{
n
s|^\(\s*\)tm_file="|\1tmake_file="${tmake_file} i386/t-x86_64-elf" # include the new multilib configuration\
\1tm_file="|
}' "$SRCDIR/$GCC/gcc/config.gcc"

	(
		mkdir -p "$SRCDIR/build-gcc"
		cd "$SRCDIR/build-gcc"
		../$GCC/configure --target=$TARGET --prefix="$PREFIX" --disable-nls --enable-languages=c,c++ --without-headers --disable-hosted-libstdcxx
		make -j"$(nproc)" all-gcc all-target-libgcc all-target-libstdc++-v3 install-gcc install-target-libgcc install-target-libstdc++-v3
	)
fi
