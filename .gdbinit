file ./HeliOS/build/HeliOS.kernel
target remote localhost:1234

set print pretty on

break screen.c:111

# Inspect page tables
#x /8wg &page_tables_start
