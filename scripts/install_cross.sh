#!/usr/bin/env bash
set -e

# --- Configuration ---
# Set the target architecture for the cross-compiler.
export TARGET=x86_64-elf

# Set the destination directory for the compiled tools.
export PREFIX="$PWD/tools"

# Add the new tools to the path for this script's execution.
export PATH="$PREFIX/bin:$PATH"

# Define the source and build directories.
SRCDIR="$PREFIX/src"

# Define versions for the tools to be built.
BINUTILS_VERSION=binutils-2.45
GCC_VERSION=gcc-15.2.0

# Set to true to build the C++ compiler, false for C only.
BUILD_CXX=false

# Function to check for required command-line tools.
check_dependencies() {
	echo "--- Checking for dependencies ---"
	local missing_deps=0

	# List of essential commands (texinfo is makeinfo)
	local deps=("git" "patch" "make" "bison" "flex" "makeinfo" "gcc" "g++")

	for dep in "${deps[@]}"; do
		if ! command -v "$dep" &>/dev/null; then
			echo "Error: Required dependency '$dep' is not installed."
			missing_deps=1
		fi
	done

	if [ "$missing_deps" -ne 0 ]; then
		echo "Please install the missing dependencies and try again."
		exit 1
	fi
	echo "All dependencies found."
}

# Function to download or update the Binutils source code and build it.
build_binutils() {
	echo "--- Building Binutils ($BINUTILS_VERSION) ---"

	# If the linker already exists, skip the build.
	if [ -f "$PREFIX/bin/$TARGET-ld" ]; then
		echo "Binutils already appears to be installed. Skipping."
		return 0
	fi

	local source_dir="$SRCDIR/$BINUTILS_VERSION"
	local build_dir="$SRCDIR/build-binutils"
	local branch_name="${BINUTILS_VERSION//./_}-branch"
	local repo_url="https://sourceware.org/git/binutils-gdb.git"

	# Get or Update Source Code
	if [ -d "$source_dir" ]; then
		echo "Updating existing Binutils source..."
		(
			cd "$source_dir" || exit 1
			git fetch --quiet --depth 1 origin "$branch_name"
			git reset --quiet --hard "origin/$branch_name"
		)
	else
		echo "Cloning Binutils source..."
		git clone --quiet --depth 1 --branch "$branch_name" "$repo_url" "$source_dir"
	fi

	# Configure and Build
	echo "Cleaning up previous build directory..."
	rm -rf "$build_dir"

	echo "Configuring and building..."
	mkdir -p "$build_dir"
	(
		cd "$build_dir" || exit 1

		# Configure the build
		"$source_dir/configure" --target="$TARGET" \
			--prefix="$PREFIX" \
			--with-sysroot \
			--disable-nls \
			--disable-werror

		# Compile and install
		make -j"$(nproc)"
		make -j"$(nproc)" install
	)

	echo "Binutils build and installation complete."
}

# Function to download or update the Binutils source code and build it.
build_gcc() {
	echo "--- Building GCC ($GCC_VERSION) ---"

	# If the compiler already exists, skip the build.
	if [ -f "$PREFIX/bin/$TARGET-gcc" ]; then
		echo "GCC already appears to be installed. Skipping."
		return 0
	fi

	local source_dir="$SRCDIR/$GCC_VERSION"
	local build_dir="$SRCDIR/build-gcc"
	local tag_name="releases/$GCC_VERSION"
	local repo_url="git://gcc.gnu.org/git/gcc.git"
	local patch_file="$PWD/scripts/patches/gcc-no-red-zone.patch"

	# Get or Update Source Code
	if [ -d "$source_dir" ]; then
		echo "Updating existing GCC source..."
		(
			cd "$source_dir" || exit 1
			git fetch --quiet --depth 1 origin "$tag_name"
			git checkout --quiet "$tag_name"
		)
	else
		echo "Cloning GCC source..."
		git clone --quiet --depth 1 --branch "$tag_name" "$repo_url" "$source_dir"
	fi

	# Apply Patches
	(
		cd "$source_dir" || exit 1

		echo "Downloading GCC prerequisites (GMP, MPFR, MPC)..."
		./contrib/download_prerequisites

		if [ -f "$patch_file" ]; then
			echo "Applying no-red-zone patch..."
			# Use -N to ignore patches that have already been applied
			patch -p1 -N <"$patch_file" || true
		else
			echo "Warning: Patch file not found at $patch_file"
		fi
	)

	# Configure and Build

	local languages="c"
	local configure_flags=""
	local make_targets_all="all-gcc all-target-libgcc"
	local make_targets_install="install-gcc install-target-libgcc"

	if [ "$BUILD_CXX" = true ]; then
		echo "C++ support is enabled."
		languages="c,c++"
		configure_flags="--disable-hosted-libstdcxx"
		make_targets_all="$make_targets_all all-target-libstdc++-v3"
		make_targets_install="$make_targets_install install-target-libstdc++-v3"
	else
		echo "C++ support is disabled (C only)."
	fi

	echo "Cleaning up previous build directory..."
	rm -rf "$build_dir"

	echo "Configuring and building..."
	mkdir -p "$build_dir"
	(
		cd "$build_dir" || exit 1

		# Configure the build
		"$source_dir/configure" --target="$TARGET" \
			--prefix="$PREFIX" \
			--disable-nls \
			--enable-languages="$languages" \
			--without-headers \
			$configure_flags

		# Compile the selected components
		make -j"$(nproc)" $make_targets_all

		# Install the compiled components
		make $make_targets_install
	)

	echo "GCC build and installation complete."
}

# --- Script Execution ---
main() {
	echo "Starting cross-compiler toolchain build..."
	echo "Installation Prefix: $PREFIX"

	check_dependencies

	# Ensure the source directory exists
	mkdir -p "$SRCDIR"

	# Build the components
	build_binutils
	build_gcc # You can add the function call for GCC here later

	echo "Toolchain build finished successfully!"
}

main
