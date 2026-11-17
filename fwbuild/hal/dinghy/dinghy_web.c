/* SPDX-License-Identifier: GPL-3.0-only */
/* FM1 DECK HAL adapter for Dinghy-family firmwares (OberVave, S1Vave, Shine On).
 * Runs app_main() on a 64 KiB static coroutine stack with a lock-free register swap
 * at platform_poll(), replacing wasm-opt Asyncify for native Android .so modules. */
#include <stdint.h>
#include <stddef.h>
#include <string.h>

#define OBV_HOST 1
#define OBV_WASM 1
#ifndef SHN_HOST
#define SHN_HOST 1
#endif
#ifndef SHN_WASM
#define SHN_WASM 1
#endif
#ifndef S1V_HOST
#define S1V_HOST 1
#endif
#ifndef S1V_WASM
#define S1V_WASM 1
#endif

#define printf(...) ((void)0)
static char *getenv(const char *n) { (void)n; return 0; }

#ifndef DINGHY_APP_C
#error "DINGHY_APP_C must be defined by the profile"
#endif

#include DINGHY_APP_C

#undef __attribute__

/* ---- lock-free, allocation-free userspace coroutine for platform_poll() ---- */
static uint8_t co_stack[65536] __attribute__((aligned(16)));
static void *co_main_sp;
static void *co_app_sp;
static uint32_t co_booted;

#if defined(__aarch64__)
__attribute__((noinline, naked)) static void co_swap(void **from_sp, void *to_sp)
{
    __asm__ volatile(
        "sub sp, sp, #160\n\t"
        "stp x19, x20, [sp, #0]\n\t"
        "stp x21, x22, [sp, #16]\n\t"
        "stp x23, x24, [sp, #32]\n\t"
        "stp x25, x26, [sp, #48]\n\t"
        "stp x27, x28, [sp, #64]\n\t"
        "stp x29, x30, [sp, #80]\n\t"
        "stp d8,  d9,  [sp, #96]\n\t"
        "stp d10, d11, [sp, #112]\n\t"
        "stp d12, d13, [sp, #128]\n\t"
        "stp d14, d15, [sp, #144]\n\t"
        "mov x2, sp\n\t"
        "str x2, [x0]\n\t"
        "mov sp, x1\n\t"
        "ldp x19, x20, [sp, #0]\n\t"
        "ldp x21, x22, [sp, #16]\n\t"
        "ldp x23, x24, [sp, #32]\n\t"
        "ldp x25, x26, [sp, #48]\n\t"
        "ldp x27, x28, [sp, #64]\n\t"
        "ldp x29, x30, [sp, #80]\n\t"
        "ldp d8,  d9,  [sp, #96]\n\t"
        "ldp d10, d11, [sp, #112]\n\t"
        "ldp d12, d13, [sp, #128]\n\t"
        "ldp d14, d15, [sp, #144]\n\t"
        "add sp, sp, #160\n\t"
        "ret\n\t"
    );
}

static void co_trampoline(void)
{
    app_main();
    for (;;) co_swap(&co_app_sp, co_main_sp);
}

static void co_init_stack(void)
{
    uintptr_t top = (uintptr_t)(co_stack + sizeof(co_stack)) & ~(uintptr_t)15u;
    uint64_t *frame = (uint64_t *)(top - 160u);
    memset(frame, 0, 160u);
    frame[11] = (uint64_t)(uintptr_t)co_trampoline; /* x30 = LR */
    co_app_sp = frame;
}
#elif defined(__x86_64__)
__attribute__((noinline, naked)) static void co_swap(void **from_sp, void *to_sp)
{
    __asm__ volatile(
        "push %rbp\n\t"
        "push %rbx\n\t"
        "push %r12\n\t"
        "push %r13\n\t"
        "push %r14\n\t"
        "push %r15\n\t"
        "mov %rsp, (%rdi)\n\t"
        "mov %rsi, %rsp\n\t"
        "pop %r15\n\t"
        "pop %r14\n\t"
        "pop %r13\n\t"
        "pop %r12\n\t"
        "pop %rbx\n\t"
        "pop %rbp\n\t"
        "ret\n\t"
    );
}

static void co_trampoline(void)
{
    app_main();
    for (;;) co_swap(&co_app_sp, co_main_sp);
}

static void co_init_stack(void)
{
    uintptr_t top = ((uintptr_t)(co_stack + sizeof(co_stack)) & ~(uintptr_t)15u) - 8u;
    uint64_t *frame = (uint64_t *)(top - 7u * 8u);
    memset(frame, 0, 7u * 8u);
    frame[6] = (uint64_t)(uintptr_t)co_trampoline; /* return address for ret */
    co_app_sp = frame;
}
#else
#error "dinghy_web.c supports aarch64 and x86_64"
#endif

__attribute__((noinline)) static void platform_poll(void)
{
    co_swap(&co_app_sp, co_main_sp);
}

static void sim_pass(void)
{
    if (!co_booted) {
        co_booted = 1;
        co_init_stack();
    }
    co_swap(&co_main_sp, co_app_sp);
}

/* ---- web_* surface for fm1deck_glue.h ---- */
#undef NB
#undef NE
#undef B_FX
#undef B_PLAY
#undef B_OCTUP
#undef EN_SELECT
#undef EN_K4
#define NB 14
#define NE 7
#define B_FX 0
#define B_PLAY 10
#define B_OCTUP 13
#define EN_SELECT 0
#define EN_K4 6

static const char *const B_NAME[NB] = {
    "FX", "SEL", "ENV", "LFO", "EDIT", "GLO", "HOME", "SAVE", "ARP", "SEQ", "PLAY", "REC", "OCT-", "OCT+"
};
static const uint8_t FM1_TO_DGY_BTN[14] = { 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 0, 1 };
static const uint8_t FM1_TO_DGY_ENC[7]  = { 0, 1, 6, 2, 3, 4, 5 };

static uint8_t nor[0x100000];
static uint16_t web_fb[240 * 240];
static float out_l[1024], out_r[1024];
static int32_t blk[2u * HALF_FRAMES];
static uint32_t blk_pos = HALF_FRAMES, ms_acc;
static int32_t master_q12 = 2048;
static uint32_t dgy_btn_mask, dgy_draws, dgy_last_hash;

#include "../common/store_nor.h"
#define DGY_MAGIC 0x31594744u

static uint32_t dgy_nor_hash, dgy_nor_writes, dgy_last_saved_writes = 0xFFFFFFFFu;

static uint32_t dgy_calc_hash(void)
{
    uint32_t h = 2166136261u;
    h = fd_fnv1a(h, host_store, sizeof(host_store));
    h = fd_fnv1a(h, host_blob, sizeof(host_blob));
#ifdef SLOT_SECT
    h = fd_fnv1a(h, host_slot, sizeof(host_slot));
#else
    h = fd_fnv1a(h, host_psect, sizeof(host_psect));
#endif
    return h;
}

static void dgy_unpack_nor(void)
{
    if (fd_nor_rd32(nor) != DGY_MAGIC) return;
    uint32_t s0 = fd_nor_rd32(nor + 4u);
    uint32_t s1 = fd_nor_rd32(nor + 8u);
    uint32_t s2 = fd_nor_rd32(nor + 12u);
#ifdef SLOT_SECT
    uint32_t e2 = (uint32_t)sizeof(host_slot);
#else
    uint32_t e2 = (uint32_t)sizeof(host_psect);
#endif
    if (s0 != (uint32_t)sizeof(host_store) || s1 != (uint32_t)sizeof(host_blob) || s2 != e2)
        return;
    if (16u + s0 + s1 + s2 > (uint32_t)sizeof(nor)) return;
    memcpy(host_store, nor + 16u, s0);
    memcpy(host_blob, nor + 16u + s0, s1);
#ifdef SLOT_SECT
    memcpy(host_slot, nor + 16u + s0 + s1, s2);
#else
    memcpy(host_psect, nor + 16u + s0 + s1, s2);
#endif
}

static int32_t dgy_pack_nor(void)
{
    uint32_t h = dgy_calc_hash();
    if (h == dgy_nor_hash && fd_nor_rd32(nor) == DGY_MAGIC)
        return 0;
    uint32_t s0 = (uint32_t)sizeof(host_store);
    uint32_t s1 = (uint32_t)sizeof(host_blob);
#ifdef SLOT_SECT
    uint32_t s2 = (uint32_t)sizeof(host_slot);
#else
    uint32_t s2 = (uint32_t)sizeof(host_psect);
#endif
    if (16u + s0 + s1 + s2 > (uint32_t)sizeof(nor)) return -1;
    fd_nor_wr32(nor, DGY_MAGIC);
    fd_nor_wr32(nor + 4u, s0);
    fd_nor_wr32(nor + 8u, s1);
    fd_nor_wr32(nor + 12u, s2);
    memcpy(nor + 16u, host_store, s0);
    memcpy(nor + 16u + s0, host_blob, s1);
#ifdef SLOT_SECT
    memcpy(nor + 16u + s0 + s1, host_slot, s2);
#else
    memcpy(nor + 16u + s0 + s1, host_psect, s2);
#endif
    if (16u + s0 + s1 + s2 < (uint32_t)sizeof(nor))
        memset(nor + 16u + s0 + s1 + s2, 0xFF, sizeof(nor) - (16u + s0 + s1 + s2));
    dgy_nor_hash = h;
    return 1;
}

static uint8_t *web_nor(void) { return nor; }
static uint32_t web_flash_writes_count(void)
{
    uint32_t h = dgy_calc_hash();
    if (h != dgy_nor_hash) {
        dgy_nor_hash = h;
        dgy_nor_writes++;
    }
    return dgy_nor_writes;
}
static void web_nor_erase(void) { memset(nor, 0xFF, sizeof(nor)); }

static void web_boot(void)
{
    if (!co_booted) {
        dgy_unpack_nor();
        blk_pos = HALF_FRAMES;
        ms_acc = 0;
        sim_pass();
        dgy_nor_hash = dgy_calc_hash();
        dgy_last_saved_writes = dgy_nor_writes;
    }
}

static void web_render(uint32_t n)
{
    if (n > 1024u) n = 1024u;
    if (!co_booted) {
        memset(out_l, 0, n * sizeof(float));
        memset(out_r, 0, n * sizeof(float));
        return;
    }
    for (uint32_t i = 0; i < n; i++) {
        if (blk_pos >= HALF_FRAMES) {
            memset(blk, 0, sizeof(blk));
            app_audio(blk, HALF_FRAMES);
            for (uint32_t j = 0; j < 2u * HALF_FRAMES; j++) {
                int32_t v = (blk[j] * master_q12) >> 12;
                blk[j] = v > 32767 ? 32767 : v < -32767 ? -32767 : v;
            }
            blk_pos = 0;
        }
        out_l[i] = (float)blk[2u * blk_pos] * (1.0f / 32768.0f);
        out_r[i] = (float)blk[2u * blk_pos + 1u] * (1.0f / 32768.0f);
        blk_pos++;
        if ((ms_acc += 1000u) >= FS) {
            ms_acc -= FS;
            fm1_ms++;
            sim_pass();
        }
    }
}

static float *web_out_l(void) { return out_l; }
static float *web_out_r(void) { return out_r; }

static void web_buttons(uint32_t mask)
{
    mask &= (1u << NB) - 1u;
    uint32_t diff = mask ^ dgy_btn_mask;
    for (int i = 0; i < NB; i++) {
        if ((diff >> i) & 1u) {
            uint32_t b = FM1_TO_DGY_BTN[i];
            if ((mask >> i) & 1u) {
                fm1_in.buttons |= 1u << b;
                host_edges |= 1u << b;
            } else {
                fm1_in.buttons &= ~(1u << b);
                if ((host_edges >> b) & 1u) host_rel_next |= 1u << b;
                else host_rel |= 1u << b;
            }
        }
    }
    dgy_btn_mask = mask;
}
static void web_keys(uint32_t mask) { fm1_in.notes = mask & 0x7FFFFFFu; }
static void web_enc(uint32_t role, int32_t steps)
{
    if (role < 7u) {
        uint32_t e = FM1_TO_DGY_ENC[role];
        host_enc[e] += steps;
        host_enc_pos[e] += steps;
    }
}
static void web_master(uint32_t adc)
{
    if (adc > 1023u) adc = 1023u;
    master_q12 = (int32_t)((adc * adc) >> 8);
}
static uint32_t web_midi(uint32_t status, uint32_t d1, uint32_t d2)
{
    if (status < 0x80u || status == 0xF0u || status == 0xF7u || status == 0xFEu || mq_w - mq_r >= 64u)
        return 1u;
    mq[mq_w % 64u][0] = (uint8_t)status;
    mq[mq_w % 64u][1] = (uint8_t)(d1 & 0x7Fu);
    mq[mq_w % 64u][2] = (uint8_t)(d2 & 0x7Fu);
    mq_w++;
    return 1u;
}
static uint32_t web_screen_draws(void)
{
    uint32_t h = 2166136261u;
    const uint32_t *p = (const uint32_t *)fb;
    for (uint32_t i = 0; i < 240u * 240u / 2u; i++)
        h = (h ^ p[i]) * 16777619u;
    if (h != dgy_last_hash) {
        dgy_last_hash = h;
        for (uint32_t i = 0; i < 240u * 240u; i++)
            web_fb[i] = (uint16_t)((fb[i] >> 8) | (fb[i] << 8));
        dgy_draws++;
    }
    return dgy_draws;
}
static const uint16_t *web_screen(void) { return web_fb; }
static uint32_t web_lit_buttons(void)
{
    uint32_t raw = (uint32_t)(host_leds & 0x3FFFu), m = 0;
    for (int i = 0; i < NB; i++)
        if ((raw >> FM1_TO_DGY_BTN[i]) & 1u) m |= 1u << i;
    return m;
}
static uint32_t web_lit_keys(void) { return (uint32_t)(host_leds >> 14) & 0x7FFFFFFu; }
static uint32_t web_dim_buttons(void) { return 0u; }
static uint32_t web_dim_keys(void) { return (uint32_t)(host_glow >> 14) & 0x7FFFFFFu; }
static uint32_t web_breath_buttons(void) { return 0u; }
static uint32_t web_breath_keys(void) { return 0u; }
static uint32_t web_mid_keys(void) { return 0u; }
static uint32_t web_dim_level(void) { return 1600u; }
static const uint8_t *web_anim_levels(void) { return 0; }
static uint32_t web_playing(void) { return 0u; }
static uint32_t web_now_ms(void) { return fm1_ms; }
