file ./HeliOS/build/HeliOS.kernel
target remote localhost:1234

set print pretty on

break switch.asm:7
break scheduler.c:18
c

# Inspect page tables
#x /8wg &page_tables_start
