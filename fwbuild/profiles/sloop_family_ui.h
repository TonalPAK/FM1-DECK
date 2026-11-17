/* SPDX-License-Identifier: GPL-3.0-only */
/* SLOOP / CLIPPER page metadata from the same parameter tables as the firmware display. */
static void fd_ui_copy(char *dst, const char *src, size_t n)
{
    size_t i = 0;
    if (src)
        for (; i + 1 < n && src[i]; ++i) dst[i] = src[i];
    dst[i] = 0;
}

static void fd_ui_knob(Fm1UiState *o, uint32_t k, const char *label,
                       int16_t value, int16_t min, int16_t max)
{
    fd_ui_copy(o->knob[k], label, sizeof o->knob[k]);
    if (max <= min) return;
    o->knob_value[k] = value;
    o->knob_min[k] = min;
    o->knob_max[k] = max;
    o->knob_valid |= 1u << k;
}

static void fd_ui_state(Fm1UiState *o)
{
    static const uint8_t layer_map[LY_COUNT] = {
        [LY_PLAY] = FM1_LAYER_NONE, [LY_FX] = FM1_LAYER_FX, [LY_ERASE] = FM1_LAYER_EDIT,
        [LY_ROLL] = FM1_LAYER_ARP, [LY_STEP] = FM1_LAYER_SEQ, [LY_SCALE] = FM1_LAYER_SCL,
        [LY_MIX] = FM1_LAYER_GLO, [LY_SONG] = FM1_LAYER_SAVE
    };
    memset(o, 0, sizeof *o);
    o->layer_raw = ui.layer;
    o->layer = ui.layer < LY_COUNT ? layer_map[ui.layer] : FM1_LAYER_OTHER;
    o->page = ui.page;
    o->flags = (ui.home ? 1u : 0u) | (ui.menu ? 2u : 0u) | (ui.confirm ? 4u : 0u);
    if (ui.page >= NPAGES || ui.menu || ui.confirm) return;
    const page_t *pg = &PAGES[ui.page];
    fd_ui_copy(o->page_title, ui.home ? "HOME" : pg->title, sizeof o->page_title);
    if (!ui.home && is_drum(TSEL) && !page_for_drum(pg)) return;
    if (!ui.home && pg->scope == SC_TRK) {
        fd_ui_knob(o, 0, "TRACK", song.sel + 1, 1, NTRK);
        fd_ui_knob(o, 1, "LEVEL", trk_level(song.sel), 0, 127);
        fd_ui_knob(o, 2, "LEN", TSEL->p[P_SLEN], TP[P_SLEN].min, TP[P_SLEN].max);
        fd_ui_knob(o, 3, "PAN", TSEL->p[P_PAN], TP[P_PAN].min, TP[P_PAN].max);
        return;
    }
    if (!ui.home) {
        const char *const step[4] = {"NOTE", "PITCH", "LEN", "FORCE"};
        const char *const browse[4] = {"No.", "ENG", "", "FAV"};
        const char *const user[4] = {"SLOT", "LOAD", "ERASE", "SAVE"};
        const char *const *labels = pg->scope == SC_STEP ? step :
            pg->graph == GR_BROWSE ? browse : pg->graph == GR_USER ? user : NULL;
        if (labels) {
            for (uint32_t k = 0; k < 4u; ++k)
                fd_ui_copy(o->knob[k], labels[k], sizeof o->knob[k]);
            return;
        }
    }
    for (uint32_t k = 0; k < 4u; ++k) {
        int16_t *v = 0;
        if (!ui.home && pg->graph == GR_SLOTS && k == 1u) {
            fd_ui_copy(o->knob[k], "ERASE", sizeof o->knob[k]);
            continue;
        }
        const param_desc_t *d = ui.home ? home_param(k, &v) : page_desc(pg, k, &v);
        if (!d || !d->label || d->label[0] == '-') continue;
        fd_ui_knob(o, k, d->label, v ? *v : 0, d->min, v ? d->max : d->min);
    }
}
