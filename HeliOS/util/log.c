#include <drivers/serial.h>
#include <util/log.h>

void log_output(const char* msg)
{
    printf("%s", msg); // Console / screen output
#if ENABLE_SERIAL_LOGGING
    write_serial_string(msg); // Custom serial output
#endif
}
