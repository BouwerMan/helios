file ./helios/build/HeliOS.kernel
target remote localhost:1234

set print pretty on

# break switch.asm:73
break prune_page_table_recursive
# break bootmem_free_all
c

define hook-quit
    set confirm off
end

# Inspect page tables
#x /8wg &page_tables_start
