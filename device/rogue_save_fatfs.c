/*
 * ThumbyRogue — save backend, FatFs (ThumbyOne slot mode).
 *
 * Same API as rogue_save_flash.c (rogue_plat_save / rogue_plat_load), but the
 * in-progress run blob lives in the shared FAT volume at
 *   /thumbyrogue/run.sav
 * instead of a dedicated flash sector, so it survives reflashing the slot and
 * is visible over the lobby's USB MSC. The volume is mounted once by the slot
 * device main (thumbyone_fs_mount_or_format) before the game starts.
 */
#include <stdint.h>
#include "ff.h"

#define ROGUE_SAVE_DIR  "/thumbyrogue"
#define ROGUE_SAVE_PATH ROGUE_SAVE_DIR "/run.sav"

int rogue_plat_save(const uint8_t *data, int len) {
    if (len <= 0) return 0;
    f_mkdir(ROGUE_SAVE_DIR);   /* idempotent — FR_EXIST is fine */
    FIL fp;
    if (f_open(&fp, ROGUE_SAVE_PATH, FA_WRITE | FA_CREATE_ALWAYS) != FR_OK) return 0;
    UINT bw = 0;
    f_write(&fp, data, (UINT)len, &bw);
    f_close(&fp);
    return (bw == (UINT)len) ? len : 0;
}

int rogue_plat_load(uint8_t *data, int max) {
    if (max <= 0) return 0;
    FIL fp;
    if (f_open(&fp, ROGUE_SAVE_PATH, FA_READ) != FR_OK) return 0;
    UINT br = 0;
    f_read(&fp, data, (UINT)max, &br);
    f_close(&fp);
    return (int)br;
}
