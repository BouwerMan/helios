#!/usr/bin/env bash
# Generates a .gdbinit file for debugging Helios with GDB.
# - Loads kernel as the main file.
# - Auto-loads symbols for userspace ELFs whose sources contain "GDB BREAKPOINT".
# - Sets breakpoints on all lines with "GDB BREAKPOINT" in helios/libc/userspace.

set -euo pipefail

# Find the kernel file (assuming only one exists)
find_kernel() {
	local kernel_file
	kernel_file=$(find helios -name "*.kernel" -print -quit 2>/dev/null)

	if [[ -z "$kernel_file" ]]; then
		echo "Error: No kernel file found" >&2
		exit 1
	fi

	echo "$kernel_file"
}

# Find userspace programs with GDB breakpoints
find_programs_with_breakpoints() {
	local programs=()
	local prog_dir prog_name elf

	echo "Scanning for userspace programs with GDB breakpoints..." >&2

	# Use process substitution to avoid subshell array issues
	while IFS= read -r -d '' prog_dir; do
		prog_name=$(basename "$prog_dir")
		elf="sysroot/usr/bin/${prog_name}.elf"

		# Check if ELF exists and has GDB BREAKPOINT tags (combine conditions for efficiency)
		if [[ -f "$elf" ]] && grep -r --quiet "GDB BREAKPOINT" "$prog_dir" 2>/dev/null; then
			programs+=("$elf")
			echo "  + will load symbols for: $elf" >&2
		fi
	done < <(find userspace -maxdepth 1 -mindepth 1 -type d -print0 2>/dev/null)

	printf '%s\n' "${programs[@]}"
}

# Find all breakpoint locations in one pass
find_breakpoint_locations() {
	local breakpoint_count=0
	local file line

	echo "Setting breakpoints for GDB BREAKPOINT markers..." >&2

	# Use single find + grep pipeline for efficiency
	while IFS=: read -r file line _; do
		[[ -n "$file" && -n "$line" ]] || continue
		echo "break $file:$((line + 1))"
		((breakpoint_count += 1))
	done < <(find helios libc userspace -type f \( -name "*.c" -o -name "*.h" -o -name "*.asm" \) \
		-exec grep -Hn "GDB BREAKPOINT" {} + 2>/dev/null)

	echo "  + set $breakpoint_count breakpoints" >&2
}

# Generate the complete .gdbinit file
generate_gdbinit() {
	local kernel_file="$1"
	local programs_output breakpoints_output

	# Capture outputs from functions
	programs_output=$(find_programs_with_breakpoints)
	breakpoints_output=$(find_breakpoint_locations)

	# Write entire .gdbinit in one operation
	cat >.gdbinit <<EOL
# Load the kernel as the main file (gives the base symbols)
file ${kernel_file}

define add-symbol-file-auto
  shell readelf -WS \$arg0 | awk '/\.text/ {printf "set \$text_address=0x%s\n", \$5; exit}' > /tmp/gdb_addr.cmd
  source /tmp/gdb_addr.cmd
  add-symbol-file \$arg0 \$text_address
  shell rm -f /tmp/gdb_addr.cmd
end

# Talk to QEMU
target remote localhost:1234

set print pretty on
set trace-commands on
set logging enabled on

EOL

	# Add symbol loading commands
	if [[ -n "$programs_output" ]]; then
		while IFS= read -r elf; do
			echo "add-symbol-file-auto $elf"
		done <<<"$programs_output" >>.gdbinit
	fi

	# Add breakpoints
	if [[ -n "$breakpoints_output" ]]; then
		echo >>.gdbinit # Add blank line
		echo "$breakpoints_output" >>.gdbinit
	fi

	# Add final commands
	cat >>.gdbinit <<EOF

continue

define hook-quit
    set confirm off
end
EOF
}

# Main execution
main() {
	local kernel_file

	kernel_file=$(find_kernel)
	echo "Generating .gdbinit for kernel: $kernel_file"

	generate_gdbinit "$kernel_file"

	echo "Wrote .gdbinit"
}

main "$@"
