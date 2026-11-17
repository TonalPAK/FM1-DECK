/* SPDX-License-Identifier: GPL-3.0-only */
/* Shared SPI-NOR store/pack helpers for FM1 DECK HAL adapters (never called from the audio thread). */
#ifndef FM1DECK_HAL_STORE_NOR_H
#define FM1DECK_HAL_STORE_NOR_H

#include <stdint.h>
#include <string.h>

static uint32_t fd_nor_rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void fd_nor_wr32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
    p[2] = (uint8_t)((v >> 16) & 0xFFu);
    p[3] = (uint8_t)((v >> 24) & 0xFFu);
}

static uint32_t fd_fnv1a(uint32_t h, const void *buf, size_t n)
{
    const uint8_t *p = (const uint8_t *)buf;
    for (size_t i = 0; i < n; i++)
        h = (h ^ p[i]) * 16777619u;
    return h;
}

static void fd_unpack_slots(
    const uint8_t *nor, size_t nor_size, uint32_t magic,
    uint32_t nobj, uint32_t max_len,
    uint8_t *(*get_buf)(uint32_t), void (*set_len)(uint32_t, int))
{
    if (nor_size < 8u || fd_nor_rd32(nor) != magic || fd_nor_rd32(nor + 4u) != nobj)
        return;
    uint32_t pos = 8u;
    for (uint32_t i = 0; i < nobj; i++) {
        if (pos + 4u > (uint32_t)nor_size) break;
        int32_t len = (int32_t)fd_nor_rd32(nor + pos);
        pos += 4u;
        if (len < 0) {
            set_len(i, -1);
            continue;
        }
        if ((uint32_t)len > max_len || pos + (uint32_t)len > (uint32_t)nor_size)
            break;
        uint8_t *dst = get_buf(i);
        if (dst && len > 0)
            memcpy(dst, nor + pos, (size_t)len);
        set_len(i, len);
        pos += (uint32_t)len;
    }
}

static int32_t fd_pack_slots(
    uint8_t *nor, size_t nor_size, uint32_t magic, uint32_t nobj,
    uint8_t *(*get_buf)(uint32_t), int (*get_len)(uint32_t),
    uint32_t writes, uint32_t *last_saved_writes)
{
    if (writes == *last_saved_writes)
        return 0;
    if (nor_size < 8u) return -1;
    uint32_t pos = 8u;
    fd_nor_wr32(nor, magic);
    fd_nor_wr32(nor + 4u, nobj);
    for (uint32_t i = 0; i < nobj; i++) {
        if (pos + 4u > (uint32_t)nor_size) return -1;
        int len = get_len(i);
        fd_nor_wr32(nor + pos, (uint32_t)len);
        pos += 4u;
        if (len > 0) {
            if (pos + (uint32_t)len > (uint32_t)nor_size) return -1;
            const uint8_t *src = get_buf(i);
            if (src) memcpy(nor + pos, src, (size_t)len);
            pos += (uint32_t)len;
        }
    }
    if (pos < (uint32_t)nor_size)
        memset(nor + pos, 0xFF, (size_t)(nor_size - pos));
    *last_saved_writes = writes;
    return 1;
}

#endif
