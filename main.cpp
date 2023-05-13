#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "stdio.h"
#include "swd.pio.h"
#include "blink.h"
#include <algorithm>

uint watchdog_data[4] = {0xb007c0d3, 0x6ff83f2c, 0x20042000, 0x20000001};

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

static uint pio_offset;
static uint pio_sm;
static const pio_program_t* pio_prog;

void wait_for_idle() {
    uint pull_offset = (pio_prog == &swd_raw_write_program) ? 2 : 
                       (pio_prog == &swd_raw_read_program) ? 0 : 5;
    while (!pio_sm_is_tx_fifo_empty(pio0, pio_sm) || pio0_hw->sm[pio_sm].addr != pio_offset + pull_offset);
}

void switch_program(bool read, bool raw = false) {
    wait_for_idle();
    pio_sm_set_enabled(pio0, pio_sm, false);
    pio_remove_program(pio0, pio_prog, pio_offset);
    pio_prog = raw ? (read ? &swd_raw_read_program : &swd_raw_write_program) :
                     (read ? &swd_read_program : &swd_write_program);
    pio_offset = pio_add_program(pio0, pio_prog);
    if (raw) {
        swd_raw_program_init(pio0, pio_sm, pio_offset, 2, 3, read);
    } else {
        swd_program_init(pio0, pio_sm, pio_offset, 2, 3, read);
        wait_for_idle();
        pio0_hw->irq = 1;
    }
}

bool write_cmd(uint cmd, uint data) {
    if (pio_prog != &swd_write_program) {
        switch_program(false);
    }
    pio_sm_put_blocking(pio0, pio_sm, cmd);
    pio_sm_put_blocking(pio0, pio_sm, data);
    wait_for_idle();
    if (pio0_hw->irq & 0x1) {
        return false;
    }
    return true;
}

bool write_block(uint addr, const uint* data, uint len_in_words) {
    if (!write_cmd(0x0B, addr)) return false;
    for (uint i = 0; i < len_in_words; ++i) {
        if (!write_cmd(0x3B, *data++)) return false;
    }
    return true;
}

bool write_reg(uint addr, uint data) {
    return write_block(addr, &data, 1);
}

bool read_cmd(uint cmd, uint& data) {
    if (pio_prog != &swd_read_program) {
        switch_program(true);
    }
    pio_sm_put_blocking(pio0, pio_sm, cmd);
    wait_for_idle();
    if (pio0_hw->irq & 0x1) {
        printf("Read ID failed\n");
        return false;
    }
    data = pio_sm_get_blocking(pio0, pio_sm);
    return true;
}

bool read_reg(uint addr, uint &data) {
    if (!write_cmd(0x0B, addr)) return false;
    if (!read_cmd(0x1F, data)) return false;
    if (!read_cmd(0x3D, data)) return false;
    return true;
}

void idle() {
    switch_program(false, true);
    pio_sm_put_blocking(pio0, pio_sm, 7);
    pio_sm_put_blocking(pio0, pio_sm, 0);
}

bool connect(bool first = true) {
    if (first) {
        pio_prog = &swd_raw_write_program;
        pio_offset = pio_add_program(pio0, &swd_raw_write_program);
        pio_sm = pio_claim_unused_sm(pio0, true);

        swd_initial_init(pio0, pio_sm, 2, 3);

        swd_raw_program_init(pio0, pio_sm, pio_offset, 2, 3, false);
    } else {
        switch_program(false, true);
    }

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

    printf("Target Select\n"); // Must ignore error response
    wait_for_idle();
    pio_sm_set_enabled(pio0, pio_sm, false);
    pio_remove_program(pio0, pio_prog, pio_offset);
    pio_prog = &swd_write_ignore_error_program;
    pio_offset = pio_add_program(pio0, pio_prog);
    swd_program_init(pio0, pio_sm, pio_offset, 2, 3, false);
    wait_for_idle();
    pio0_hw->irq = 1;
    pio_sm_put_blocking(pio0, pio_sm, 0x19);
    pio_sm_put_blocking(pio0, pio_sm, 0x01002927);

    printf("Read ID\n");
    uint id;
    if (!read_cmd(0x25, id)) {
        printf("Read ID failed\n");
        return false;
    }
    printf("Received ID: %08x\n", id);

    if (id != 0x0bc12477) return false;

    printf("Abort\n");
    if (!write_cmd(0x01, 0x1E)) {
        printf("Abort failed\n");
        return false;
    }

    printf("Select\n");
    if (!write_cmd(0x31, 0)) {
        printf("Select failed\n");
        return false;
    }

    printf("Ctrl/Stat\n");
    if (!write_cmd(0x29, 0x50000001)) {
        printf("Ctrl power up failed\n");
        return false;
    }

    uint status;
    if (!read_cmd(0x0D, status)) {
        printf("Read status on power up failed\n");
        return false;
    }
    printf("Status: %08x\n", status);
    if ((status & 0xA0000000) != 0xA0000000) {
        printf("Power up not acknowledged\n");
        return false;
    }

    printf("Setup memory access\n");
    if (!write_cmd(0x23, 0xA2000052)) {
        printf("Memory access setup failed\n");
        return false;
    }

    printf("Halt CPU\n");
    if (!write_reg(0xe000edf0, 0xA05F0003)) {
        printf("Halt failed\n");
    }

    idle();

#if 0
    uint mem_value;
    if (!read_reg(0x20000000, mem_value)) {
        printf("Read reg failed\n");
    }
    printf("Mem: %08x\n", mem_value);
    if (!write_reg(0x20000000, 0x12345678)) {
        printf("Write reg failed\n");
    }
    idle();
    if (!read_reg(0x20000000, mem_value)) {
        printf("Read reg failed\n");
    }
    printf("Mem: %08x\n", mem_value);

    if (!read_reg(0x4000c000, mem_value)) {
        printf("Read reg failed\n");
    }
    printf("Resets: %08x\n", mem_value);
    if (!write_reg(0x4000f000, 0x120)) {
        printf("Write reg failed\n");
    }
    idle();
    if (!read_reg(0x4000c000, mem_value)) {
        printf("Read reg failed\n");
    }
    printf("Resets: %08x\n", mem_value);


    if (!read_reg(0x400140cc, mem_value)) {
        printf("Read reg failed\n");
    }
    printf("Mem: %08x\n", mem_value);
    idle();
    if (!write_reg(0x400140cc, 0x331F)) {
        printf("Write reg failed\n");
    }
    idle();
    if (!read_reg(0x400140cc, mem_value)) {
        printf("Read reg failed\n");
    }
    printf("Mem: %08x\n", mem_value);
    if (!write_reg(0x4001c068, 0x56)) {
        printf("Write reg failed\n");
    }
    if (!read_reg(0x4001c068, mem_value)) {
        printf("Read reg failed\n");
    }
    printf("Mem: %08x\n", mem_value);
#endif

    return true;
}

bool load(uint address, const uint* data, uint len_in_bytes) {
    printf("Loading %d bytes at %08x\n", len_in_bytes, address);
    idle();

    printf("Halt CPU\n");
    if (!write_reg(0xe000edf0, 0xA05F0003)) {
        printf("Halt failed\n");
        return false;
    }

    idle();

    constexpr uint BLOCK_SIZE = 1024;
    for (int i = 0; i < len_in_bytes; i += BLOCK_SIZE) {
        uint block_len_in_words = std::min(BLOCK_SIZE >> 2, (len_in_bytes - i) >> 2);
        if (!write_block(address + i, &data[i >> 2], block_len_in_words)) {
            printf("Block write failed\n");
            return false;
        }
    }

    for (int j = 0; j < len_in_bytes; j += 4) {
        uint check_data;
        if (!read_reg(address + j, check_data)) {
            printf("Read failed\n");
            return false;
        }
        if (check_data != data[j >> 2]) {
            printf("Verify failed at %08x, %08x != %08x\n", address + j, check_data, data[j >> 2]);
            return false;
        }
    } 

    idle();

    return true;
}

bool start(uint pc = 0x20000001) {
    idle();

    printf("Setup watchdog\n");
    if (!write_block(0x4005801c, watchdog_data, 4)) {
        printf("Setup watchdog failed\n");
        return false;
    }

    printf("Set PC\n");
    if (!write_reg(0xe000edf8, pc) ||
        !write_reg(0xe000edf4, 0x1000F))
    {
        printf("Failed to set PC\n");
        return false;
    }
    printf("Set SP\n");
    if (!write_reg(0xe000edf8, 0x20042000) ||
        !write_reg(0xe000edf4, 0x1000D))
    {
        printf("Failed to set PC\n");
        return false;
    }
    idle();

    uint data;
    write_reg(0xe000edf4, 0x0000F);
    idle();
    read_reg(0xe000edf8, data);
    printf("Set PC to %08x\n", data);

    for (int i = 0; i < 16; ++i) {
        write_reg(0xe000edf4, i);
        idle();
        read_reg(0xe000edf8, data);
        printf("R%d is %08x\n", i, data);
    }

    for (int i = 0; i < 4; ++i) {
        read_reg(0x4005801c + 4*i, data);
        printf("WD%d is %08x\n", i, data);
    }

    //write_reg(0x40010008, 0xC000);

    printf("Start CPU\n");
    if (!write_reg(0xe000edf0, 0xA05F0001)) {
        printf("Start failed\n");
        return false;
    }

    idle();

    //write_reg(0x40058000, 0x80000000);
    //write_reg(0xe000edf0, 0xA05F0000);

    //connect(false);
#if 0
    for (int j = 0; j < 10; ++j) {
        sleep_ms(1);
        for (int i = 0; i < 16; ++i) {
            write_reg(0xe000edf4, i);
            idle();
            read_reg(0xe000edf8, data);
            printf("R%d is %08x\n", i, data);
        }
        idle();
        write_reg(0xe000edf0, 0xA05F000D);
    }
#endif
    
#if 0
    sleep_ms(1000);
    idle();
    printf("Halt CPU\n");
    if (!write_reg(0xe000edf0, 0xA05F0003)) {
        printf("Halt failed\n");
        return false;
    }
    write_reg(0xe000edf4, 0x0000F);
    idle();
    read_reg(0xe000edf8, data);
    printf("PC is %08x\n", data);

    for (int i = 0; i < 16; ++i) {
        write_reg(0xe000edf4, i);
        idle();
        read_reg(0xe000edf8, data);
        printf("R%d is %08x\n", i, data);
    }
    for (int i = 0; i < 4; ++i) {
        read_reg(0x4005801c + 4*i, data);
        printf("WD%d is %08x\n", i, data);
    }
#endif
    return true;
}

int main() {
    stdio_init_all();

    gpio_init(2);
    gpio_init(3);
    gpio_disable_pulls(2);
    gpio_pull_up(3);

#ifdef CORE1_LOGIC_ANALYSE
    multicore_launch_core1(core1_entry);
#endif

    sleep_ms(4000);

    printf("Connecting\n");

    bool ok = connect();

    printf("Connected %s\n", ok ? "OK" : "Fail");

    if (ok) {
        ok = load(elf_data0_addr, elf_data0, sizeof(elf_data0));
    }

    if (ok) {
        ok = load(elf_data1_addr, elf_data1, sizeof(elf_data1));
    }

    if (ok) {
        start();
    }

#ifdef CORE1_LOGIC_ANALYSE
    for (int i = 0; i < 64; ++i) {
        printf("%08x ", data[i]);
        if ((i & 7) == 7) printf("\n");
    }
#endif

    while(1);
}
