#!/usr/bin/env python3
"""t34-camera-scan.py -- which playback ticks does this demo's camera move at?

Reads a product trace (TF2_NATIVE_TRACE) and reports, for a list of candidate
ticks, the resolved camera pose (map units, pitch/yaw) and the displacement from
the previous candidate. T34: weapon-animation-check.sh asserts that three frames
captured at three ticks are byte-distinct. That assertion is only about the
animation chain if the camera itself moved between those ticks; on a demo whose
camera is still, the frames differ (or fail to differ) because of the WORLD, and
after T29's near-plane fix the camera-adjacent wall fills the frame so they stop
differing at all.

The tool prints one line per candidate tick:

    tick=<t> eye=<x>,<y>,<z> pitch=<p> yaw=<y> src=<demo|held|debug> moved=<0|1>

`moved` is 1 when the pose differs from the previous candidate by more than
MOVE_EPS in any component (yaw compared on the circle). The caller decides what
to do with it; this tool only measures. Exit 0 when it read at least one pose,
1 when it read none.
"""
import re
import sys

MOVE_EPS = 0.5  # map units / degrees

def main(argv):
    if len(argv) < 3:
        sys.stderr.write("usage: t34-camera-scan.py <trace> <tick> [tick...]\n")
        return 2
    path = argv[1]
    ticks = []
    for arg in argv[2:]:
        try:
            ticks.append(int(arg))
        except ValueError:
            sys.stderr.write("not a tick: %s\n" % arg)
            return 2
    poses = {}
    line_re = re.compile(
        r"scene: camera mode=\S+ target=\d+ follows=\d+ holds=\d+ freeze=\d+"
        r" eye=([-0-9.]+),([-0-9.]+),([-0-9.]+) pitch=([-0-9.]+) yaw=([-0-9.]+) src=(\S+)"
    )
    scene_re = re.compile(r"scene: begin tick=(\d+)")
    current = None
    found = 0
    with open(path, "r", errors="replace") as handle:
        for raw in handle:
            line = raw.replace("\r", "").strip()
            m = scene_re.search(line)
            if m:
                current = int(m.group(1))
                continue
            m = line_re.search(line)
            if m and current is not None:
                poses[current] = (
                    float(m.group(1)), float(m.group(2)), float(m.group(3)),
                    float(m.group(4)), float(m.group(5)), m.group(6),
                )
                found += 1
    if not poses:
        sys.stderr.write("no scene: camera pose line in %s\n" % path)
        return 1
    want = [t for t in ticks if t in poses]
    if not want:
        sys.stderr.write(
            "none of %s in the trace (read %d scene ticks, %d..%d)\n"
            % (ticks, len(poses), min(poses), max(poses))
        )
        return 1
    previous = None
    for tick in want:
        x, y, z, pitch, yaw, src = poses[tick]
        moved = 0
        if previous is not None:
            dx = abs(x - previous[0]); dy = abs(y - previous[1]); dz = abs(z - previous[2])
            dp = abs(pitch - previous[3]); dw = abs(yaw - previous[4])
            # yaw is periodic: 350 -> 10 is a 20-degree move, not 340.
            if dw > 180.0:
                dw = 360.0 - dw
            if max(dx, dy, dz) > MOVE_EPS or max(dp, dw) > MOVE_EPS:
                moved = 1
        print("tick=%d eye=%.1f,%.1f,%.1f pitch=%.2f yaw=%.2f src=%s moved=%d"
              % (tick, x, y, z, pitch, yaw, src, moved))
        previous = (x, y, z, pitch, yaw, src)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
