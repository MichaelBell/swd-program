#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "stdio.h"
#include "swd_load.hpp"
//#include "pico-stick.h"
#include "blink.h"
#include <algorithm>

//uint watchdog_data[4] = {0xb007c0d3, 0x6ff83f2c, 0x20042000, 0x20000001};

#if CORE1_LOGIC_ANALYSE
uint data[64] = {0};

void core1_entry() {
    uint* data_ptr = data;
    uint i = 0;

    while (gpio_get(2) == 0);
    while (gpio_get(2) == 1);
    while (gpio_get(2) == 0);
    while (true) {
        while (gpio_get(2) == 1);
        if (gpio_get(3)) *data_ptr |= 1 << i;
        if (++i == 32) {
            i = 0;
            ++data_ptr;
        }
        while (gpio_get(2) == 0);
    }
}
#endif

int main() {
    stdio_init_all();

    sleep_ms(100);

    swd_load_program(section_addresses, section_data, section_data_len, sizeof(section_addresses) / sizeof(section_addresses[0]));

#ifdef CORE1_LOGIC_ANALYSE
    for (int i = 0; i < 64; ++i) {
        printf("%08x ", data[i]);
        if ((i & 7) == 7) printf("\n");
    }
#endif

    while(1);
}
