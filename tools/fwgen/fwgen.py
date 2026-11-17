#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# FM1 DECK — tools/fwgen/fwgen.py
# Generates firmware headers (fonts, icons, tables, samples) for all 27 built-in firmwares
# without touching upstream submodules/clones.

import argparse
import hashlib
import io
import re
import shutil
import subprocess
import sys
import tarfile
from pathlib import Path

FELUCCA_STYLE = ("felucca", "jiant", "sloop", "hortator", "fmini", "clipper", "coralflower", "aisling", "fimba")
FM1BASE_STYLE = ("amb", "blop", "choppah", "echofield", "frasil", "jamtrack", "loafbreaker", "loom", "mutable", "atek303")
PREGEN_STYLE = ("obervave", "s1vave", "shineon", "arctic", "demake", "kwasub")
SPECIAL_STYLE = ("guru600", "ravel")
VALID_FIRMWARES = FELUCCA_STYLE + FM1BASE_STYLE + PREGEN_STYLE + SPECIAL_STYLE


def hortator_keymap(src_dir: Path, gen_dir: Path) -> None:
    """tests/sim_host.h includes sim_keymap.h: FM1_KEYMAP cut out of firmware/hal/fm1_input.h."""
    lines, out = (src_dir / "firmware" / "hal" / "fm1_input.h").read_text(encoding="utf-8").splitlines(), []
    for line in lines:
        if out or "static const int8_t FM1_KEYMAP" in line:
            out.append(line)
            if line.startswith("};"):
                break
    if not out or not out[-1].startswith("};"):
        raise SystemExit("[fwgen] FM1_KEYMAP not found in firmware/hal/fm1_input.h")
    (src_dir / "build" / "gen" / "sim_keymap.h").write_text("\n".join(out) + "\n", encoding="utf-8")


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


def export_git_tree(src_repo: Path, dst_dir: Path) -> None:
    res = subprocess.run(["git", "-C", str(src_repo), "archive", "HEAD"], stdout=subprocess.PIPE, check=True)
    with tarfile.open(fileobj=io.BytesIO(res.stdout)) as tar:
        tar.extractall(path=dst_dir)
    gm = src_repo / ".gitmodules"
    if gm.exists():
        for sp in re.findall(r'^\s*path\s*=\s*(.+?)\s*$', gm.read_text(encoding="utf-8", errors="ignore"), re.M):
            sd = src_repo / sp
            if (sd / ".git").exists() or (sd / ".git").is_file():
                (dst_dir / sp).mkdir(parents=True, exist_ok=True)
                export_git_tree(sd, dst_dir / sp)


def generate_firmware(fw_id: str, root: Path) -> None:
    print(f"\n[fwgen] === Generating headers for {fw_id} ===")
    upstream_dir = root / "upstream" / fw_id
    if not upstream_dir.exists():
        raise SystemExit(f"[fwgen] Upstream dir not found: {upstream_dir}")

    build_fwgen_dir = root / "build" / "fwgen" / fw_id
    src_dir = build_fwgen_dir / "src"
    gen_dir = build_fwgen_dir / "gen"

    if src_dir.exists():
        shutil.rmtree(src_dir)
    src_dir.mkdir(parents=True, exist_ok=True)
    if gen_dir.exists():
        shutil.rmtree(gen_dir)
    gen_dir.mkdir(parents=True, exist_ok=True)

    # Pre-generated firmwares: copy committed firmware/gen/*.h directly
    if fw_id in PREGEN_STYLE:
        pre_dir = upstream_dir / ("source/firmware/gen" if fw_id == "kwasub" else "firmware/gen")
        headers = sorted(pre_dir.glob("*.h"))
        if not headers:
            raise SystemExit(f"[fwgen] No pre-generated headers in {pre_dir}")
        _copy_headers_and_hash(headers, gen_dir, build_fwgen_dir)
        return

    print(f"[fwgen] Exporting git archive from {upstream_dir}...")
    export_git_tree(upstream_dir, src_dir)

    if fw_id == "guru600":
        work_dir = src_dir / "firmware"
        proc = subprocess.run(
            [sys.executable, "-c", "import sys; sys.path.insert(0, 'tools'); import build; build.generate()"],
            cwd=work_dir,
        )
        if proc.returncode != 0:
            raise SystemExit(f"[fwgen] Generator failed for {fw_id} with exit code {proc.returncode}")
        src_gen_dir = work_dir / "build" / "gen"
    elif fw_id == "ravel":
        src_gen_dir = src_dir / "build" / "gen"
        src_gen_dir.mkdir(parents=True, exist_ok=True)
        proc = subprocess.run(
            [sys.executable, str(src_dir / "tools" / "gen_rv_font.py"), str(src_gen_dir / "rv_font.h")],
            cwd=src_dir,
        )
        if proc.returncode != 0:
            raise SystemExit(f"[fwgen] Generator failed for {fw_id} with exit code {proc.returncode}")
    elif fw_id in FM1BASE_STYLE:
        if fw_id == "atek303":
            app_dir = src_dir / "atek303" / "app"
            base_dir = src_dir / "atek303" / "base"
            if base_dir.is_symlink() or base_dir.exists():
                if base_dir.is_dir() and not base_dir.is_symlink():
                    shutil.rmtree(base_dir)
                else:
                    base_dir.unlink()
            base_dir.mkdir(parents=True, exist_ok=True)
            export_git_tree(root / "upstream" / "amb" / "base", base_dir)
        elif fw_id == "blop":
            app_dir = src_dir
            base_dir = src_dir / "fm1-base"
        else:
            app_dir = src_dir / "app"
            base_dir = src_dir / "base"

        src_gen_dir = app_dir / "build" / "gen"
        snippet = (
            "import sys, os; from pathlib import Path; "
            f"sys.path.insert(0, {str(base_dir / 'tools')!r}); "
            "import build; "
            f"build.APP = Path({str(app_dir)!r}).resolve(); "
            "build.CONF = build.read_conf(build.APP / 'fm1.toml'); "
            "build.NAME = build.CONF.get('name', build.APP.name).lower(); "
            "build.OUT = build.APP / 'build'; "
            "build.GEN = build.OUT / 'gen'; "
            "build.ROOT = Path(os.path.commonpath([build.SRC, build.APP])); "
            "build.generate()"
        )
        proc = subprocess.run([sys.executable, "-c", snippet], cwd=app_dir)
        if proc.returncode != 0:
            raise SystemExit(f"[fwgen] fm1-base generator failed for {fw_id} with exit code {proc.returncode}")
    else:
        proc = subprocess.run(
            [sys.executable, "-c", "import sys; sys.path.insert(0, 'tools'); import build; build.generate()"],
            cwd=src_dir,
        )
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
    _copy_headers_and_hash(headers, gen_dir, build_fwgen_dir)


def _copy_headers_and_hash(headers: list[Path], gen_dir: Path, build_fwgen_dir: Path) -> None:
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
    parser.add_argument("target", help=f"Firmware ID ({', '.join(VALID_FIRMWARES)}) or 'all'")
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
