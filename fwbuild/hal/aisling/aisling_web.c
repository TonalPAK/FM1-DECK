/* SPDX-License-Identifier: GPL-3.0-only */
/* FM1 DECK HAL adapter for AISLING (upstream/aisling/web/emu/amb_web.c). */
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#define web_boot   ais_emu_boot
#define web_master ais_emu_master
#define web_fb     ais_emu_fb
#define web_midi   ais_emu_midi_out
#include "../../web/emu/amb_web.c"
#undef web_boot
#undef web_master
#undef web_fb
#undef web_midi

#define FS 44100u
static const char *const B_NAME[NB] = {
    "FX", "SEL", "ENV", "LFO", "EDIT", "GLO", "HOME", "SAVE", "ARP", "SEQ", "PLAY", "REC", "OCT-", "OCT+"
};
static uint8_t nor[0x100000];
#define web_fb fb
#include "../common/store_nor.h"
#define AIS_MAGIC 0x54534246u
static uint32_t ais_last_saved_writes = 0xFFFFFFFFu;

static int32_t ais_pack_nor(void)
{
    return fd_pack_slots(
        nor, sizeof(nor), AIS_MAGIC, web_nobj(),
        web_store, web_store_len, web_store_writes(), &ais_last_saved_writes
    );
}

static uint8_t *web_nor(void) { return nor; }
static uint32_t web_flash_writes_count(void) { return web_store_writes(); }
static void web_nor_erase(void) { memset(nor, 0xFF, sizeof(nor)); }
static void web_boot(void)
{
    fd_unpack_slots(nor, sizeof(nor), AIS_MAGIC, web_nobj(), web_store_max(), web_store, web_store_set_len);
    ais_emu_boot();
    ais_last_saved_writes = web_store_writes();
}
static void web_master(uint32_t adc)
{
    if (adc > 1023u) adc = 1023u;
    ais_emu_master((adc * 4096u + 511u) / 1023u);
}
static uint32_t web_midi(uint32_t status, uint32_t d1, uint32_t d2)
{
    uint32_t cin = status >= 0xF0u ? 0x0Fu : ((status >> 4) & 0xFu);
    if (status >= 0x80u && (mi_w - mi_r < MQ))
        min_q[mi_w++ % MQ] = cin | ((status & 0xFFu) << 8) | ((d1 & 0x7Fu) << 16) | ((d2 & 0x7Fu) << 24);
    return 1u;
}
static const uint16_t *web_screen(void) { return fb; }
static uint32_t web_screen_draws(void) { return web_blits(); }
static uint32_t web_dim_buttons(void) { return 0u; }
static uint32_t web_dim_keys(void) { return 0u; }
static uint32_t web_breath_buttons(void) { return 0u; }
static uint32_t web_breath_keys(void) { return 0u; }
static uint32_t web_mid_keys(void) { return 0u; }
static uint32_t web_dim_level(void) { return 0u; }
static const uint8_t *web_anim_levels(void) { return 0; }
static uint32_t web_playing(void) { return 0u; }
static uint32_t web_now_ms(void) { return web_now(); }
