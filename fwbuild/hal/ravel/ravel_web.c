/* SPDX-License-Identifier: GPL-3.0-only */
/* FM1 DECK HAL adapter for Ravel (upstream/ravel/web/emu/ravel_web.c + ravel_app.c). */
#include <stdint.h>
#include <string.h>

#define web_playing rv_emu_playing
#include "../../web/emu/ravel_web.c"
#undef web_playing

#undef NB
#undef NE
#undef B_FX
#undef B_PLAY
#undef B_OCTUP
#undef EN_SELECT
#undef EN_K4
#define FS 44100u
#define NB 14
#define NE 7
#define B_FX 0
#define B_PLAY 10
#define B_OCTUP 13
#define EN_SELECT 0
#define EN_K4 6

static const char *const B_NAME[NB] = {
    "FX", "SCL", "ENV", "LFO", "EDIT", "GLO", "HOME", "SAVE", "ARP", "SEQ", "PLAY", "REC", "OCT-", "OCT+"
};
/* FM1_B_* -> ravel B_OCTDN=0, B_OCTUP=1, B_FX=2..B_REC=13 */
static const uint8_t FM1_TO_RV_BTN[14] = { 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 0, 1 };
/* FM1_E_* -> ravel EN_SELECT=0, EN_ALGO=1, EN_K1=2..EN_K4=5, EN_PRESET=6 */
static const uint8_t FM1_TO_RV_ENC[7]  = { 0, 1, 6, 2, 3, 4, 5 };

static uint8_t nor[0x100000];
static uint16_t web_fb[240 * 240];
static float out_l[1024], out_r[1024];
static float ring_l[1024], ring_r[1024];
static uint32_t ring_w, ring_r_, rv_draws, rv_seen_lcd, rv_booted;
#include "../common/store_nor.h"
#define RV_MAGIC 0x54535652u
static uint32_t rv_last_saved_writes = 0xFFFFFFFFu;

static int32_t rv_pack_nor(void)
{
    return fd_pack_slots(
        nor, sizeof(nor), RV_MAGIC, rv_store_slots(),
        rv_store, rv_store_len, rv_store_writes(), &rv_last_saved_writes
    );
}

static uint8_t *web_nor(void) { return nor; }
static uint32_t web_flash_writes_count(void) { return rv_store_writes(); }
static void web_nor_erase(void) { memset(nor, 0xFF, sizeof(nor)); }

static void web_boot(void)
{
    if (rv_booted) return;
    fd_unpack_slots(nor, sizeof(nor), RV_MAGIC, rv_store_slots(), rv_store_max(), rv_store, rv_store_set_len);
    rv_boot();
    rv_last_saved_writes = rv_store_writes();
    ring_w = ring_r_ = 0;
    rv_booted = 1;
}

static void web_render(uint32_t n)
{
    if (n > 1024u) n = 1024u;
    if (!rv_booted) {
        memset(out_l, 0, n * sizeof(float));
        memset(out_r, 0, n * sizeof(float));
        return;
    }
    while (ring_w - ring_r_ < n) {
        uint32_t got = rv_run(4u);
        const float *pcm = rv_audio();
        for (uint32_t i = 0; i < got; i++) {
            uint32_t idx = (ring_w++) & 1023u;
            ring_l[idx] = pcm[2u * i];
            ring_r[idx] = pcm[2u * i + 1u];
        }
    }
    for (uint32_t i = 0; i < n; i++) {
        uint32_t idx = (ring_r_++) & 1023u;
        out_l[i] = ring_l[idx];
        out_r[i] = ring_r[idx];
    }
}

static float *web_out_l(void) { return out_l; }
static float *web_out_r(void) { return out_r; }

static void web_buttons(uint32_t mask)
{
    uint32_t m = 0;
    for (int i = 0; i < 14; i++)
        if ((mask >> i) & 1u) m |= 1u << FM1_TO_RV_BTN[i];
    rv_set_buttons(m);
}
static void web_keys(uint32_t mask) { rv_set_keys(mask & 0x7FFFFFFu); }
static void web_enc(uint32_t role, int32_t steps)
{
    if (role < 7u) rv_enc(FM1_TO_RV_ENC[role], steps);
}
static void web_master(uint32_t adc)
{
    if (adc > 1023u) adc = 1023u;
    rv_set_master((adc * 4096u + 511u) / 1023u);
}
static uint32_t rv_web_midi(uint32_t status, uint32_t d1, uint32_t d2)
{
    web_midi((int)status, (int)d1, (int)d2);
    return 1u;
}
#define web_midi rv_web_midi

static uint32_t web_screen_draws(void)
{
    uint32_t changed = 0;
    for (uint32_t i = 0; i < 240u * 240u; i++) {
        uint16_t sw = (uint16_t)((fb[i] >> 8) | (fb[i] << 8));
        changed |= (uint32_t)(web_fb[i] ^ sw);
        web_fb[i] = sw;
    }
    if (changed) rv_draws++;
    return rv_draws;
}
static const uint16_t *web_screen(void) { return web_fb; }
static uint32_t web_lit_buttons(void)
{
    uint32_t raw = rv_leds_buttons(), m = 0;
    for (int i = 0; i < 14; i++)
        if ((raw >> FM1_TO_RV_BTN[i]) & 1u) m |= 1u << i;
    return m;
}
static uint32_t web_lit_keys(void) { return rv_leds_keys(); }
static uint32_t web_dim_buttons(void) { return 0u; }
static uint32_t web_dim_keys(void) { return 0u; }
static uint32_t web_breath_buttons(void) { return 0u; }
static uint32_t web_breath_keys(void) { return 0u; }
static uint32_t web_mid_keys(void) { return 0u; }
static uint32_t web_dim_level(void) { return 0u; }
static const uint8_t *web_anim_levels(void) { return 0; }
static uint32_t web_playing(void) { return (uint32_t)rv_emu_playing(); }
static uint32_t web_now_ms(void) { return now_ms; }
