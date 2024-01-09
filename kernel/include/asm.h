// I know this isn't techincally standard but im tired of writing guards
#pragma once
#include <stdint.h>

static inline void outb(uint16_t port, uint8_t val);
