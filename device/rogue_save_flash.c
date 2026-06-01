/*
 * ThumbyRogue device save backend — persists the run to the last flash sector.
 *
 * Flash writes must run with XIP disabled, so core1 (which spins/renders from
 * XIP flash) is parked via multicore lockout for the duration. core1 calls
 * multicore_lockout_victim_init() at startup (see rogue_device_main.c).
 */
#include <string.h>
#include "pico/multicore.h"
#include "hardware/flash.h"
#include "hardware/sync.h"

#ifndef PICO_FLASH_SIZE_BYTES
#define PICO_FLASH_SIZE_BYTES (2 * 1024 * 1024)
#endif
/* Two sectors (8 KB) — the full mid-level suspend blob (enemies + ground
 * loot + inventory) is larger than one 4 KB sector. */
#define SAVE_BYTES (2u * FLASH_SECTOR_SIZE)
#define SAVE_OFF   (PICO_FLASH_SIZE_BYTES - SAVE_BYTES)

int rogue_plat_save(const uint8_t *data, int len) {
    static uint8_t buf[SAVE_BYTES];
    memset(buf, 0xFF, sizeof buf);
    int c = (len > (int)SAVE_BYTES) ? (int)SAVE_BYTES : len;
    memcpy(buf, data, (size_t)c);

    multicore_lockout_start_blocking();         /* park core1 in RAM */
    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(SAVE_OFF, SAVE_BYTES);
    flash_range_program(SAVE_OFF, buf, SAVE_BYTES);
    restore_interrupts(ints);
    multicore_lockout_end_blocking();
    return 1;
}

int rogue_plat_load(uint8_t *data, int max) {
    const uint8_t *src = (const uint8_t *)(XIP_BASE + SAVE_OFF);
    int n = (max < (int)SAVE_BYTES) ? max : (int)SAVE_BYTES;
    memcpy(data, src, (size_t)n);             /* caller validates the magic */
    return n;
}
