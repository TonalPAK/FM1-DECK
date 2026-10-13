#pragma once

/* FM1 DECK firmware module ABI (native backend).
 *
 * A firmware module is one FM-1 alternative firmware (Felucca, JIANT, SLOOP, ...) built for the phone from its own
 * sources around its browser-emulator HAL (web/emu/felucca_web.c or our fwbuild/hal/<id>/<id>_web.c) plus our glue
 * (fwbuild/glue/fm1deck_glue.h). It is a shared library libfw_<id>.so exporting one symbol, FM1DECK_FW_ENTRY,
 * that returns a pointer to a static Fm1FirmwareApi table. FM1 DECK loads it with dlopen (FirmwareHost).
 *
 * Contract (see docs/FIRMWARE-ABI.md):
 *   - Plain C, no allocation across the boundary, no callbacks into the host, no threads, no I/O, no logging.
 *   - Single-threaded: exactly one thread calls into a module at a time (the audio thread while the slot is live,
 *     the control thread while it is parked). Flash (nor) changes only inside calls.
 *   - One instance per dlopen: the firmware's state is the module's static memory. A fresh power-on is
 *     dlclose + dlopen.
 *   - Optional entries may be NULL; a non-NULL optional entry is always paired with its FM1_CAP_* bit in caps.
 *   - struct_size lets later minor revisions append fields: the host reads a field only when
 *     offsetof(field) + sizeof(field) <= struct_size. Moving or changing the meaning of a field bumps FM1DECK_FW_ABI;
 *     FM1 DECK refuses modules of another ABI.
 *
 * The WASM backend (upstream felucca.wasm / our *.wasm, see docs/FIRMWARE-ABI.md §WASM) does not use this table;
 * it is wrapped into the same C++ interface (IFirmware) by WasmFirmware.
 *
 * SPDX-License-Identifier: GPL-3.0-only */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FM1DECK_FW_ABI 1u
#define FM1DECK_FW_ENTRY "fm1deck_firmware_api"

/* Buttons by printed label: the bit index into buttons(). Order = the firmware's panel.c B_* order. */
enum {
    FM1_B_FX, FM1_B_SCL, FM1_B_ENV, FM1_B_LFO, FM1_B_EDIT, FM1_B_GLO, FM1_B_HOME, FM1_B_SAVE,
    FM1_B_ARP, FM1_B_SEQ, FM1_B_PLAY, FM1_B_REC, FM1_B_OCTDN, FM1_B_OCTUP, FM1_BUTTON_COUNT
};

/* Encoders by printed label: the role in enc(). MASTER is a pot, see master(). */
enum { FM1_E_SELECT, FM1_E_ALGO, FM1_E_PRESET, FM1_E_K1, FM1_E_K2, FM1_E_K3, FM1_E_K4, FM1_ENCODER_COUNT };

/* Keys: bit k = key k from F3 (MIDI 53) up to G5 (MIDI 79), chromatic. */
#define FM1_KEY_COUNT 27u
#define FM1_KEY_LOWEST_NOTE 53

/* leds() indexes. Buttons masks use FM1_B_* bits (+ bit 14: PLAY's green LED), keys masks use key bits. */
enum {
    FM1_LED_LIT_BUTTONS, FM1_LED_LIT_KEYS, FM1_LED_DIM_BUTTONS, FM1_LED_DIM_KEYS,
    FM1_LED_BREATH_BUTTONS, FM1_LED_BREATH_KEYS, FM1_LED_MID_KEYS, FM1_LED_DIM_LEVEL, FM1_LED_COUNT
};
#define FM1_LED_PLAY_GREEN_BIT 14u

/* anim_levels(): FM1_ANIM_LEVELS bytes, 0..13 buttons by label, 14..40 keys F3..G5;
 * value 0..64 = level /64 of lit, | 0x80 = the dim glow under it. NULL while the power-on sweep does not run. */
#define FM1_ANIM_LEVELS 41u

/* Capability bits (caps). */
#define FM1_CAP_HOST_CLOCK  (1u << 0)  /* host_clock(): the firmware follows MIDI clock F8/FA/FB/FC sent via midi() */
#define FM1_CAP_SAVE_NOW    (1u << 1)  /* save_now(): write RESTORE LAST / autosave at once */
#define FM1_CAP_UI_STATE    (1u << 2)  /* ui_state(): layer, page title and knob labels from the firmware's tables */
#define FM1_CAP_SEED        (1u << 3)  /* seed(): session random seed before boot (JIANT 0.4+) */
#define FM1_CAP_ANIM        (1u << 4)  /* anim_levels() is real (not a stub returning NULL forever) */
#define FM1_CAP_MID_KEYS    (1u << 5)  /* FM1_LED_MID_KEYS is meaningful (drum grid beats) */
#define FM1_CAP_TEST_HEAVY  (1u << 6)  /* test_heavy(): load the upstream benchmark song (perf gates) */
#define FM1_CAP_MIDI_REALTIME (1u << 7)/* midi() accepts real-time bytes F8 FA FB FC (implied by HOST_CLOCK) */

/* Layer ids normalised across firmwares (ui_state().layer). Profiles map their LAYER_* / LY_* onto these. */
enum {
    FM1_LAYER_NONE, FM1_LAYER_FX, FM1_LAYER_GLO, FM1_LAYER_SCL, FM1_LAYER_EDIT, FM1_LAYER_SEQ, FM1_LAYER_REC,
    FM1_LAYER_SAVE, FM1_LAYER_MACRO, FM1_LAYER_ARP, FM1_LAYER_LFO, FM1_LAYER_MENU, FM1_LAYER_OTHER = 31
};

/* ui_state() output. Strings are NUL-terminated ASCII, truncated to fit. */
typedef struct Fm1UiState {
    uint32_t layer;            /* FM1_LAYER_* */
    uint32_t layer_raw;        /* the firmware's own layer value (profile-specific, for overlay rules) */
    uint32_t page;             /* the firmware's page index (profile-specific) */
    uint32_t flags;            /* bit 0: HOME page shown, bit 1: menu open, bit 2: dialog open, bit 3: layer latched */
    char page_title[16];       /* PAGES[page].title, e.g. "DLY 2" */
    char knob[4][12];          /* labels of KNOB 1..4 on this page (param_desc_t.label; "" = empty slot) */
    int16_t knob_value[4];     /* the parameter's current value (page_desc's *valp), valid when bit k of knob_valid */
    int16_t knob_min[4];       /* param_desc_t.min / .max: lets the UI draw a value ring around the encoder */
    int16_t knob_max[4];
    uint32_t knob_valid;       /* bit k: knob k has label + value + range */
} Fm1UiState;

typedef struct Fm1FirmwareApi {
    uint32_t abi;                       /* FM1DECK_FW_ABI */
    uint32_t struct_size;               /* sizeof(Fm1FirmwareApi) as the module was built */
    uint32_t caps;                      /* FM1_CAP_* */
    const char *id;                     /* stable lowercase id: "felucca", "jiant", "sloop" */
    const char *name;                   /* display name: "FELUCCA", "JIANT FM", "SLOOP" */
    const char *version;                /* the firmware's own version string (FELUCCA_VERSION) */
    const char *build_info;             /* "upstream=<sha12> fwbuild=<sha12> ndk=<rev> date=<iso>" */
    uint32_t sample_rate;               /* the firmware's FS: 44100 (fixed-point constants are baked for it) */
    uint32_t screen_w, screen_h;        /* 240 x 240 */
    uint32_t nor_size;                  /* the emulated SPI NOR, bytes (0x100000) */
    const char *const *button_names;    /* FM1_BUTTON_COUNT printed labels (SLOOP: "SEL" for SCL) */

    /* flash: the host erases it and copies a saved image in before boot() */
    uint8_t *(*nor)(void);
    uint32_t (*nor_writes)(void);       /* erases + programs since power-on */
    void (*nor_erase)(void);

    void (*seed)(uint32_t seed);        /* optional (FM1_CAP_SEED): before boot() */
    void (*boot)(void);                 /* power on, once per load */
    /* n frames (1..1024) of the firmware's stereo output at sample_rate; the device clock advances to match */
    void (*render)(uint32_t n, float *left, float *right);

    /* the panel, by printed labels */
    void (*buttons)(uint32_t mask);     /* FM1_B_* bits currently held */
    void (*keys)(uint32_t mask);        /* key bits currently held */
    void (*enc)(uint32_t role, int32_t steps);   /* + = clockwise */
    void (*master)(uint32_t adc);       /* MASTER pot as its ADC reads it, 0..1023 */
    /* a MIDI message as USB-MIDI brings it; channel messages always, real-time bytes with FM1_CAP_MIDI_REALTIME.
     * returns 0 when the firmware's ring was full */
    uint32_t (*midi)(uint32_t status, uint32_t d1, uint32_t d2);
    void (*host_clock)(uint32_t on);    /* optional (FM1_CAP_HOST_CLOCK) */
    int32_t (*save_now)(void);          /* optional (FM1_CAP_SAVE_NOW): 1 written, 0 unchanged, -1 failed */

    /* outputs */
    const uint16_t *(*screen)(void);    /* RGB565, high byte first (the panel's byte order), screen_w * screen_h */
    uint32_t (*screen_draws)(void);     /* increments on every draw: compare to detect a new frame */
    void (*leds)(uint32_t out[FM1_LED_COUNT]);
    const uint8_t *(*anim_levels)(void);/* FM1_ANIM_LEVELS bytes or NULL (see above) */
    uint32_t (*playing)(void);
    uint32_t (*now_ms)(void);           /* the device's millisecond clock */

    void (*ui_state)(Fm1UiState *out);  /* optional (FM1_CAP_UI_STATE) */
    void (*test_heavy)(void);           /* optional (FM1_CAP_TEST_HEAVY) */
} Fm1FirmwareApi;

typedef const Fm1FirmwareApi *(*Fm1FirmwareEntry)(void);

#ifdef __cplusplus
}
#endif
