/* SPDX-License-Identifier: GPL-3.0-only
 * ui_state() for the Felucca family (Felucca, JIANT): ui.c's `ui` (page, layer, home) and params.c's PAGES[] /
 * page_desc(), all in the same translation unit as the HAL. Verified against Felucca 1.5.1 and JIANT 0.6.41
 * (params.c page_t {title, fam, scope, graph, id[4]}; page_desc(pg, slot, &valp); param_desc_t.label/min/max).
 * A firmware update that changes these names fails to compile here: CI then rebuilds with
 * FM1DECK_GLUE_UI_STATE=0 and the pack loses FM1_CAP_UI_STATE (hints fall back to held-button rules). */

static uint32_t fd_layer_map(uint32_t l)
{
    switch (l) {
    case LAYER_NONE: return FM1_LAYER_NONE;
    case LAYER_FX: return FM1_LAYER_FX;
    case LAYER_GLO: return FM1_LAYER_GLO;
    case LAYER_SCL: return FM1_LAYER_SCL;
    case LAYER_EDIT: return FM1_LAYER_EDIT;
    case LAYER_SEQ: return FM1_LAYER_SEQ;
    case LAYER_REC: return FM1_LAYER_REC;
#if FD_HAS_LAYER_SAVE_MACRO
    case LAYER_SAVE: return FM1_LAYER_SAVE;
    case LAYER_MACRO: return FM1_LAYER_MACRO;
#endif
    default: return FM1_LAYER_OTHER;
    }
}

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
    o->layer = fd_layer_map(ui.layer);
    o->page = ui.page;
    o->flags = ui.home ? 1u : 0u;
    if (ui.page >= sizeof PAGES / sizeof PAGES[0])
        return;
    fd_copy(o->page_title, PAGES[ui.page].title, sizeof o->page_title);
    for (k = 0; k < 4u; k++) {
        int16_t *v = 0;
        const param_desc_t *d = page_desc(&PAGES[ui.page], k, &v);
        if (!d)
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
