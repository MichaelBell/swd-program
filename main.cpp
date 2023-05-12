#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "stdio.h"
#include "swd.pio.h"

uint data[32] = {0};

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

static uint pio_offset;
static uint pio_sm;
static const pio_program_t* pio_prog;

void wait_for_idle(uint pull_offset = 5) {
    while (!pio_sm_is_tx_fifo_empty(pio0, pio_sm) || pio0_hw->sm[pio_sm].addr != pio_offset + pull_offset);
}

void switch_program(bool read, uint pull_offset = 5) {
    wait_for_idle(pull_offset);    
    pio_sm_set_enabled(pio0, pio_sm, false);
    pio_remove_program(pio0, pio_prog, pio_offset);
    pio_prog = read ? &swd_read_program : &swd_write_program;
    pio_offset = pio_add_program(pio0, pio_prog);
    swd_program_init(pio0, pio_sm, pio_offset, 2, 3, read);
    wait_for_idle();
    pio0_hw->irq = 1;
}

bool connect() {
    pio_prog = &swd_raw_write_program;
    pio_offset = pio_add_program(pio0, &swd_raw_write_program);
    pio_sm = pio_claim_unused_sm(pio0, true);

    swd_initial_init(pio0, pio_sm, 2, 3);

    swd_raw_program_init(pio0, pio_sm, pio_offset, 2, 3, false);

    // Begin transaction: 8 clocks, data low
    printf("Begin transaction\n");
    pio_sm_put_blocking(pio0, pio_sm, 7);
    pio_sm_put_blocking(pio0, pio_sm, 0);

    // Go to SWD mode:
    // 8 clocks, data high
    // 0x6209F392, 0x86852D95, 0xE3DDAFE9, 0x19BC0EA2
    // 4 clocks, data low
    // 0x1A
    printf("SWD Mode\n");
    pio_sm_put_blocking(pio0, pio_sm, 8-1);
    pio_sm_put_blocking(pio0, pio_sm, 0xFF);

    printf("Tag\n");
    pio_sm_put_blocking(pio0, pio_sm, 32*4+4+8-1);
    pio_sm_put_blocking(pio0, pio_sm, 0x6209F392);
    pio_sm_put_blocking(pio0, pio_sm, 0x86852D95);
    pio_sm_put_blocking(pio0, pio_sm, 0xE3DDAFE9);
    pio_sm_put_blocking(pio0, pio_sm, 0x19BC0EA2);
    pio_sm_put_blocking(pio0, pio_sm, 0x1A0);

    // Line Reset: 50 high, 8 low
    printf("Line Reset\n");
    pio_sm_put_blocking(pio0, pio_sm, 58-1);
    pio_sm_put_blocking(pio0, pio_sm, 0xFFFFFFFF);
    pio_sm_put_blocking(pio0, pio_sm, 0x003FFFF);

    // Switch to normal SWD command programming
    switch_program(true, 2);

    printf("Read ID\n");

    pio_sm_put_blocking(pio0, pio_sm, 0x25);
    wait_for_idle();
    if (pio0_hw->irq & 0x1) {
        printf("Read ID failed\n");
        return false;
    }
    uint id = pio_sm_get_blocking(pio0, pio_sm);
    printf("Received ID: %08x\n", id);

    if (id != 0x0BC12477) return false;

    switch_program(false);

    printf("Abort\n");
    pio_sm_put_blocking(pio0, pio_sm, 0x01);
    pio_sm_put_blocking(pio0, pio_sm, 0x1E);
    wait_for_idle();
    if (pio0_hw->irq & 0x1) {
        printf("Abort failed\n");
        return false;
    }

    return true;
}

int main() {
    stdio_init_all();

    gpio_init(2);
    gpio_init(3);
    gpio_disable_pulls(2);
    gpio_pull_up(3);

    multicore_launch_core1(core1_entry);

    sleep_ms(4000);

    printf("Starting\n");

    bool ok = connect();

    printf("Connect %s\n", ok ? "OK" : "Fail");

    for (int i = 0; i < 32; ++i) {
        printf("%08x ", data[i]);
        if ((i & 7) == 7) printf("\n");
    }

    while(1);
}

#if 0
    printf("IRQ0: %d\n", pio0_hw->irq & 0x1);
    pio0_hw->irq = 1;
    printf("IRQ0: %d\n", pio0_hw->irq & 0x1);
#endif