/* SPDX-License-Identifier: GPL-3.0-only */
/* FM1 DECK HAL adapter for fm1-base firmwares (amb, blop, choppah, echofield, frasil,
 * jamtrack, loafbreaker, loom, mutable, atek303). */
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#ifndef FM1BASE_EMU_WEB_C
#define FM1BASE_EMU_WEB_C "emu_web.c"
#endif

#define web_boot   fm1b_emu_boot
#define web_master fm1b_emu_master
#define web_fb     fm1b_emu_fb
#include FM1BASE_EMU_WEB_C
#undef web_boot
#undef web_master
#undef web_fb

#define FS 44100u
static const char *const B_NAME[NB] = {
    "FX", "SEL", "ENV", "LFO", "EDIT", "GLO", "HOME", "SAVE", "ARP", "SEQ", "PLAY", "REC", "OCT-", "OCT+"
};

static uint8_t nor[0x100000];
#define web_fb fb

#include "../common/store_nor.h"
#define FBST_MAGIC 0x54534246u
static uint32_t fm1b_last_saved_writes = 0xFFFFFFFFu;

static void fm1b_unpack_nor(void)
{
    fd_unpack_slots(nor, sizeof(nor), FBST_MAGIC, web_nobj(), web_store_max(), web_store, web_store_set_len);
}

static int32_t fm1b_pack_nor(void)
{
    return fd_pack_slots(
        nor, sizeof(nor), FBST_MAGIC, web_nobj(),
        web_store, web_store_len, web_store_writes(), &fm1b_last_saved_writes
    );
}

static uint8_t *web_nor(void) { return nor; }
static uint32_t web_flash_writes_count(void) { return web_store_writes(); }
static void web_nor_erase(void) { memset(nor, 0xFF, sizeof(nor)); }

static void web_boot(void)
{
    fm1b_unpack_nor();
    fm1b_emu_boot();
    fm1b_last_saved_writes = web_store_writes();
}

static void web_master(uint32_t adc)
{
    if (adc > 1023u) adc = 1023u;
    fm1b_emu_master((adc * 4096u + 511u) / 1023u);
}

static uint32_t web_midi(uint32_t status, uint32_t d1, uint32_t d2)
{
    if (status >= 0xF8u) {
        web_midi_in(PORT_USB, 0x0Fu | (status << 8));
        return 1u;
    }
    if (status >= 0x80u && status < 0xF0u) {
        uint32_t pkt = ((status >> 4) & 0xFu) | (status << 8) | ((d1 & 0x7Fu) << 16) | ((d2 & 0x7Fu) << 24);
        web_midi_in(PORT_USB, pkt);
    }
    return 1u;
}

static const uint16_t *web_screen(void) { return fb; }
static uint32_t web_screen_draws(void) { return web_blits(); }
static uint32_t web_breath_buttons(void) { return 0u; }
static uint32_t web_breath_keys(void) { return 0u; }
static uint32_t web_mid_keys(void) { return mid_keys; }
static uint32_t web_dim_level(void)
{
#ifdef APP_DIM_LEVELS
    return dim_pulse();
#elif defined(FM1BASE_NO_LIGHTS_GLOW)
    return glow_on ? 1600u : 0u;
#else
    return lights_glow == 1 ? 1600u : lights_glow ? 3200u : 0u;
#endif
}
static const uint8_t *web_anim_levels(void) { return 0; }
static uint32_t web_playing(void) { return 0u; }
static uint32_t web_now_ms(void) { return web_now(); }
