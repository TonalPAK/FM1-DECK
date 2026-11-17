#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Translate standalone shrub.wasm (0 imports, fixed memory) into clean C99 source/header."""

from __future__ import annotations

import argparse
import struct
from dataclasses import dataclass, field
from pathlib import Path

VTYPES = {0x7F: "i32", 0x7E: "i64", 0x7D: "f32", 0x7C: "f64", 0x40: "void"}
CTYPES = {"i32": "uint32_t", "i64": "uint64_t", "f32": "float", "f64": "double", "void": "void"}


def uleb(b: bytes, p: int) -> tuple[int, int]:
    r = 0
    s = 0
    while True:
        x = b[p]
        p += 1
        r |= (x & 0x7F) << s
        if not (x & 0x80):
            return r, p
        s += 7


def sleb(b: bytes, p: int) -> tuple[int, int]:
    r = 0
    s = 0
    while True:
        x = b[p]
        p += 1
        r |= (x & 0x7F) << s
        s += 7
        if not (x & 0x80):
            if x & 0x40:
                r |= -(1 << s)
            return r, p


@dataclass
class Frame:
    kind: str  # 'func', 'block', 'loop', 'if'
    entry_height: int
    result_type: str | None
    result_var: str | None
    label_br: str
    label_end: str
    label_else: str | None = None
    has_else: bool = False
    outer_unreachable: bool = False
    unreachable: bool = False


@dataclass
class FuncContext:
    fidx: int
    params: list[str]
    ret_type: str | None
    locals_types: list[str]
    lines: list[str] = field(default_factory=list)
    stack: list[tuple[str, str]] = field(default_factory=list)
    frames: list[Frame] = field(default_factory=list)
    max_slots: dict[tuple[int, str], str] = field(default_factory=dict)
    blk_res_vars: list[tuple[str, str]] = field(default_factory=list)
    blk_counter: int = 0
    tmp_counter: int = 0

    def cur_unreachable(self) -> bool:
        return self.frames[-1].unreachable

    def set_unreachable(self) -> None:
        self.frames[-1].unreachable = True

    def slot(self, depth: int, vt: str) -> str:
        key = (depth, vt)
        if key not in self.max_slots:
            self.max_slots[key] = f"s{depth}_{vt}"
        return self.max_slots[key]

    def new_tmp(self, vt: str) -> str:
        self.tmp_counter += 1
        name = f"tmp_{self.tmp_counter}"
        self.blk_res_vars.append((vt, name))
        return name

    def new_blk_res(self, vt: str | None) -> str | None:
        if vt is None:
            return None
        self.blk_counter += 1
        name = f"blk_{self.blk_counter}_{vt}"
        self.blk_res_vars.append((vt, name))
        return name

    def push(self, vt: str, expr: str) -> None:
        if self.cur_unreachable():
            return
        depth = len(self.stack)
        var = self.slot(depth, vt)
        self.emit(f"{var} = {expr};")
        self.stack.append((vt, var))

    def pop(self, expected_vt: str | None = None) -> tuple[str, str]:
        if self.cur_unreachable():
            vt = expected_vt or "i32"
            zero = "0.0f" if vt == "f32" else ("0.0" if vt == "f64" else ("0ULL" if vt == "i64" else "0U"))
            return vt, zero
        if len(self.stack) <= self.frames[-1].entry_height:
            raise RuntimeError(f"f{self.fidx}: stack underflow below frame entry")
        vt, var = self.stack.pop()
        if expected_vt is not None and vt != expected_vt:
            raise RuntimeError(f"f{self.fidx}: expected {expected_vt}, got {vt}")
        return vt, var

    def peek(self) -> tuple[str, str]:
        if self.cur_unreachable():
            return "i32", "0U"
        if len(self.stack) <= self.frames[-1].entry_height:
            raise RuntimeError(f"f{self.fidx}: peek underflow")
        return self.stack[-1]

    def emit(self, line: str) -> None:
        indent = "    " * max(1, len(self.frames))
        self.lines.append(f"{indent}{line}")


PREAMBLE = r"""/* Auto-generated from shrub.wasm by tools/shrub_wasm2c.py — DO NOT EDIT. */
#ifndef SHRUB_WASM_GEN_H
#define SHRUB_WASM_GEN_H

#include <math.h>
#include <stdint.h>
#include <string.h>

#define SHRUB_WASM_MEM_BYTES (128u * 65536u)
static uint8_t wasm_mem[SHRUB_WASM_MEM_BYTES];
static uint32_t wasm_g0 = 3569904u;

static inline uint32_t w_ld_u8(uint32_t addr) {
    uint8_t v = 0;
    if (addr < SHRUB_WASM_MEM_BYTES) v = wasm_mem[addr];
    return (uint32_t)v;
}
static inline uint32_t w_ld_s8(uint32_t addr) {
    int8_t v = 0;
    if (addr < SHRUB_WASM_MEM_BYTES) v = (int8_t)wasm_mem[addr];
    return (uint32_t)(int32_t)v;
}
static inline uint32_t w_ld_u16(uint32_t addr) {
    uint16_t v = 0;
    if (addr <= SHRUB_WASM_MEM_BYTES - 2u) memcpy(&v, wasm_mem + addr, 2);
    return (uint32_t)v;
}
static inline uint32_t w_ld_s16(uint32_t addr) {
    int16_t v = 0;
    if (addr <= SHRUB_WASM_MEM_BYTES - 2u) memcpy(&v, wasm_mem + addr, 2);
    return (uint32_t)(int32_t)v;
}
static inline uint32_t w_ld_u32(uint32_t addr) {
    uint32_t v = 0;
    if (addr <= SHRUB_WASM_MEM_BYTES - 4u) memcpy(&v, wasm_mem + addr, 4);
    return v;
}
static inline uint64_t w_ld_u64(uint32_t addr) {
    uint64_t v = 0;
    if (addr <= SHRUB_WASM_MEM_BYTES - 8u) memcpy(&v, wasm_mem + addr, 8);
    return v;
}
static inline float w_ld_f32(uint32_t addr) {
    float v = 0.0f;
    if (addr <= SHRUB_WASM_MEM_BYTES - 4u) memcpy(&v, wasm_mem + addr, 4);
    return v;
}
static inline double w_ld_f64(uint32_t addr) {
    double v = 0.0;
    if (addr <= SHRUB_WASM_MEM_BYTES - 8u) memcpy(&v, wasm_mem + addr, 8);
    return v;
}
static inline uint64_t w_ld64_s8(uint32_t addr) { return (uint64_t)(int64_t)(int32_t)w_ld_s8(addr); }
static inline uint64_t w_ld64_u8(uint32_t addr) { return (uint64_t)w_ld_u8(addr); }
static inline uint64_t w_ld64_s16(uint32_t addr) { return (uint64_t)(int64_t)(int32_t)w_ld_s16(addr); }
static inline uint64_t w_ld64_u16(uint32_t addr) { return (uint64_t)w_ld_u16(addr); }
static inline uint64_t w_ld64_s32(uint32_t addr) { return (uint64_t)(int64_t)(int32_t)w_ld_u32(addr); }
static inline uint64_t w_ld64_u32(uint32_t addr) { return (uint64_t)w_ld_u32(addr); }

static inline void w_st_8(uint32_t addr, uint32_t v) {
    if (addr < SHRUB_WASM_MEM_BYTES) wasm_mem[addr] = (uint8_t)v;
}
static inline void w_st_16(uint32_t addr, uint32_t v) {
    uint16_t t = (uint16_t)v;
    if (addr <= SHRUB_WASM_MEM_BYTES - 2u) memcpy(wasm_mem + addr, &t, 2);
}
static inline void w_st_32(uint32_t addr, uint32_t v) {
    if (addr <= SHRUB_WASM_MEM_BYTES - 4u) memcpy(wasm_mem + addr, &v, 4);
}
static inline void w_st_64(uint32_t addr, uint64_t v) {
    if (addr <= SHRUB_WASM_MEM_BYTES - 8u) memcpy(wasm_mem + addr, &v, 8);
}
static inline void w_st_f32(uint32_t addr, float v) {
    if (addr <= SHRUB_WASM_MEM_BYTES - 4u) memcpy(wasm_mem + addr, &v, 4);
}
static inline void w_st_f64(uint32_t addr, double v) {
    if (addr <= SHRUB_WASM_MEM_BYTES - 8u) memcpy(wasm_mem + addr, &v, 8);
}

static inline void w_mem_copy(uint32_t dst, uint32_t src, uint32_t n) {
    if (n && dst <= SHRUB_WASM_MEM_BYTES && src <= SHRUB_WASM_MEM_BYTES &&
        n <= SHRUB_WASM_MEM_BYTES - dst && n <= SHRUB_WASM_MEM_BYTES - src) {
        memmove(wasm_mem + dst, wasm_mem + src, n);
    }
}
static inline void w_mem_fill(uint32_t dst, uint32_t val, uint32_t n) {
    if (n && dst <= SHRUB_WASM_MEM_BYTES && n <= SHRUB_WASM_MEM_BYTES - dst) {
        memset(wasm_mem + dst, (int)(val & 0xFFu), n);
    }
}

static inline uint32_t w_i32_sar(uint32_t a, uint32_t b) {
    uint32_t s = b & 31u;
    int32_t sa = (int32_t)a;
    if (!s) return a;
    return sa < 0 ? ~(~a >> s) : (a >> s);
}
static inline uint64_t w_i64_sar(uint64_t a, uint64_t b) {
    uint32_t s = (uint32_t)(b & 63u);
    int64_t sa = (int64_t)a;
    if (!s) return a;
    return sa < 0 ? ~(~a >> s) : (a >> s);
}
static inline uint32_t w_i32_rotl(uint32_t a, uint32_t b) {
    uint32_t s = b & 31u;
    return s ? ((a << s) | (a >> (32u - s))) : a;
}
static inline uint32_t w_i32_rotr(uint32_t a, uint32_t b) {
    uint32_t s = b & 31u;
    return s ? ((a >> s) | (a << (32u - s))) : a;
}
static inline uint64_t w_i64_rotl(uint64_t a, uint64_t b) {
    uint32_t s = (uint32_t)(b & 63u);
    return s ? ((a << s) | (a >> (64u - s))) : a;
}
static inline uint64_t w_i64_rotr(uint64_t a, uint64_t b) {
    uint32_t s = (uint32_t)(b & 63u);
    return s ? ((a >> s) | (a << (64u - s))) : a;
}
static inline uint32_t w_i32_clz(uint32_t x) {
    if (!x) return 32u;
#if defined(__GNUC__) || defined(__clang__)
    return (uint32_t)__builtin_clz(x);
#else
    uint32_t n = 0;
    while (!(x & 0x80000000u)) { n++; x <<= 1; }
    return n;
#endif
}
static inline uint32_t w_i32_ctz(uint32_t x) {
    if (!x) return 32u;
#if defined(__GNUC__) || defined(__clang__)
    return (uint32_t)__builtin_ctz(x);
#else
    uint32_t n = 0;
    while (!(x & 1u)) { n++; x >>= 1; }
    return n;
#endif
}
static inline uint32_t w_i32_popcnt(uint32_t x) {
#if defined(__GNUC__) || defined(__clang__)
    return (uint32_t)__builtin_popcount(x);
#else
    uint32_t n = 0;
    for (; x; x &= x - 1u) n++;
    return n;
#endif
}
static inline uint64_t w_i64_clz(uint64_t x) {
    if (!x) return 64ULL;
#if defined(__GNUC__) || defined(__clang__)
    return (uint64_t)__builtin_clzll(x);
#else
    uint64_t n = 0;
    while (!(x & (1ULL << 63))) { n++; x <<= 1; }
    return n;
#endif
}
static inline uint64_t w_i64_ctz(uint64_t x) {
    if (!x) return 64ULL;
#if defined(__GNUC__) || defined(__clang__)
    return (uint64_t)__builtin_ctzll(x);
#else
    uint64_t n = 0;
    while (!(x & 1ULL)) { n++; x >>= 1; }
    return n;
#endif
}
static inline uint32_t w_i32_sdiv(uint32_t a, uint32_t b) {
    int32_t sa = (int32_t)a, sb = (int32_t)b;
    if (sb == 0 || (sa == INT32_MIN && sb == -1)) return (uint32_t)sa;
    return (uint32_t)(sa / sb);
}
static inline uint32_t w_i32_srem(uint32_t a, uint32_t b) {
    int32_t sa = (int32_t)a, sb = (int32_t)b;
    if (sb == 0 || (sa == INT32_MIN && sb == -1)) return 0u;
    return (uint32_t)(sa % sb);
}
static inline uint64_t w_i64_sdiv(uint64_t a, uint64_t b) {
    int64_t sa = (int64_t)a, sb = (int64_t)b;
    if (sb == 0 || (sa == INT64_MIN && sb == -1)) return (uint64_t)sa;
    return (uint64_t)(sa / sb);
}
static inline uint64_t w_i64_srem(uint64_t a, uint64_t b) {
    int64_t sa = (int64_t)a, sb = (int64_t)b;
    if (sb == 0 || (sa == INT64_MIN && sb == -1)) return 0ULL;
    return (uint64_t)(sa % sb);
}
static inline float w_f32_reinterp(uint32_t u) {
    float f;
    memcpy(&f, &u, 4);
    return f;
}
static inline uint32_t w_i32_reinterp(float f) {
    uint32_t u;
    memcpy(&u, &f, 4);
    return u;
}
"""


def translate_wasm(wasm_bytes: bytes) -> str:
    if wasm_bytes[:8] != b"\x00asm\x01\x00\x00\x00":
        raise RuntimeError("Invalid WASM magic/version")

    pos = 8
    secs: dict[int, bytes] = {}
    while pos < len(wasm_bytes):
        sid = wasm_bytes[pos]
        pos += 1
        sz, pos = uleb(wasm_bytes, pos)
        secs[sid] = wasm_bytes[pos : pos + sz]
        pos += sz

    if 2 in secs:
        raise RuntimeError("Unexpected imports section in shrub.wasm")

    # Types
    tp = secs[1]
    p = 0
    tc, p = uleb(tp, p)
    types: list[tuple[list[str], list[str]]] = []
    for _ in range(tc):
        assert tp[p] == 0x60
        p += 1
        np, p = uleb(tp, p)
        params = [VTYPES[tp[p + j]] for j in range(np)]
        p += np
        nr, p = uleb(tp, p)
        rets = [VTYPES[tp[p + j]] for j in range(nr)]
        p += nr
        types.append((params, rets))

    # Function type indices
    fp = secs[3]
    p = 0
    fc, p = uleb(fp, p)
    func_type_idx: list[int] = []
    for _ in range(fc):
        ti, p = uleb(fp, p)
        func_type_idx.append(ti)

    # Exports
    xp = secs[7]
    p = 0
    xc, p = uleb(xp, p)
    exports: list[tuple[str, int, int]] = []
    for _ in range(xc):
        nlen, p = uleb(xp, p)
        name = xp[p : p + nlen].decode("utf-8")
        p += nlen
        kind = xp[p]
        p += 1
        idx, p = uleb(xp, p)
        exports.append((name, kind, idx))

    # Elements (function table)
    table_map: dict[int, int] = {}
    if 9 in secs:
        ep = secs[9]
        p = 0
        ec, p = uleb(ep, p)
        for _ in range(ec):
            mode, p = uleb(ep, p)
            assert mode == 0 and ep[p] == 0x41
            p += 1
            off, p = sleb(ep, p)
            assert ep[p] == 0x0B
            p += 1
            nc, p = uleb(ep, p)
            for k in range(nc):
                fi, p = uleb(ep, p)
                table_map[off + k] = fi

    # Data segments
    dp = secs[11]
    p = 0
    dc, p = uleb(dp, p)
    data_segs: list[tuple[int, bytes]] = []
    for _ in range(dc):
        mode, p = uleb(dp, p)
        assert mode == 0 and dp[p] == 0x41
        p += 1
        off, p = sleb(dp, p)
        assert dp[p] == 0x0B
        p += 1
        dlen, p = uleb(dp, p)
        data_segs.append((off, dp[p : p + dlen]))
        p += dlen

    # Global init check
    gp = secs[6]
    p = 0
    gc, p = uleb(gp, p)
    assert gc == 1 and gp[p] == 0x7F and gp[p + 1] == 1 and gp[p + 2] == 0x41
    g0_init, gp_next = sleb(gp, p + 3)
    assert gp[gp_next] == 0x0B

    # Code section
    cp = secs[10]
    p = 0
    cc, p = uleb(cp, p)
    assert cc == fc

    out: list[str] = [PREAMBLE]
    out.append(f"static void wasm_reset_globals(void) {{ wasm_g0 = {g0_init & 0xFFFFFFFF}u; }}\n")

    # Data initialization blob + table
    blob = bytearray()
    seg_records: list[tuple[int, int, int]] = []
    for off, seg in data_segs:
        seg_records.append((off, len(blob), len(seg)))
        blob.extend(seg)

    out.append(f"static const uint8_t wasm_data_blob[{len(blob)}] = {{")
    for i in range(0, len(blob), 24):
        chunk = blob[i : i + 24]
        out.append("    " + ",".join(f"0x{b:02x}" for b in chunk) + ",")
    out.append("};\n")
    out.append(f"typedef struct {{ uint32_t dst; uint32_t src; uint32_t len; }} WasmDataSeg;")
    out.append(f"static const WasmDataSeg wasm_data_segs[{len(seg_records)}] = {{")
    for dst, src, ln in seg_records:
        out.append(f"    {{{dst}u, {src}u, {ln}u}},")
    out.append("};")
    out.append("""static void wasm_init_memory(void) {
    memset(wasm_mem, 0, sizeof(wasm_mem));
    wasm_reset_globals();
    for (uint32_t i = 0; i < (uint32_t)(sizeof(wasm_data_segs) / sizeof(wasm_data_segs[0])); i++) {
        memcpy(wasm_mem + wasm_data_segs[i].dst, wasm_data_blob + wasm_data_segs[i].src, wasm_data_segs[i].len);
    }
}
""")

    # Forward declarations for all 292 functions
    for fi in range(fc):
        params, rets = types[func_type_idx[fi]]
        ret_c = CTYPES[rets[0]] if rets else "void"
        param_c = ", ".join(f"{CTYPES[pt]} p{k}" for k, pt in enumerate(params)) or "void"
        out.append(f"static {ret_c} wf_{fi}({param_c});")
    out.append("")

    # Helper to emit call_indirect dispatch per signature type index
    used_indirect_types: set[int] = set()

    # First pass: decode all functions, collect used indirect types
    func_bodies_c: list[str] = []
    for fi in range(fc):
        bsz, p = uleb(cp, p)
        fend = p + bsz
        lcnt, p = uleb(cp, p)
        decl_locals: list[str] = []
        for _ in range(lcnt):
            n, p = uleb(cp, p)
            vt = VTYPES[cp[p]]
            p += 1
            decl_locals.extend([vt] * n)

        params, rets = types[func_type_idx[fi]]
        ret_type = rets[0] if rets else None
        all_locals = params + decl_locals
        ctx = FuncContext(fidx=fi, params=params, ret_type=ret_type, locals_types=all_locals)
        root_res = "ret_val" if ret_type else None
        ctx.frames.append(
            Frame(
                kind="func",
                entry_height=0,
                result_type=ret_type,
                result_var=root_res,
                label_br="L_func_ret",
                label_end="L_func_ret",
            )
        )

        def emit_branch(target_depth: int, val_expr: str | None) -> None:
            tgt = ctx.frames[-1 - target_depth]
            if tgt.kind != "loop" and tgt.result_type is not None and tgt.result_var is not None:
                ctx.emit(f"{tgt.result_var} = {val_expr};")
            ctx.emit(f"goto {tgt.label_br};")

        while p < fend:
            op = cp[p]
            p += 1
            if op == 0x00:  # unreachable
                ctx.emit("/* unreachable */")
                ctx.set_unreachable()
            elif op == 0x01:  # nop
                pass
            elif op in (0x02, 0x03, 0x04):  # block, loop, if
                bt = cp[p]
                p += 1
                if bt not in VTYPES:
                    raise RuntimeError(f"f{fi}: multi-value/indexed block type 0x{bt:02x}")
                rt = None if bt == 0x40 else VTYPES[bt]
                outer_unreach = ctx.cur_unreachable()
                if op == 0x04:
                    _, cond = ctx.pop("i32")
                else:
                    cond = ""
                ctx.blk_counter += 1
                bid = ctx.blk_counter
                rvar = f"blk_{bid}_{rt}" if rt else None
                if rvar:
                    ctx.blk_res_vars.append((rt, rvar))
                eh = len(ctx.stack)
                if op == 0x02:
                    ctx.frames.append(
                        Frame("block", eh, rt, rvar, f"L_end_{bid}", f"L_end_{bid}", outer_unreachable=outer_unreach, unreachable=outer_unreach)
                    )
                elif op == 0x03:
                    ctx.emit(f"L_loop_{bid}: ;")
                    ctx.frames.append(
                        Frame("loop", eh, rt, rvar, f"L_loop_{bid}", f"L_end_{bid}", outer_unreachable=outer_unreach, unreachable=outer_unreach)
                    )
                else:
                    if not outer_unreach:
                        ctx.emit(f"if (!({cond})) goto L_else_{bid};")
                    ctx.frames.append(
                        Frame("if", eh, rt, rvar, f"L_end_{bid}", f"L_end_{bid}", label_else=f"L_else_{bid}", outer_unreachable=outer_unreach, unreachable=outer_unreach)
                    )
            elif op == 0x05:  # else
                fr = ctx.frames[-1]
                assert fr.kind == "if" and not fr.has_else
                if not fr.unreachable and fr.result_type and fr.result_var:
                    _, rv = ctx.pop(fr.result_type)
                    ctx.emit(f"{fr.result_var} = {rv};")
                if not fr.outer_unreachable:
                    ctx.emit(f"goto {fr.label_end};")
                ctx.emit(f"{fr.label_else}: ;")
                del ctx.stack[fr.entry_height :]
                fr.has_else = True
                fr.unreachable = fr.outer_unreachable
            elif op == 0x0B:  # end
                fr = ctx.frames[-1]
                if not fr.unreachable and fr.result_type and fr.result_var:
                    _, rv = ctx.pop(fr.result_type)
                    ctx.emit(f"{fr.result_var} = {rv};")
                ctx.frames.pop()
                if fr.kind == "func":
                    ctx.emit("L_func_ret: ;")
                    assert p == fend, f"f{fi}: func end before fend"
                else:
                    if fr.kind == "if" and not fr.has_else:
                        ctx.emit(f"{fr.label_else}: ;")
                    ctx.emit(f"{fr.label_end}: ;")
                    del ctx.stack[fr.entry_height :]
                    ctx.frames[-1].unreachable = fr.outer_unreachable
                    if fr.result_type and fr.result_var:
                        ctx.push(fr.result_type, fr.result_var)
            elif op == 0x0C:  # br
                depth, p = uleb(cp, p)
                tgt = ctx.frames[-1 - depth]
                val_expr = None
                if tgt.kind != "loop" and tgt.result_type is not None:
                    _, val_expr = ctx.pop(tgt.result_type)
                if not ctx.cur_unreachable():
                    emit_branch(depth, val_expr)
                ctx.set_unreachable()
            elif op == 0x0D:  # br_if
                depth, p = uleb(cp, p)
                _, cond = ctx.pop("i32")
                tgt = ctx.frames[-1 - depth]
                if not ctx.cur_unreachable():
                    ctx.emit(f"if ({cond}) {{")
                    if tgt.kind != "loop" and tgt.result_type is not None and tgt.result_var is not None:
                        _, pv = ctx.peek()
                        ctx.emit(f"    {tgt.result_var} = {pv};")
                    ctx.emit(f"    goto {tgt.label_br};")
                    ctx.emit("}")
            elif op == 0x0E:  # br_table
                cnt, p = uleb(cp, p)
                depths = []
                for _ in range(cnt + 1):
                    d, p = uleb(cp, p)
                    depths.append(d)
                _, idx = ctx.pop("i32")
                # Check if target expects value
                t0 = ctx.frames[-1 - depths[0]]
                val_expr = None
                if t0.kind != "loop" and t0.result_type is not None:
                    _, val_expr = ctx.pop(t0.result_type)
                if not ctx.cur_unreachable():
                    ctx.emit(f"switch ({idx}) {{")
                    for ci, d in enumerate(depths[:-1]):
                        ctx.emit(f"case {ci}:")
                        emit_branch(d, val_expr)
                    ctx.emit("default:")
                    emit_branch(depths[-1], val_expr)
                    ctx.emit("}")
                ctx.set_unreachable()
            elif op == 0x0F:  # return
                if ret_type:
                    _, rv = ctx.pop(ret_type)
                    if not ctx.cur_unreachable():
                        ctx.emit(f"ret_val = {rv};")
                if not ctx.cur_unreachable():
                    ctx.emit("goto L_func_ret;")
                ctx.set_unreachable()
            elif op == 0x10:  # call
                cf, p = uleb(cp, p)
                cparams, crets = types[func_type_idx[cf]]
                args = [ctx.pop(pt)[1] for pt in reversed(cparams)]
                args.reverse()
                call_expr = f"wf_{cf}({', '.join(args)})"
                if crets:
                    ctx.push(crets[0], call_expr)
                elif not ctx.cur_unreachable():
                    ctx.emit(f"{call_expr};")
            elif op == 0x11:  # call_indirect
                ti, p = uleb(cp, p)
                tbl, p = uleb(cp, p)
                assert tbl == 0
                used_indirect_types.add(ti)
                cparams, crets = types[ti]
                _, tidx = ctx.pop("i32")
                args = [ctx.pop(pt)[1] for pt in reversed(cparams)]
                args.reverse()
                all_args = [tidx] + args
                call_expr = f"w_call_indirect_t{ti}({', '.join(all_args)})"
                if crets:
                    ctx.push(crets[0], call_expr)
                elif not ctx.cur_unreachable():
                    ctx.emit(f"{call_expr};")
            elif op == 0x1A:  # drop
                ctx.pop()
            elif op == 0x1B:  # select
                _, c = ctx.pop("i32")
                t2, v2 = ctx.pop()
                t1, v1 = ctx.pop(t2)
                ctx.push(t1, f"(({c}) ? ({v1}) : ({v2}))")
            elif op == 0x20:  # local.get
                li, p = uleb(cp, p)
                ctx.push(all_locals[li], f"l{li}")
            elif op == 0x21:  # local.set
                li, p = uleb(cp, p)
                _, v = ctx.pop(all_locals[li])
                if not ctx.cur_unreachable():
                    ctx.emit(f"l{li} = {v};")
            elif op == 0x22:  # local.tee
                li, p = uleb(cp, p)
                _, v = ctx.pop(all_locals[li])
                if not ctx.cur_unreachable():
                    ctx.emit(f"l{li} = {v};")
                    ctx.push(all_locals[li], f"l{li}")
            elif op == 0x23:  # global.get
                gi, p = uleb(cp, p)
                assert gi == 0
                ctx.push("i32", "wasm_g0")
            elif op == 0x24:  # global.set
                gi, p = uleb(cp, p)
                assert gi == 0
                _, v = ctx.pop("i32")
                if not ctx.cur_unreachable():
                    ctx.emit(f"wasm_g0 = {v};")
            elif 0x28 <= op <= 0x35:  # loads
                _, p = uleb(cp, p)  # align
                off, p = uleb(cp, p)
                _, base = ctx.pop("i32")
                addr = f"(uint32_t)({base} + {off}u)"
                load_map = {
                    0x28: ("i32", "w_ld_u32"),
                    0x29: ("i64", "w_ld_u64"),
                    0x2A: ("f32", "w_ld_f32"),
                    0x2B: ("f64", "w_ld_f64"),
                    0x2C: ("i32", "w_ld_s8"),
                    0x2D: ("i32", "w_ld_u8"),
                    0x2E: ("i32", "w_ld_s16"),
                    0x2F: ("i32", "w_ld_u16"),
                    0x30: ("i64", "w_ld64_s8"),
                    0x31: ("i64", "w_ld64_u8"),
                    0x32: ("i64", "w_ld64_s16"),
                    0x33: ("i64", "w_ld64_u16"),
                    0x34: ("i64", "w_ld64_s32"),
                    0x35: ("i64", "w_ld64_u32"),
                }
                vt, fn = load_map[op]
                ctx.push(vt, f"{fn}({addr})")
            elif 0x36 <= op <= 0x3E:  # stores
                _, p = uleb(cp, p)  # align
                off, p = uleb(cp, p)
                store_map = {
                    0x36: ("i32", "w_st_32"),
                    0x37: ("i64", "w_st_64"),
                    0x38: ("f32", "w_st_f32"),
                    0x39: ("f64", "w_st_f64"),
                    0x3A: ("i32", "w_st_8"),
                    0x3B: ("i32", "w_st_16"),
                    0x3C: ("i64", "w_st_8"),
                    0x3D: ("i64", "w_st_16"),
                    0x3E: ("i64", "w_st_32"),
                }
                vt, fn = store_map[op]
                _, val = ctx.pop(vt)
                _, base = ctx.pop("i32")
                if not ctx.cur_unreachable():
                    cast_val = f"(uint32_t)({val})" if op in (0x3C, 0x3D, 0x3E) else val
                    ctx.emit(f"{fn}((uint32_t)({base} + {off}u), {cast_val});")
            elif op == 0x3F:  # memory.size
                p += 1
                ctx.push("i32", "128u")
            elif op == 0x40:  # memory.grow
                p += 1
                _, _ = ctx.pop("i32")
                ctx.push("i32", "0xFFFFFFFFu")
            elif op == 0x41:  # i32.const
                v, p = sleb(cp, p)
                ctx.push("i32", f"{v & 0xFFFFFFFF}u")
            elif op == 0x42:  # i64.const
                v, p = sleb(cp, p)
                ctx.push("i64", f"{v & 0xFFFFFFFFFFFFFFFF}ULL")
            elif op == 0x43:  # f32.const
                raw_u32 = struct.unpack_from("<I", cp, p)[0]
                p += 4
                ctx.push("f32", f"w_f32_reinterp(0x{raw_u32:08x}u)")
            elif op == 0x44:  # f64.const
                raw_u64 = struct.unpack_from("<Q", cp, p)[0]
                p += 8
                f64v = struct.unpack("<d", struct.pack("<Q", raw_u64))[0]
                ctx.push("f64", repr(f64v))
            elif op == 0x45:  # i32.eqz
                _, a = ctx.pop("i32")
                ctx.push("i32", f"(({a}) == 0u ? 1u : 0u)")
            elif 0x46 <= op <= 0x4F:  # i32 cmp
                _, b = ctx.pop("i32")
                _, a = ctx.pop("i32")
                cmps = {
                    0x46: f"({a}) == ({b})",
                    0x47: f"({a}) != ({b})",
                    0x48: f"(int32_t)({a}) < (int32_t)({b})",
                    0x49: f"({a}) < ({b})",
                    0x4A: f"(int32_t)({a}) > (int32_t)({b})",
                    0x4B: f"({a}) > ({b})",
                    0x4C: f"(int32_t)({a}) <= (int32_t)({b})",
                    0x4D: f"({a}) <= ({b})",
                    0x4E: f"(int32_t)({a}) >= (int32_t)({b})",
                    0x4F: f"({a}) >= ({b})",
                }
                ctx.push("i32", f"(({cmps[op]}) ? 1u : 0u)")
            elif op == 0x50:  # i64.eqz
                _, a = ctx.pop("i64")
                ctx.push("i32", f"(({a}) == 0ULL ? 1u : 0u)")
            elif 0x51 <= op <= 0x5A:  # i64 cmp
                _, b = ctx.pop("i64")
                _, a = ctx.pop("i64")
                cmps = {
                    0x51: f"({a}) == ({b})",
                    0x52: f"({a}) != ({b})",
                    0x53: f"(int64_t)({a}) < (int64_t)({b})",
                    0x54: f"({a}) < ({b})",
                    0x55: f"(int64_t)({a}) > (int64_t)({b})",
                    0x56: f"({a}) > ({b})",
                    0x57: f"(int64_t)({a}) <= (int64_t)({b})",
                    0x58: f"({a}) <= ({b})",
                    0x59: f"(int64_t)({a}) >= (int64_t)({b})",
                    0x5A: f"({a}) >= ({b})",
                }
                ctx.push("i32", f"(({cmps[op]}) ? 1u : 0u)")
            elif 0x5B <= op <= 0x60:  # f32 cmp
                _, b = ctx.pop("f32")
                _, a = ctx.pop("f32")
                cmps = {
                    0x5B: f"({a}) == ({b})",
                    0x5C: f"({a}) != ({b})",
                    0x5D: f"({a}) < ({b})",
                    0x5E: f"({a}) > ({b})",
                    0x5F: f"({a}) <= ({b})",
                    0x60: f"({a}) >= ({b})",
                }
                ctx.push("i32", f"(({cmps[op]}) ? 1u : 0u)")
            elif op == 0x67:
                _, a = ctx.pop("i32")
                ctx.push("i32", f"w_i32_clz({a})")
            elif op == 0x68:
                _, a = ctx.pop("i32")
                ctx.push("i32", f"w_i32_ctz({a})")
            elif op == 0x69:
                _, a = ctx.pop("i32")
                ctx.push("i32", f"w_i32_popcnt({a})")
            elif 0x6A <= op <= 0x78:  # i32 binop
                _, b = ctx.pop("i32")
                _, a = ctx.pop("i32")
                ops32 = {
                    0x6A: f"(({a}) + ({b}))",
                    0x6B: f"(({a}) - ({b}))",
                    0x6C: f"(({a}) * ({b}))",
                    0x6D: f"w_i32_sdiv({a}, {b})",
                    0x6E: f"(({b}) ? (({a}) / ({b})) : 0u)",
                    0x6F: f"w_i32_srem({a}, {b})",
                    0x70: f"(({b}) ? (({a}) % ({b})) : 0u)",
                    0x71: f"(({a}) & ({b}))",
                    0x72: f"(({a}) | ({b}))",
                    0x73: f"(({a}) ^ ({b}))",
                    0x74: f"(({a}) << (({b}) & 31u))",
                    0x75: f"w_i32_sar({a}, {b})",
                    0x76: f"(({a}) >> (({b}) & 31u))",
                    0x77: f"w_i32_rotl({a}, {b})",
                    0x78: f"w_i32_rotr({a}, {b})",
                }
                ctx.push("i32", ops32[op])
            elif op == 0x79:
                _, a = ctx.pop("i64")
                ctx.push("i64", f"w_i64_clz({a})")
            elif op == 0x7A:
                _, a = ctx.pop("i64")
                ctx.push("i64", f"w_i64_ctz({a})")
            elif 0x7C <= op <= 0x8A:  # i64 binop
                _, b = ctx.pop("i64")
                _, a = ctx.pop("i64")
                ops64 = {
                    0x7C: f"(({a}) + ({b}))",
                    0x7D: f"(({a}) - ({b}))",
                    0x7E: f"(({a}) * ({b}))",
                    0x7F: f"w_i64_sdiv({a}, {b})",
                    0x80: f"(({b}) ? (({a}) / ({b})) : 0ULL)",
                    0x81: f"w_i64_srem({a}, {b})",
                    0x82: f"(({b}) ? (({a}) % ({b})) : 0ULL)",
                    0x83: f"(({a}) & ({b}))",
                    0x84: f"(({a}) | ({b}))",
                    0x85: f"(({a}) ^ ({b}))",
                    0x86: f"(({a}) << (({b}) & 63ULL))",
                    0x87: f"w_i64_sar({a}, {b})",
                    0x88: f"(({a}) >> (({b}) & 63ULL))",
                    0x89: f"w_i64_rotl({a}, {b})",
                    0x8A: f"w_i64_rotr({a}, {b})",
                }
                ctx.push("i64", ops64[op])
            elif op == 0x8B:
                _, a = ctx.pop("f32")
                ctx.push("f32", f"fabsf({a})")
            elif op == 0x8C:
                _, a = ctx.pop("f32")
                ctx.push("f32", f"(-({a}))")
            elif op == 0x91:
                _, a = ctx.pop("f32")
                ctx.push("f32", f"sqrtf({a})")
            elif 0x92 <= op <= 0x98:  # f32 binops
                _, b = ctx.pop("f32")
                _, a = ctx.pop("f32")
                fops = {
                    0x92: f"(({a}) + ({b}))",
                    0x93: f"(({a}) - ({b}))",
                    0x94: f"(({a}) * ({b}))",
                    0x95: f"(({a}) / ({b}))",
                    0x96: f"fminf({a}, {b})",
                    0x97: f"fmaxf({a}, {b})",
                    0x98: f"copysignf({a}, {b})",
                }
                ctx.push("f32", fops[op])
            elif op == 0xA7:  # i32.wrap_i64
                _, a = ctx.pop("i64")
                ctx.push("i32", f"(uint32_t)({a})")
            elif op == 0xA8:  # i32.trunc_f32_s
                _, a = ctx.pop("f32")
                ctx.push("i32", f"(uint32_t)(int32_t)({a})")
            elif op == 0xA9:  # i32.trunc_f32_u
                _, a = ctx.pop("f32")
                ctx.push("i32", f"(uint32_t)({a})")
            elif op == 0xAC:  # i64.extend_i32_s
                _, a = ctx.pop("i32")
                ctx.push("i64", f"(uint64_t)(int64_t)(int32_t)({a})")
            elif op == 0xAD:  # i64.extend_i32_u
                _, a = ctx.pop("i32")
                ctx.push("i64", f"(uint64_t)({a})")
            elif op == 0xB2:  # f32.convert_i32_s
                _, a = ctx.pop("i32")
                ctx.push("f32", f"(float)(int32_t)({a})")
            elif op == 0xB3:  # f32.convert_i32_u
                _, a = ctx.pop("i32")
                ctx.push("f32", f"(float)({a})")
            elif op == 0xBC:  # i32.reinterpret_f32
                _, a = ctx.pop("f32")
                ctx.push("i32", f"w_i32_reinterp({a})")
            elif op == 0xBE:  # f32.reinterpret_i32
                _, a = ctx.pop("i32")
                ctx.push("f32", f"w_f32_reinterp({a})")
            elif op == 0xC0:  # i32.extend8_s
                _, a = ctx.pop("i32")
                ctx.push("i32", f"(uint32_t)(int32_t)(int8_t)(uint8_t)({a})")
            elif op == 0xC1:  # i32.extend16_s
                _, a = ctx.pop("i32")
                ctx.push("i32", f"(uint32_t)(int32_t)(int16_t)(uint16_t)({a})")
            elif op == 0xC2:  # i64.extend8_s
                _, a = ctx.pop("i64")
                ctx.push("i64", f"(uint64_t)(int64_t)(int8_t)(uint8_t)({a})")
            elif op == 0xC3:  # i64.extend16_s
                _, a = ctx.pop("i64")
                ctx.push("i64", f"(uint64_t)(int64_t)(int16_t)(uint16_t)({a})")
            elif op == 0xC4:  # i64.extend32_s
                _, a = ctx.pop("i64")
                ctx.push("i64", f"(uint64_t)(int64_t)(int32_t)(uint32_t)({a})")
            elif op == 0xFC:
                sub, p = uleb(cp, p)
                if sub == 10:  # memory.copy
                    assert cp[p] == 0 and cp[p + 1] == 0
                    p += 2
                    _, n = ctx.pop("i32")
                    _, src = ctx.pop("i32")
                    _, dst = ctx.pop("i32")
                    if not ctx.cur_unreachable():
                        ctx.emit(f"w_mem_copy({dst}, {src}, {n});")
                elif sub == 11:  # memory.fill
                    assert cp[p] == 0
                    p += 1
                    _, n = ctx.pop("i32")
                    _, val = ctx.pop("i32")
                    _, dst = ctx.pop("i32")
                    if not ctx.cur_unreachable():
                        ctx.emit(f"w_mem_fill({dst}, {val}, {n});")
                else:
                    raise RuntimeError(f"f{fi}: unsupported 0xfc subop {sub}")
            else:
                raise RuntimeError(f"f{fi}: unsupported opcode 0x{op:02x}")

        # Build complete C function
        ret_c = CTYPES[ret_type] if ret_type else "void"
        param_c = ", ".join(f"{CTYPES[pt]} l{k}" for k, pt in enumerate(params)) or "void"
        flines = [f"static {ret_c} wf_{fi}({param_c}) {{"]
        if ret_type:
            flines.append(f"    {CTYPES[ret_type]} ret_val = 0;")
        for k in range(len(params), len(all_locals)):
            flines.append(f"    {CTYPES[all_locals[k]]} l{k} = 0;")
        for (_, vt), vname in sorted(ctx.max_slots.items()):
            flines.append(f"    {CTYPES[vt]} {vname} = 0;")
        for vt, vname in ctx.blk_res_vars:
            flines.append(f"    {CTYPES[vt]} {vname} = 0;")
        flines.extend(ctx.lines)
        if ret_type:
            flines.append("    return ret_val;")
        flines.append("}\n")
        func_bodies_c.append("\n".join(flines))

    # Emit call_indirect dispatchers before function bodies
    for ti in sorted(used_indirect_types):
        tparams, trets = types[ti]
        ret_c = CTYPES[trets[0]] if trets else "void"
        plist = ["uint32_t idx"] + [f"{CTYPES[pt]} a{k}" for k, pt in enumerate(tparams)]
        out.append(f"static {ret_c} w_call_indirect_t{ti}({', '.join(plist)}) {{")
        out.append("    switch (idx) {")
        for tidx, fi in sorted(table_map.items()):
            if func_type_idx[fi] == ti:
                alist = ", ".join(f"a{k}" for k in range(len(tparams)))
                if trets:
                    out.append(f"    case {tidx}u: return wf_{fi}({alist});")
                else:
                    out.append(f"    case {tidx}u: wf_{fi}({alist}); return;")
        out.append("    default: break;")
        out.append("    }")
        if trets:
            out.append("    return 0;")
        out.append("}\n")

    out.extend(func_bodies_c)

    # Emit clean static wrapper functions for all exported sw_* + _initialize
    out.append("/* Exported SHRUB WASM API wrappers */")
    for name, kind, idx in exports:
        if kind != 0 or name in ("setThrew", "_emscripten_stack_restore", "emscripten_stack_get_current"):
            continue
        tparams, trets = types[func_type_idx[idx]]
        ret_c = CTYPES[trets[0]] if trets else "void"
        cname = "shrub_wasm_initialize" if name == "_initialize" else name
        plist = ", ".join(f"{CTYPES[pt]} a{k}" for k, pt in enumerate(tparams)) or "void"
        alist = ", ".join(f"a{k}" for k in range(len(tparams)))
        if trets:
            out.append(f"static inline {ret_c} {cname}({plist}) {{ return wf_{idx}({alist}); }}")
        else:
            out.append(f"static inline void {cname}({plist}) {{ wf_{idx}({alist}); }}")

    out.append("\n#endif /* SHRUB_WASM_GEN_H */\n")
    return "\n".join(out)


def main() -> None:
    parser = argparse.ArgumentParser(description="Translate shrub.wasm into self-contained C99")
    parser.add_argument("--wasm", type=Path, default=Path(r"D:\DECOMPILLE\Shrub_decompiled\ui\shrub.wasm"))
    parser.add_argument("--out-header", type=Path, required=True)
    parser.add_argument("--out-decompiled", type=Path, default=None)
    args = parser.parse_args()

    wasm_bytes = args.wasm.read_bytes()
    c_src = translate_wasm(wasm_bytes)
    args.out_header.parent.mkdir(parents=True, exist_ok=True)
    args.out_header.write_text(c_src, encoding="utf-8")
    print(f"[wasm2c] Wrote {args.out_header} ({len(c_src)} bytes, {c_src.count(chr(10))} lines)")
    if args.out_decompiled is not None:
        args.out_decompiled.parent.mkdir(parents=True, exist_ok=True)
        args.out_decompiled.write_text(c_src, encoding="utf-8")
        print(f"[wasm2c] Wrote {args.out_decompiled}")


if __name__ == "__main__":
    main()
