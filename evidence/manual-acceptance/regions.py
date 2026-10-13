#!/usr/bin/env python3
"""Region decomposition for the T2 matrix frames.

The plan asks for two numbers per cell: the ViewModel region difference and the
world region difference. A whole-frame percentage cannot answer that -- a frame
that changed only in the world and a frame that changed only in the ViewModel
have the same whole-frame number.

Every cell is compared against the same reference frame (f90/left/tick2000, the
compiled defaults), so the two shares are comparable across cells:

  vm_share    -- differing share inside the ViewModel composite region
  world_share -- differing share inside the three strips outside it

A frame whose input only reaches the ViewModel has vm_share > 0 and
world_share == 0.0000. That is the signature the plan is after, and it is the
same signature the ViewModel-material gate pins for its own pixel evidence.
"""
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.abspath(__file__))
REF = os.path.join(ROOT, "r2-f90-left-t2000.bmp")
DIFF = os.path.join(ROOT, "..", "..", "bmp-pixel-diff.py")

VM_REGION = "420,250,424,431"
WORLD_REGIONS = ["0,0,420,681", "844,0,420,681", "0,0,1264,250"]


def share(bmp, region):
    out = subprocess.run(
        [sys.executable, DIFF, "--min-delta", "1", REF, bmp, "--region", region],
        capture_output=True, text=True).stdout
    for line in out.splitlines():
        if line.startswith("BMP-DIFF region="):
            for token in line.split():
                if token.startswith("share="):
                    return float(token.split("=", 1)[1])
    return None


def main():
    names = sorted(
        f[:-4] for f in os.listdir(ROOT)
        if f.startswith("r2-f") and f.endswith(".bmp"))
    print("cell,vm_share,world_share_max,vm_differing,world_differing")
    for name in names:
        bmp = os.path.join(ROOT, name + ".bmp")
        vm = share(bmp, VM_REGION)
        world = max(
            (share(bmp, r) for r in WORLD_REGIONS),
            key=lambda v: v if v is not None else -1.0)
        vm_pct = "n/a" if vm is None else "%.4f" % vm
        world_pct = "n/a" if world is None else "%.4f" % world
        print("%s,%s,%s" % (name, vm_pct, world_pct))


if __name__ == "__main__":
    main()
