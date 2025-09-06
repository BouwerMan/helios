#!/usr/bin/env bash
# Generates a .gdbinit file for debugging Helios with GDB.
# Looks for comments containing "GDB BREAKPOINT" in source files to set breakpoints.

# Truncate .gdbinit file if it exists, or create it
true >.gdbinit

# Find the kernel file (assuming only one exists)
kernel_file=$(find helios -name "*.kernel" | head -n 1)
# user_file=sysroot/usr/bin/hello_world.elf

echo "Generating .gdbinit for kernel: $kernel_file"

# Write initial GDB configuration to .gdbinit
cat >.gdbinit <<EOL
file $kernel_file

define add-symbol-file-auto
  # Parse .text address to temp file
  shell echo set \\\$text_address=\$(readelf -WS \$arg0 | grep .text | awk '{ print "0x"\$5 }') >/tmp/temp_gdb_text_address.txt

  # Source .text address
  source /tmp/temp_gdb_text_address.txt

  #  Clean tempfile
  shell rm -f /tmp/temp_gdb_text_address.txt

  # Load symbol table
  add-symbol-file \$arg0 \$text_address
end

#add-symbol-file-auto sysroot/usr/bin/init.elf
add-symbol-file-auto sysroot/usr/bin/hsh.elf

target remote localhost:1234
set print pretty on
set trace-commands on
set logging enabled on
EOL

# Find all relevant source files in one command
source_files=$(find helios libc userspace -type f \( -name "*.c" -o -name "*.h" -o -name "*.asm" \))

# Add breakpoints for each file containing "GDB BREAKPOINT"
for file in $source_files; do
	sed -n '/GDB BREAKPOINT/=' "$file" | while read -r line; do
		echo "break $file:$((line + 1))" >>.gdbinit
	done
done

# Append remaining GDB commands
cat >>.gdbinit <<EOL

continue

define hook-quit
    set confirm off
end
EOL
