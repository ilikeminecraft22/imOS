#pragma once

#define PIT_COMMAND 0x43
#define PIT_CHANNEL0 0x40
#define PIT_FREQUENCY 1193182

#define Hz(x) (x)
#define kHz(x) (x*1000)
#define MHz(x) (x*1000000)

#define MS(x) (x)
#define SEC(x) (x*1000)

void pit_init(uint32_t frequency);

void sleep(uint64_t ticks);
uint64_t get_uptime();