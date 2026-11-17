#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Extract embedded UI archive (ui.zip) and native DSP DLL (shrub_engine.bin) from Shrub.vst3,
then disassemble shrub_engine.dll into Intel x86-64 assembly and function metadata."""

from __future__ import annotations

import argparse
import io
import json
import os
import re
import shutil
import struct
import subprocess
import zipfile
from pathlib import Path

DEFAULT_VST3 = Path(r"D:\DECOMPILLE\Shrub.vst3\Contents\x86_64-win\Shrub.vst3")
DEFAULT_OUT = Path(r"D:\DECOMPILLE\Shrub_decompiled")

OBJDUMP_CANDIDATES = [
    Path(r"C:\Users\ADMIN\AppData\Local\Android\Sdk\ndk\28.2.13676358\toolchains\llvm\prebuilt\windows-x86_64\bin\llvm-objdump.exe"),
    Path(r"C:\Users\ADMIN\AppData\Local\Android\Sdk\ndk\27.0.12077973\toolchains\llvm\prebuilt\windows-x86_64\bin\llvm-objdump.exe"),
    Path(r"C:\Users\ADMIN\AppData\Local\Android\Sdk\ndk\26.1.10909125\toolchains\llvm\prebuilt\windows-x86_64\bin\llvm-objdump.exe"),
]


def find_objdump() -> Path:
    for p in OBJDUMP_CANDIDATES:
        if p.is_file():
            return p
    found = shutil.which("llvm-objdump") or shutil.which("objdump")
    if found:
        return Path(found)
    raise FileNotFoundError("Could not find llvm-objdump.exe in Android NDK or PATH")


def find_ui_zip(data: bytes) -> tuple[int, int]:
    """Locate the embedded ui.zip (containing app.js and shrub.wasm) inside Shrub.vst3."""
    marker = data.find(b"PK\x03\x04")
    while marker != -1:
        # Check if app.js appears right after the local file header (first entry is app.js)
        if b"app.js" in data[marker : marker + 64]:
            eocd = data.find(b"PK\x05\x06", marker)
            while eocd != -1:
                if eocd + 22 <= len(data):
                    comment_len = struct.unpack_from("<H", data, eocd + 20)[0]
                    end = eocd + 22 + comment_len
                    try:
                        zf = zipfile.ZipFile(io.BytesIO(data[marker:end]))
                        if "shrub.wasm" in zf.namelist():
                            return marker, end
                    except zipfile.BadZipFile:
                        pass
                eocd = data.find(b"PK\x05\x06", eocd + 4)
        marker = data.find(b"PK\x03\x04", marker + 4)
    raise RuntimeError("Embedded ui.zip not found in Shrub.vst3")


def find_shrub_engine_pe(data: bytes) -> tuple[int, int]:
    """Locate the embedded nested PE64 DLL (shrub_engine.bin) inside Shrub.vst3."""
    tag = data.find(b"shrub_engine.bin\x00")
    if tag == -1:
        raise RuntimeError("shrub_engine.bin resource marker not found")
    mz_off = data.find(b"MZ\x90\x00", tag)
    if mz_off == -1 or mz_off - tag > 64:
        raise RuntimeError("MZ header after shrub_engine.bin marker not found")

    pe_rel = struct.unpack_from("<I", data, mz_off + 0x3C)[0]
    pe_off = mz_off + pe_rel
    if data[pe_off : pe_off + 4] != b"PE\x00\x00":
        raise RuntimeError("Invalid PE signature in embedded shrub_engine.bin")
    num_sec = struct.unpack_from("<H", data, pe_off + 6)[0]
    opt_size = struct.unpack_from("<H", data, pe_off + 20)[0]
    sec_off = pe_off + 24 + opt_size

    pe_end = 0
    for i in range(num_sec):
        s = data[sec_off + i * 40 : sec_off + (i + 1) * 40]
        _, _, rsize, roff = struct.unpack_from("<IIII", s, 8)
        pe_end = max(pe_end, roff + rsize)
    return mz_off, mz_off + pe_end


def parse_pe_metadata(pe: bytes) -> dict:
    pe_off = struct.unpack_from("<I", pe, 0x3C)[0]
    opt_off = pe_off + 24
    magic = struct.unpack_from("<H", pe, opt_off)[0]
    if magic != 0x20B:
        raise RuntimeError(f"Expected PE32+ (0x20B), got 0x{magic:x}")

    image_base = struct.unpack_from("<Q", pe, opt_off + 24)[0]
    entry_rva = struct.unpack_from("<I", pe, opt_off + 16)[0]
    export_rva, export_size = struct.unpack_from("<II", pe, opt_off + 112)
    pdata_rva, pdata_size = struct.unpack_from("<II", pe, opt_off + 112 + 3 * 8)

    num_sec = struct.unpack_from("<H", pe, pe_off + 6)[0]
    opt_size = struct.unpack_from("<H", pe, pe_off + 20)[0]
    sec_off = pe_off + 24 + opt_size

    sections = []
    for i in range(num_sec):
        s = pe[sec_off + i * 40 : sec_off + (i + 1) * 40]
        name = s[:8].rstrip(b"\x00").decode("latin1")
        vsize, vaddr, rsize, roff, _, _, _, chars = struct.unpack_from("<IIIIIIHI", s, 8)
        sections.append({
            "name": name,
            "vaddr": f"0x{vaddr:08x}",
            "vsize": vsize,
            "roff": f"0x{roff:08x}",
            "rsize": rsize,
            "characteristics": f"0x{chars:08x}",
            "_vaddr": vaddr,
            "_vsize": vsize,
            "_roff": roff,
            "_rsize": rsize,
        })

    def rva_to_off(rva: int) -> int | None:
        for sec in sections:
            if sec["_vaddr"] <= rva < sec["_vaddr"] + max(sec["_vsize"], sec["_rsize"]):
                return sec["_roff"] + (rva - sec["_vaddr"])
        return None

    exports = []
    if export_rva:
        exp_off = rva_to_off(export_rva)
        if exp_off is not None:
            num_funcs, num_names, funcs_rva, names_rva, ords_rva = struct.unpack_from("<IIIII", pe, exp_off + 20)
            names_off = rva_to_off(names_rva)
            ords_off = rva_to_off(ords_rva)
            funcs_off = rva_to_off(funcs_rva)
            for i in range(num_names):
                nrva = struct.unpack_from("<I", pe, names_off + i * 4)[0]
                ord_idx = struct.unpack_from("<H", pe, ords_off + i * 2)[0]
                frva = struct.unpack_from("<I", pe, funcs_off + ord_idx * 4)[0]
                noff = rva_to_off(nrva)
                end = pe.index(b"\x00", noff)
                name = pe[noff:end].decode("latin1")
                exports.append({
                    "name": name,
                    "ordinal": ord_idx,
                    "rva": f"0x{frva:06x}",
                    "va": f"0x{image_base + frva:012x}",
                })

    export_by_rva = {int(e["rva"], 16): e["name"] for e in exports}
    functions = []
    if pdata_rva and pdata_size:
        poff = rva_to_off(pdata_rva)
        if poff is not None:
            for i in range(0, pdata_size, 12):
                b_rva, e_rva, u_rva = struct.unpack_from("<III", pe, poff + i)
                if b_rva == 0 and e_rva == 0:
                    break
                functions.append({
                    "begin_rva": f"0x{b_rva:06x}",
                    "end_rva": f"0x{e_rva:06x}",
                    "begin_va": f"0x{image_base + b_rva:012x}",
                    "end_va": f"0x{image_base + e_rva:012x}",
                    "size": e_rva - b_rva,
                    "unwind_rva": f"0x{u_rva:06x}",
                    "export_name": export_by_rva.get(b_rva),
                })

    clean_sections = [
        {k: v for k, v in sec.items() if not k.startswith("_")}
        for sec in sections
    ]
    return {
        "image_base": f"0x{image_base:x}",
        "entry_rva": f"0x{entry_rva:06x}",
        "size_bytes": len(pe),
        "sections": clean_sections,
        "export_count": len(exports),
        "exports": exports,
        "pdata_function_count": len(functions),
        "functions": functions,
    }


def extract_and_decompile(vst3_path: Path, out_dir: Path) -> None:
    if not vst3_path.is_file():
        raise FileNotFoundError(f"VST3 binary not found: {vst3_path}")

    data = vst3_path.read_bytes()
    out_dir.mkdir(parents=True, exist_ok=True)
    ui_dir = out_dir / "ui"
    asm_dir = out_dir / "asm"
    src_dir = out_dir / "src"
    ui_dir.mkdir(parents=True, exist_ok=True)
    asm_dir.mkdir(parents=True, exist_ok=True)
    src_dir.mkdir(parents=True, exist_ok=True)

    # 1. Extract ui.zip
    z_start, z_end = find_ui_zip(data)
    zbytes = data[z_start:z_end]
    (out_dir / "ui.zip").write_bytes(zbytes)
    with zipfile.ZipFile(io.BytesIO(zbytes)) as zf:
        zf.extractall(ui_dir)
        zip_files = zf.namelist()
    print(f"[extract] ui.zip: 0x{z_start:x}..0x{z_end:x} ({len(zbytes)} bytes), {len(zip_files)} entries -> {ui_dir}")

    # 2. Extract shrub_engine.bin -> shrub_engine.dll
    pe_start, pe_end = find_shrub_engine_pe(data)
    pe_bytes = data[pe_start:pe_end]
    dll_path = out_dir / "shrub_engine.dll"
    dll_path.write_bytes(pe_bytes)
    print(f"[extract] shrub_engine.dll: 0x{pe_start:x}..0x{pe_end:x} ({len(pe_bytes)} bytes) -> {dll_path}")

    # 3. Parse exports + .pdata function table
    meta = parse_pe_metadata(pe_bytes)
    meta["source_vst3"] = str(vst3_path)
    meta["ui_zip_range"] = {"start": f"0x{z_start:x}", "end": f"0x{z_end:x}", "bytes": len(zbytes)}
    meta["engine_pe_range"] = {"start": f"0x{pe_start:x}", "end": f"0x{pe_end:x}", "bytes": len(pe_bytes)}
    meta_path = asm_dir / "functions.json"
    meta_path.write_text(json.dumps(meta, indent=2), encoding="utf-8")
    print(f"[pe] exports={meta['export_count']}, pdata_functions={meta['pdata_function_count']} -> {meta_path}")

    # 4. Disassemble shrub_engine.dll with llvm-objdump (Intel syntax)
    objdump = find_objdump()
    asm_path = asm_dir / "shrub_engine_x64.asm"
    print(f"[disasm] Running {objdump.name} on {dll_path.name}...")
    with asm_path.open("w", encoding="utf-8") as f_out:
        res = subprocess.run(
            [str(objdump), "-d", "--x86-asm-syntax=intel", str(dll_path)],
            stdout=f_out,
            stderr=subprocess.PIPE,
            text=True,
            check=True,
        )
    asm_size = asm_path.stat().st_size
    print(f"[disasm] Wrote {asm_path} ({asm_size} bytes)")


def main() -> None:
    parser = argparse.ArgumentParser(description="Extract and disassemble Shrub.vst3 payloads")
    parser.add_argument("--vst3", type=Path, default=DEFAULT_VST3)
    parser.add_argument("--out", type=Path, default=DEFAULT_OUT)
    args = parser.parse_args()
    extract_and_decompile(args.vst3, args.out)


if __name__ == "__main__":
    main()
