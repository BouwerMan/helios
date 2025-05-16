# Script to write isr.asm without being suicidal
# Only writes the repetative parts, might make it do all the comments and stuff
# in the future if I use it enough.

of = open("isr.asm", "w")

of.write("; A bunch of Interrupt Service Routines (ISRs)\n")
of.write("; For info on each ISR see isr.md in Documentation\n")
# Writes all the global tags
for i in range(32):
    of.write(f"global isr{i}\n")

of.write("\n; ISR definitions\n")
# Writes the actual ISR
eisr = [8, 10, 11, 12, 13, 14]
for i in range(32):
    # Manually do 0 and 8 so I can add extra comments
    if i == 0:
        of.write("\n; Divide by 0\n")
        of.write("    cli ; Disable interrupts\n")
        of.write("    push byte 0 ; push a dummy error code\n")
        of.write("    push byte 0 ; push the isr code\n")
        of.write("    jmp_isr_common_stub\n")
        continue
    if i == 8:
        of.write("\n; Double Fault Exception\n")
        of.write("    cli ; Disable interrupts\n")
        of.write(
            "    ; We don't push a dummy error code since this interrupt comes with one\n"
        )
        of.write("    push byte 0 ; push the isr code\n")
        of.write("    jmp_isr_common_stub\n")
        continue
    of.write("\n")
    of.write(f"isr{i}:\n")
    of.write("    cli\n")
    if i not in eisr:
        of.write("    push byte 0\n")
    of.write(f"    push byte {i}\n")
    of.write("    jmp isr_common_stub\n")

# Does not create isr_common_stub

of.close()
