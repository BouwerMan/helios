file ./HeliOS/build/HeliOS.kernel
target remote localhost:1234

set print pretty on

break strtok.c:17
c

# Inspect page tables
#x /8wg &page_tables_start
