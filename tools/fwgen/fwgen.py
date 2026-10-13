#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# FM1 DECK — tools/fwgen/fwgen.py
# Generates firmware headers (fonts, icons, tables, samples) for Felucca, JIANT, SLOOP, Hortator
# without touching upstream submodules.

import argparse
import hashlib
import io
import os
import shutil
import subprocess
import sys
import tarfile
from pathlib import Path

VALID_FIRMWARES = ("felucca", "jiant", "sloop", "hortator")


def hortator_keymap(src_dir: Path, gen_dir: Path) -> None:
    """tests/sim_host.h includes sim_keymap.h: FM1_KEYMAP cut out of firmware/hal/fm1_input.h, as the upstream's
    tools/build_sim.sh does it with awk."""
    lines, out = (src_dir / "firmware" / "hal" / "fm1_input.h").read_text(encoding="utf-8").splitlines(), []
    for line in lines:
        if out or "static const int8_t FM1_KEYMAP" in line:
            out.append(line)
            if line.startswith("};"):
                break
    if not out or not out[-1].startswith("};"):
        raise SystemExit("[fwgen] FM1_KEYMAP not found in firmware/hal/fm1_input.h")
    (src_dir / "build" / "gen" / "sim_keymap.h").write_text("\n".join(out) + "\n", encoding="utf-8")


# headers a firmware's own build makes outside generate()
EXTRA_GEN = {"hortator": hortator_keymap}


def repo_root() -> Path:
    return Path(__file__).resolve().parents[2]


def run_in_wsl(args: argparse.Namespace) -> int:
    root = repo_root()
    cmd_wslpath = ["wsl", "-d", "Ubuntu", "--", "wslpath", "-u", str(root)]
    res = subprocess.run(cmd_wslpath, capture_output=True, text=True, check=True)
    wsl_root = res.stdout.strip()

    fw_args = [args.target]
    inner_cmd = f"cd '{wsl_root}' && ~/mwgen-venv/bin/python tools/fwgen/fwgen.py {' '.join(fw_args)}"
    wsl_cmd = ["wsl", "-d", "Ubuntu", "--", "bash", "-c", inner_cmd]
    print(f"[fwgen] Running in WSL: {' '.join(wsl_cmd)}")
    return subprocess.run(wsl_cmd).returncode


def generate_firmware(fw_id: str, root: Path) -> None:
    print(f"\n[fwgen] === Generating headers for {fw_id} ===")
    upstream_dir = root / "upstream" / fw_id
    if not (upstream_dir / ".git").exists() and not (upstream_dir / "firmware").exists():
        raise SystemExit(f"[fwgen] Upstream dir not found or submodule empty: {upstream_dir}")

    build_fwgen_dir = root / "build" / "fwgen" / fw_id
    src_dir = build_fwgen_dir / "src"
    gen_dir = build_fwgen_dir / "gen"

    if src_dir.exists():
        shutil.rmtree(src_dir)
    src_dir.mkdir(parents=True, exist_ok=True)
    if gen_dir.exists():
        shutil.rmtree(gen_dir)
    gen_dir.mkdir(parents=True, exist_ok=True)

    print(f"[fwgen] Exporting git archive from {upstream_dir}...")
    git_cmd = ["git", "-C", str(upstream_dir), "archive", "HEAD"]
    res = subprocess.run(git_cmd, stdout=subprocess.PIPE, check=True)

    with tarfile.open(fileobj=io.BytesIO(res.stdout)) as tar:
        tar.extractall(path=src_dir)

    print(f"[fwgen] Running generator in {src_dir}...")
    # Upstream generators call sys.executable for sub-scripts, so use sys.executable
    gen_cmd = [
        sys.executable,
        "-c",
        "import sys; sys.path.insert(0, 'tools'); import build; build.generate()",
    ]
    proc = subprocess.run(gen_cmd, cwd=src_dir)
    if proc.returncode != 0:
        raise SystemExit(f"[fwgen] Generator failed for {fw_id} with exit code {proc.returncode}")

    src_gen_dir = src_dir / "build" / "gen"
    if fw_id in EXTRA_GEN and src_gen_dir.exists():
        EXTRA_GEN[fw_id](src_dir, gen_dir)
    if not src_gen_dir.exists():
        raise SystemExit(f"[fwgen] Expected generated directory not found: {src_gen_dir}")

    headers = sorted(src_gen_dir.glob("*.h"))
    if not headers:
        raise SystemExit(f"[fwgen] No headers produced in {src_gen_dir}")

    # Copy to gen_dir
    sha256_lines = []
    for h in headers:
        dst = gen_dir / h.name
        content = h.read_bytes()
        dst.write_bytes(content)
        file_hash = hashlib.sha256(content).hexdigest()
        sha256_lines.append(f"{file_hash}  {h.name}")
        print(f"  {h.name} ({len(content)} bytes, sha256: {file_hash[:12]}...)")

    sha256_file = build_fwgen_dir / "gen.sha256"
    sha256_file.write_text("\n".join(sha256_lines) + "\n", encoding="utf-8")
    print(f"[fwgen] Wrote {sha256_file}")


def main() -> None:
    parser = argparse.ArgumentParser(description="FM1 DECK firmware header generator")
    parser.add_argument("target", help="Firmware ID (felucca, jiant, sloop, hortator) or 'all'")
    parser.add_argument("--wsl", action="store_true", help="Run via WSL Ubuntu and ~/mwgen-venv")
    args = parser.parse_args()

    if args.wsl:
        sys.exit(run_in_wsl(args))

    targets = VALID_FIRMWARES if args.target == "all" else [args.target]
    for t in targets:
        if t not in VALID_FIRMWARES:
            raise SystemExit(f"[fwgen] Unknown firmware: {t}. Expected one of {VALID_FIRMWARES} or 'all'")

    root = repo_root()
    for t in targets:
        generate_firmware(t, root)

    print("\n[fwgen] All done successfully!")


if __name__ == "__main__":
    main()
