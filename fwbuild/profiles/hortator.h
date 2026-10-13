/* SPDX-License-Identifier: GPL-3.0-only
 * FM1 DECK profile: HORTATOR (https://github.com/DeadActive/hortator, by DEADACTIVE), a drum-machine firmware based
 * on Felucca. HAL = fwbuild/hal/hortator/hortator_web.c around the upstream's own web simulator (tests/sim_core.c).
 * G_CLOCK picks the clock as in Felucca (INT / USB / TRS = the MIDI input ring's source). Projects live in four
 * slots (A-D) the user saves; there is no autosave to flush, so SAVE_NOW is off. Hortator has no Felucca LAYER_*:
 * ui_state reports PERFORM (FX held) as the FX layer, the menu as a flag, and the page's knobs from PAGES[] /
 * page_desc() (pages.c), as felucca_family_ui.h does; on HOME the knobs are ui.c home_param's. */
#define FM1DECK_FW_ID "hortator"
#define FM1DECK_FW_NAME "HORTATOR"
#define FM1DECK_CLOCK_PARAM G_CLOCK
#ifndef FM1DECK_GLUE_HOST_CLOCK
#define FM1DECK_GLUE_HOST_CLOCK 1
#endif
#ifndef FM1DECK_GLUE_SAVE_NOW
#define FM1DECK_GLUE_SAVE_NOW 0                 /* no autosave in this firmware: projects are saved to slots */
#endif
#ifndef FM1DECK_GLUE_UI_STATE
#define FM1DECK_GLUE_UI_STATE 1
#endif
#ifndef FM1DECK_GLUE_SEED
#define FM1DECK_GLUE_SEED 0
#endif
#ifndef FM1DECK_GLUE_TEST_HEAVY
#define FM1DECK_GLUE_TEST_HEAVY 0               /* the simulator has no benchmark song */
#endif

#if FM1DECK_GLUE_UI_STATE
static void fd_copy(char *dst, const char *src, uint32_t cap)
{
    uint32_t i = 0;
    if (src)
        for (; i + 1u < cap && src[i]; i++)
            dst[i] = src[i];
    dst[i] = 0;
}

static void fd_ui_state(Fm1UiState *o)
{
    uint32_t k;
    memset(o, 0, sizeof *o);
    o->layer_raw = ui.layer;
    o->layer = ui.layer ? FM1_LAYER_FX : FM1_LAYER_NONE;
    o->page = ui.page;
    o->flags = (ui.home ? 1u : 0u) | (ui.menu ? 2u : 0u);
    if (ui.page >= NPAGES)
        return;
    fd_copy(o->page_title, ui.home ? "HOME" : PAGES[ui.page].title, sizeof o->page_title);
    for (k = 0; k < 4u; k++) {
        int16_t *v = 0;
        /* HOME: the selected track's first four sound knobs (ui.c home_param), as ui_draw.c draws them */
        const param_desc_t *d = ui.home ? home_param(k, &v) : page_desc(&PAGES[ui.page], k, &v);
        if (!d || !d->label || d->label[0] == '-')   /* "-": the model has no knob there */
            continue;
        fd_copy(o->knob[k], d->label, sizeof o->knob[k]);
        if (v) {
            o->knob_value[k] = *v;
            o->knob_min[k] = d->min;
            o->knob_max[k] = d->max;
            o->knob_valid |= 1u << k;
        }
    }
}
#endif
