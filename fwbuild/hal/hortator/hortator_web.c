/* SPDX-License-Identifier: GPL-3.0-only
 * Hortator in a host process: the upstream's own web simulator core (tests/sim_core.c, which #includes
 * tests/sim_host.h = the firmware's sources in felucca.c's order with the HAL replaced, the first-visit demos and
 * the features lab) behind the web_* surface that fwbuild/glue/fm1deck_glue.h calls, as Felucca's
 * web/emu/felucca_web.c offers it. Hortator is by DEADACTIVE, GPL-3.0-only, based on Felucca by Leo Kuroshita
 * (Hügelton Instruments). Nothing of the firmware or of its simulator changes for this file.
 * Differences from felucca_web.c: the simulator's clock is its audio sample count (sim_render runs the audio ISR
 * once per half and the UI frame every 15 ms of audio); the LEDs are on / off only (no dim, breath or idle
 * animation in this firmware); the screen is the simulator's native RGB565, swapped here to the panel's byte order;
 * a fresh flash gets the simulator's four demo projects, as the web page's first visit does.
 * The includes resolve through -I<upstream>/hortator/firmware/src (fwbuild/CMakeLists.txt): ../../tests/ = the
 * upstream's tests/. sim_host.h needs sim_keymap.h in the generated headers (tools/fwgen/fwgen.py). */
#include <stdint.h>
#include <stddef.h>
#include <string.h>                      /* before sim_host.h blanks __attribute__: the NDK's fortify headers need it */

#include "../../tests/sim_core.c"

#undef __attribute__                     /* sim_host.h blanks it for the firmware's sources */

static uint32_t web_booted;

/* ------------------------------------------------------------ the flash --- */
#define nor hflash                       /* the glue's sizeof nor */
static uint32_t web_flash_writes;
static uint8_t *web_nor(void) { return hflash; }
static void web_nor_erase(void)
{
    memset(hflash, 0xFF, sizeof hflash);
    sim_flash_clean();
}
/* erases + programs so far: the simulator marks written sectors dirty, a poll that finds any counts one write */
static uint32_t web_flash_writes_count(void)
{
    uint32_t i, any = 0;
    for (i = 0; i < sizeof sim_dirty / sizeof sim_dirty[0]; i++)
        any |= sim_dirty[i];
    if (any) {
        web_flash_writes++;
        sim_flash_clean();
    }
    return web_flash_writes;
}

static int web_flash_blank(void)
{
    uint32_t i;
    for (i = 0; i < sizeof hflash; i++)
        if (hflash[i] != 0xFFu)
            return 0;
    return 1;
}

/* the host erases the flash, copies the saved image in, then boots: a blank flash is a first visit */
static void web_boot(void)
{
    if (web_booted)
        return;
    sim_init(web_flash_blank());
    web_booted = 1;
}

/* ------------------------------------------------------------ audio --- */
static float out_l[SIM_MAX_FRAMES], out_r[SIM_MAX_FRAMES];
static uint32_t web_ui_frames;
static float *web_out_l(void) { return out_l; }
static float *web_out_r(void) { return out_r; }
static void web_render(uint32_t n)       /* n frames (<= 1024): the device runs until they exist */
{
    uint32_t k;
    if (n > SIM_MAX_FRAMES)
        n = SIM_MAX_FRAMES;
    if (!web_booted) {
        memset(out_l, 0, n * sizeof out_l[0]);
        memset(out_r, 0, n * sizeof out_r[0]);
        return;
    }
    web_ui_frames += sim_render(n);
    for (k = 0; k < n; k++) {
        out_l[k] = sim_out[2u * k];
        out_r[k] = sim_out[2u * k + 1u];
    }
}

/* ------------------------------------------------------------ input --- */
/* by the printed labels: buttons bit b = B_FX .. B_OCTUP (panel.c), keys bit k = key k (F3 .. G5) */
static uint32_t web_btn_mask, web_key_mask;
static void web_buttons(uint32_t labels)
{
    uint32_t b, d;
    if (!web_booted)
        return;
    d = labels ^ web_btn_mask;
    for (b = 0; b < NB; b++)
        if ((d >> b) & 1u)
            sim_btn(b, (int)((labels >> b) & 1u));
    web_btn_mask = labels;
}
static void web_keys(uint32_t mask)
{
    uint32_t k, d;
    if (!web_booted)
        return;
    mask &= (1u << 27) - 1u;
    d = mask ^ web_key_mask;
    for (k = 0; k < 27u; k++)
        if ((d >> k) & 1u)
            sim_key(k, (int)((mask >> k) & 1u));
    web_key_mask = mask;
}
static void web_enc(uint32_t role, int32_t n) { sim_enc(role, n); }
static void web_master(uint32_t v) { sim_master(v); }
/* a channel message, as a USB-MIDI event packet arrives (usb_app.c midi_in_event filters and queues it) */
static uint32_t web_midi(uint32_t status, uint32_t d1, uint32_t d2)
{
    if (status < 0x80u || status >= 0xF0u)
        return 1;
    midi_in_event((status >> 4) | status << 8 | (d1 & 127u) << 16 | (d2 & 127u) << 24);
    return 1;
}

/* ------------------------------------------------------------ outputs --- */
static uint32_t web_lit_buttons(void) { return sim_leds(); }
static uint32_t web_lit_keys(void) { return sim_key_leds(); }
static uint32_t web_dim_buttons(void) { return 0; }
static uint32_t web_dim_keys(void) { return 0; }
static uint32_t web_dim_level(void) { return 0; }
static uint32_t web_breath_buttons(void) { return 0; }
static uint32_t web_breath_keys(void) { return 0; }
static uint32_t web_mid_keys(void) { return 0; }
static uint8_t *web_anim_levels(void) { return 0; }

/* the screen in the panel's byte order (high byte first), as felucca_web.c keeps it; a UI frame that changed a
 * pixel counts as a draw */
static uint16_t web_fb[240 * 240];
static uint32_t web_draws, web_seen_frames;
static uint32_t web_screen_draws(void)
{
    uint32_t i, changed = 0;
    if (web_ui_frames == web_seen_frames)
        return web_draws;
    web_seen_frames = web_ui_frames;
    for (i = 0; i < 240u * 240u; i++) {
        uint16_t sw = (uint16_t)((fb[i] >> 8) | (fb[i] << 8));
        changed |= (uint32_t)(web_fb[i] ^ sw);
        web_fb[i] = sw;
    }
    if (changed)
        web_draws++;
    return web_draws;
}
static uint16_t *web_screen(void) { return web_fb; }
static uint32_t web_now_ms(void) { return fm1_ms; }
static uint32_t web_playing(void) { return sim_playing(); }
