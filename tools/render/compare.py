#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
import sys
import numpy as np

def compare_raw(file1, file2):
    data1 = np.fromfile(file1, dtype=np.float32)
    data2 = np.fromfile(file2, dtype=np.float32)
    
    if len(data1) != len(data2):
        print(f"Length mismatch: {file1} has {len(data1)} floats, {file2} has {len(data2)} floats")
        min_len = min(len(data1), len(data2))
        data1 = data1[:min_len]
        data2 = data2[:min_len]
    
    # Interleaved stereo: L is even indices, R is odd indices
    l1, r1 = data1[0::2], data1[1::2]
    l2, r2 = data2[0::2], data2[1::2]
    
    diff_l = np.abs(l1 - l2)
    diff_r = np.abs(r1 - r2)
    
    max_l = float(np.max(diff_l)) if len(diff_l) > 0 else 0.0
    max_r = float(np.max(diff_r)) if len(diff_r) > 0 else 0.0
    
    rms_l = float(np.sqrt(np.mean((l1 - l2) ** 2))) if len(diff_l) > 0 else 0.0
    rms_r = float(np.sqrt(np.mean((r1 - r2) ** 2))) if len(diff_r) > 0 else 0.0
    
    diff_frames = np.where((diff_l > 0) | (diff_r > 0))[0]
    
    print(f"Frames: {len(l1)}")
    print(f"Left channel  - Max abs error: {max_l:.6e}, RMS error: {rms_l:.6e}")
    print(f"Right channel - Max abs error: {max_r:.6e}, RMS error: {rms_r:.6e}")
    
    if len(diff_frames) > 0:
        first_diff = int(diff_frames[0])
        print(f"First differing frame: {first_diff} (L1={l1[first_diff]:.6f}, L2={l2[first_diff]:.6f}, R1={r1[first_diff]:.6f}, R2={r2[first_diff]:.6f})")
        print(f"Total differing frames: {len(diff_frames)} / {len(l1)}")
        return False
    else:
        print("BIT-EXACT MATCH! (0 differing frames)")
        return True

if __name__ == "__main__":
    if len(sys.argv) < 3:
        print("Usage: compare.py <native.raw> <wasm.raw>")
        sys.exit(1)
    ok = compare_raw(sys.argv[1], sys.argv[2])
    sys.exit(0 if ok else 1)
