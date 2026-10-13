/* SPDX-License-Identifier: GPL-3.0-only
 * FM1 DECK firmware glue: exposes a firmware's browser-emulator HAL (upstream web/emu/felucca_web.c, or our
 * fwbuild/hal/<id>/<id>_web.c) as the Fm1FirmwareApi table (app/src/main/cpp/fw/Fm1FirmwareApi.h).
 *
 * Included by fwbuild/glue/fm1deck_module.c AFTER the HAL and the profile header, in the same translation unit:
 * nothing of the firmware or of the HAL is changed, only called. Derived from the glue of GGBOX (an earlier port of these firmwares by
 * the same author, 2026-10-11), reworked so that the core uses only the emulator's public web_* exports, which are the
 * stable surface shared by Felucca, JIANT and the SLOOP HAL. Everything that reaches into firmware internals is an
 * optional capability switched by the profile (FM1DECK_GLUE_*), so that a firmware update that renames an internal
 * still builds: CI retries with the capability off and records that in the pack manifest.
 *
 * Required before inclusion (profile header, fwbuild/profiles/<id>.h):
 *   FM1DECK_FW_ID, FM1DECK_FW_NAME                      strings
 *   FM1DECK_GLUE_HOST_CLOCK, FM1DECK_GLUE_SAVE_NOW,
 *   FM1DECK_GLUE_UI_STATE, FM1DECK_GLUE_SEED,
 *   FM1DECK_GLUE_TEST_HEAVY                             0 / 1
 *   FM1DECK_CLOCK_PARAM                                 the global that picks the clock (G_CLOCK; SLOOP: G_SYNC)
 *   with FM1DECK_GLUE_SAVE_NOW and FM1DECK_CUSTOM_SAVE_NOW: the profile defines fd_save_now()
 *   with FM1DECK_GLUE_UI_STATE: the profile defines fd_ui_state(Fm1UiState *)
 * Optional: FM1DECK_BUILD_INFO (set by CMake). */
#include "Fm1FirmwareApi.h"

#include <string.h>

#ifndef FM1DECK_FW_ID
#error "FM1DECK_FW_ID must be defined by the profile"
#endif
#ifndef FM1DECK_BUILD_INFO
#define FM1DECK_BUILD_INFO "local"
#endif

_Static_assert(NB == FM1_BUTTON_COUNT, "button labels differ from Fm1FirmwareApi");
_Static_assert(NE == FM1_ENCODER_COUNT, "encoder roles differ from Fm1FirmwareApi");
_Static_assert(B_PLAY == FM1_B_PLAY && B_OCTUP == FM1_B_OCTUP && B_FX == FM1_B_FX, "button order differs");
_Static_assert(EN_K4 == FM1_E_K4 && EN_SELECT == FM1_E_SELECT, "encoder order differs");
_Static_assert(sizeof web_fb == 240u * 240u * sizeof(uint16_t), "screen is not 240 x 240");

/* ------------------------------------------------------------------ core: web_* exports only --- */
static uint8_t *fd_nor(void) { return web_nor(); }
static uint32_t fd_nor_writes(void) { return web_flash_writes_count(); }
static void fd_nor_erase(void) { web_nor_erase(); }
static void fd_boot(void) { web_boot(); }

#if FM1DECK_GLUE_HOST_CLOCK
static uint32_t fd_host_clock_on;
#endif

static void fd_render(uint32_t n, float *left, float *right)
{
    if (n > 1024u)
        n = 1024u;
#if FM1DECK_GLUE_HOST_CLOCK
    if (fd_host_clock_on && song.g[FM1DECK_CLOCK_PARAM] != 1)
        song.g[FM1DECK_CLOCK_PARAM] = 1;              /* CLK held at USB while the host drives the clock */
#endif
    web_render(n);                                    /* runs the device until n frames exist */
    memcpy(left, web_out_l(), n * sizeof(float));
    memcpy(right, web_out_r(), n * sizeof(float));
}

static void fd_buttons(uint32_t mask) { web_buttons(mask & ((1u << NB) - 1u)); }
static void fd_keys(uint32_t mask) { web_keys(mask); }
static void fd_enc(uint32_t role, int32_t steps) { web_enc(role, steps); }
static void fd_master(uint32_t adc) { web_master(adc); }

static uint32_t fd_midi(uint32_t status, uint32_t d1, uint32_t d2)
{
#if FM1DECK_GLUE_HOST_CLOCK
    if (status >= 0xF8u)                              /* real time: a CIN 0xF USB-MIDI packet, status in byte 1 */
        return (uint32_t)midi_enqueue(0xFu | status << 8, 1u);
#endif
    return web_midi(status, d1, d2);                  /* channel messages only; others are ignored (returns 1) */
}

static const uint16_t *fd_screen(void) { return web_screen(); }
static uint32_t fd_screen_draws(void) { return web_screen_draws(); }

static void fd_leds(uint32_t out[FM1_LED_COUNT])
{
    out[FM1_LED_LIT_BUTTONS] = web_lit_buttons();
    out[FM1_LED_LIT_KEYS] = web_lit_keys();
    out[FM1_LED_DIM_BUTTONS] = web_dim_buttons();
    out[FM1_LED_DIM_KEYS] = web_dim_keys();
    out[FM1_LED_BREATH_BUTTONS] = web_breath_buttons();
    out[FM1_LED_BREATH_KEYS] = web_breath_keys();
    out[FM1_LED_MID_KEYS] = web_mid_keys();
    out[FM1_LED_DIM_LEVEL] = web_dim_level();
}

static const uint8_t *fd_anim_levels(void) { return web_anim_levels(); }
static uint32_t fd_playing(void) { return web_playing(); }
static uint32_t fd_now_ms(void) { return web_now_ms(); }

/* ------------------------------------------------------------------ optional capabilities --- */
#if FM1DECK_GLUE_HOST_CLOCK
static void fd_host_clock(uint32_t on)
{
    fd_host_clock_on = on != 0u;
    if (!fd_host_clock_on && song.g[FM1DECK_CLOCK_PARAM] == 1)
        song.g[FM1DECK_CLOCK_PARAM] = 0;              /* back to INT */
}
#endif

#if FM1DECK_GLUE_SAVE_NOW && !defined(FM1DECK_CUSTOM_SAVE_NOW)
/* Felucca / JIANT: project.c autosave_poll's write, now (what RESTORE LAST puts back at the next boot) */
static int32_t fd_save_now(void)
{
#if FELUCCA_FLASH
    uint32_t sig = autosave_sig();
    if (!flash_ok)
        return -1;
    if (sig == as.saved && as.wrote)
        return 0;
    as.last = fm1_ms;
    as.wrote = 1;
    project_capture(&proj_scratch);
    proj_wire_gen++;
    as.err = !proj_pack(&proj_wire, &proj_scratch) || st_save(OBJ_AUTOSAVE, &proj_wire, sizeof proj_wire) != 0;
    if (as.err)
        return -1;
    as.saved = as.seen = sig;
    as.writes++;
    return 1;
#else
    return -1;
#endif
}
#endif

#if FM1DECK_GLUE_SEED
static void fd_seed(uint32_t s) { web_seed(s); }
#endif

#if FM1DECK_GLUE_TEST_HEAVY
static void fd_test_heavy(void) { web_test_heavy(); }  /* Felucca / JIANT HALs only (SLOOP HAL has none) */
#endif

/* ------------------------------------------------------------------ the table --- */
#define FD_CAPS ( (FM1DECK_GLUE_HOST_CLOCK ? (FM1_CAP_HOST_CLOCK | FM1_CAP_MIDI_REALTIME) : 0u) \
                | (FM1DECK_GLUE_SAVE_NOW ? FM1_CAP_SAVE_NOW : 0u) \
                | (FM1DECK_GLUE_UI_STATE ? FM1_CAP_UI_STATE : 0u) \
                | (FM1DECK_GLUE_SEED ? FM1_CAP_SEED : 0u) \
                | FM1_CAP_ANIM | FM1_CAP_MID_KEYS \
                | (FM1DECK_GLUE_TEST_HEAVY ? FM1_CAP_TEST_HEAVY : 0u) )

static const Fm1FirmwareApi fd_api = {
    FM1DECK_FW_ABI,
    (uint32_t)sizeof(Fm1FirmwareApi),
    FD_CAPS,
    FM1DECK_FW_ID,
    FM1DECK_FW_NAME,
    FELUCCA_VERSION,
    FM1DECK_BUILD_INFO,
    FS,
    240u, 240u,
    (uint32_t)sizeof nor,
    B_NAME,
    fd_nor, fd_nor_writes, fd_nor_erase,
#if FM1DECK_GLUE_SEED
    fd_seed,
#else
    0,
#endif
    fd_boot, fd_render,
    fd_buttons, fd_keys, fd_enc, fd_master,
    fd_midi,
#if FM1DECK_GLUE_HOST_CLOCK
    fd_host_clock,
#else
    0,
#endif
#if FM1DECK_GLUE_SAVE_NOW
    fd_save_now,
#else
    0,
#endif
    fd_screen, fd_screen_draws, fd_leds, fd_anim_levels,
    fd_playing, fd_now_ms,
#if FM1DECK_GLUE_UI_STATE
    fd_ui_state,
#else
    0,
#endif
#if FM1DECK_GLUE_TEST_HEAVY
    fd_test_heavy,
#else
    0,
#endif
};

__attribute__((visibility("default"))) const Fm1FirmwareApi *fm1deck_firmware_api(void)
{
    return &fd_api;
}
