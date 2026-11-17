/* SPDX-License-Identifier: GPL-3.0-only */
/* FM1 DECK profile: SHRUB (Xfer Records, decompiled from Shrub.vst3 / shrub.wasm). */
#define FM1DECK_FW_ID "shrub"
#define FM1DECK_FW_NAME "SHRUB"
#define FM1DECK_GLUE_HOST_CLOCK 0
#define FM1DECK_GLUE_SAVE_NOW 1
#define FM1DECK_GLUE_UI_STATE 1
#define FM1DECK_GLUE_SEED 1
#define FM1DECK_GLUE_TEST_HEAVY 0
#define FM1DECK_CUSTOM_SAVE_NOW 1

static int32_t fd_save_now(void) { return shrub_save_now(); }

static void fd_shrub_copy(char *dst, const char *src, uint32_t cap)
{
    uint32_t i = 0;
    if (src) {
        for (; i + 1u < cap && src[i]; i++)
            dst[i] = src[i];
    }
    dst[i] = 0;
}

static void fd_ui_state(Fm1UiState *o)
{
    memset(o, 0, sizeof *o);
    const ShrubGroupDef *grp = &SHRUB_GROUPS[shrub_cur_group];
    uint32_t sub = shrub_cur_sub[shrub_cur_group] % grp->sub_count;
    const ShrubPageDef *pg = &grp->subs[sub];

    o->layer = grp->layer_id;
    o->layer_raw = shrub_cur_group;
    o->page = shrub_cur_group * 4u + sub;
    o->flags = (shrub_cur_group == 0u && sub == 0u) ? 1u : 0u;
    fd_shrub_copy(o->page_title, pg->title, (uint32_t)sizeof(o->page_title));

    for (uint32_t k = 0; k < 4u; k++) {
        uint32_t p = pg->params[k];
        const char *lbl = wasm_cstr(sw_label(p));
        fd_shrub_copy(o->knob[k], lbl, (uint32_t)sizeof(o->knob[k]));
        uint32_t v = sw_value(p);
        if (v > 65535u) v = 65535u;
        o->knob_min[k] = 0;
        o->knob_max[k] = 1000;
        o->knob_value[k] = (int16_t)((v * 1000u) / 65535u);
        o->knob_valid |= (1u << k);
    }
}
