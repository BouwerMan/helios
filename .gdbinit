file ./helios/build/HeliOS.kernel
target remote localhost:1234

set print pretty on

# break scheduler.c:134
# break pmm.c:79
c

define hook-quit
    set confirm off
end

# Inspect page tables
#x /8wg &page_tables_start
