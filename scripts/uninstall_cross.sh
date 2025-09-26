#!/usr/bin/env bash

# Set the directory where the toolchain was installed.
# This should match the PREFIX variable in your build script.
INSTALL_DIR="$PWD/tools"

main() {
	echo "This script will uninstall the cross-compiler toolchain."
	echo "It will permanently delete the following directory:"
	echo "  $INSTALL_DIR"
	echo ""

	# Check if the directory actually exists before proceeding.
	if [ ! -d "$INSTALL_DIR" ]; then
		echo "Directory not found. It seems the toolchain is not installed here."
		exit 0
	fi

	# Ask the user for confirmation.
	# The -p flag displays a prompt to the user.
	# The -r flag prevents backslash escapes from being interpreted.
	read -p "Are you sure you want to continue? [y/N] " -r reply
	echo "" # Add a newline for better formatting.

	# Check the user's response.
	# We use a case statement to handle different inputs (Y, y, Yes, yes).
	case "$reply" in
	[yY][eE][sS] | [yY])
		echo "Uninstalling..."
		rm -rf "$INSTALL_DIR"
		echo "Cross-compiler toolchain has been uninstalled."
		;;
	*)
		echo "Uninstall cancelled."
		exit 1
		;;
	esac
}

# Run the main function
main
