file ./HeliOS/build/HeliOS.kernel
target remote localhost:1234

set print pretty on

break isr_common_stub
break isr_handler

# Inspect page tables
#x /8wg &page_tables_start
