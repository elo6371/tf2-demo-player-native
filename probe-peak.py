#!/usr/bin/env python3
"""probe-peak.py -- run one demo under entity_protocol_probe and report wall time
plus peak working set, so concurrency can be chosen from a measurement instead of
a guess.

Usage: python probe-peak.py <demo> [<demo> ...]
"""
from __future__ import annotations

import ctypes
import ctypes.wintypes as w
import subprocess
import sys
import time
from pathlib import Path

PROCESS_QUERY_INFORMATION = 0x0400
PROCESS_VM_READ = 0x0010


class PMC(ctypes.Structure):
    _fields_ = [
        ("cb", w.DWORD), ("PageFaultCount", w.DWORD),
        ("PeakWorkingSetSize", ctypes.c_size_t), ("WorkingSetSize", ctypes.c_size_t),
        ("QuotaPeakPagedPoolUsage", ctypes.c_size_t), ("QuotaPagedPoolUsage", ctypes.c_size_t),
        ("QuotaPeakNonPagedPoolUsage", ctypes.c_size_t), ("QuotaNonPagedPoolUsage", ctypes.c_size_t),
        ("PagefileUsage", ctypes.c_size_t), ("PeakPagefileUsage", ctypes.c_size_t),
    ]


def run(exe: Path, demo: Path) -> None:
    t0 = time.perf_counter()
    proc = subprocess.Popen([str(exe), str(demo)], stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, text=True, encoding="utf-8",
                            errors="replace")
    handle = ctypes.windll.kernel32.OpenProcess(
        PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, False, proc.pid)
    peak = 0
    while proc.poll() is None:
        info = PMC()
        info.cb = ctypes.sizeof(PMC)
        if handle and ctypes.windll.psapi.GetProcessMemoryInfo(
                handle, ctypes.byref(info), info.cb):
            peak = max(peak, info.PeakWorkingSetSize)
        time.sleep(0.05)
    out = proc.stdout.read()
    if handle:
        ctypes.windll.kernel32.CloseHandle(handle)
    elapsed = time.perf_counter() - t0

    size_mb = demo.stat().st_size / 2**20
    print(f"\n{demo.name}  size={size_mb:.1f} MB  seconds={elapsed:.1f}  "
          f"peak_rss={peak / 2**20:.0f} MB  throughput={size_mb / max(elapsed, 1e-9):.2f} MB/s")
    keep = ("packets_scanned", "packet_entities", "entity_failures",
            "malformed_packets", "unknown_message", "message_types_seen",
            "instance_baselines", "string_table_user_data_max",
            "recording", "header", "history_gap", "delta_base_unavailable",
            "enter", "preserve", "leave", "delete")
    for line in out.splitlines():
        if line.startswith(keep):
            print("    " + line)
    sys.stdout.flush()


def main() -> int:
    if len(sys.argv) < 2:
        print(__doc__, file=sys.stderr)
        return 2
    exe = Path(__file__).resolve().parent / "native" / "build-nmake" / "entity_protocol_probe.exe"
    if not exe.is_file():
        print(f"FATAL: probe not found: {exe}", file=sys.stderr)
        return 2
    for arg in sys.argv[1:]:
        run(exe, Path(arg))
    return 0


if __name__ == "__main__":
    sys.exit(main())
