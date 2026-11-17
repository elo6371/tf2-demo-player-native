"""Check Source metric geometry against the product's uploaded frame matrix.

Usage: python tools/n17_camera_metric.py <trace> [--world-fov 90]
       python tools/n17_camera_metric.py --selftest
This checks a geometric invariant, not a TF2 screenshot or GPU vertex readback.
"""
import argparse
import math
import re


def add(a, b):
    return tuple(x + y for x, y in zip(a, b))


def mul(a, value):
    return tuple(x * value for x in a)


def cross(a, b):
    return (a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0])


def normalize(a):
    return mul(a, 1.0 / math.sqrt(sum(x*x for x in a)))


def judge(matrix, scale, center, min_z, eye, pitch, yaw, width, height, fov):
    pitch, yaw = map(math.radians, (pitch, yaw))
    forward = (math.cos(pitch)*math.cos(yaw), math.cos(pitch)*math.sin(yaw), -math.sin(pitch))
    right = normalize(cross((0, 0, 1), forward))
    up = cross(forward, right)
    origin = add(eye, mul(forward, 200.0))

    def screen(point):
        box = ((point[0]-center[0])*scale, (point[1]-center[1])*scale,
               (point[2]-min_z)*scale+0.1, 1.0)
        clip = [sum(box[row]*matrix[row][col] for row in range(4)) for col in range(4)]
        if not all(map(math.isfinite, clip)) or clip[3] <= 0:
            raise ValueError("reference geometry is behind the uploaded camera")
        return ((clip[0]/clip[3]+1)*width/2, (1-clip[1]/clip[3])*height/2)

    projected = [screen(point) for point in (origin, add(origin, mul(right, 10)), add(origin, mul(up, 10)))]
    lengths = [math.dist(projected[0], projected[i]) for i in (1, 2)]
    expected = height/2 / (math.tan(math.radians(fov/2))*0.75) * (10/200)
    center_error = math.dist(projected[0], (width/2, height/2))
    # Camera trace eye/angles are rounded to 0.1u/0.01 degrees, so allow 0.25px.
    passed = center_error < 0.25 and all(abs(value-expected) < 0.25 for value in lengths)
    return passed, lengths, expected, center_error


def selftest():
    # Camera at the origin, looking along +X; right=+Y, up=+Z.
    # The upload consumes normalized coordinates, including the Z bias of .1.
    matrix = [[0, 0, 1, 1], [1, 0, 0, 0], [0, 1, 0, 0], [0, -.1, 0, 0]]
    args = (1.0, (0, 0), 0.0, (0, 0, 0), 0.0, 0.0, 600, 600, math.degrees(2*math.atan(4/3)))
    good = judge(matrix, *args)[0]
    mutant = [row[:] for row in matrix]
    mutant[2][1] *= 1.351815
    mutant[3][1] *= 1.351815
    killed = not judge(mutant, *args)[0]
    print(f"CAMERA-METRIC-SELFTEST good={int(good)} stretchKilled={int(killed)}")
    return 0 if good and killed else 1


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("trace", nargs="?")
    parser.add_argument("--world-fov", type=float, default=90.0)
    parser.add_argument("--selftest", action="store_true")
    args = parser.parse_args()
    if args.selftest:
        return selftest()
    if not args.trace or not 0 < args.world_fov < 180:
        parser.error("supply a trace and a finite world FOV between 0 and 180")
    transform = eye = angles = dimensions = matrix_camera = None
    rows = {}
    with open(args.trace, encoding="utf-8", errors="replace") as stream:
        for line in stream:
            match = re.search(r"world metric scale=(\S+) center=(\S+),(\S+) minZ=(\S+) spanZ=(\S+)", line)
            if match:
                transform = tuple(map(float, match.groups()))
                rows = {}
                matrix_camera = (eye, angles)
            match = re.search(r"world metric-mvp row=([0-3]) values=(\S+)", line)
            if match:
                rows[int(match[1])] = list(map(float, match[2].split(",")))
            match = re.search(r"scene: camera .* eye=(\S+),(\S+),(\S+) pitch=(\S+) yaw=(\S+)", line)
            if match:
                values = tuple(map(float, match.groups()))
                eye, angles = values[:3], values[3:]
            match = re.search(r"capture: frame width=(\d+) height=(\d+)", line)
            if match:
                dimensions = tuple(map(int, match.groups()))
    if transform is None or matrix_camera is None or dimensions is None or len(rows) != 4:
        raise ValueError("missing uploaded matrix, world transform, actual camera or capture dimensions")
    eye, angles = matrix_camera
    if eye is None or angles is None:
        raise ValueError("no actual camera reading precedes the uploaded matrix")
    if any(len(row) != 4 for row in rows.values()):
        raise ValueError("uploaded matrix must contain four complete rows")
    passed, lengths, expected, center_error = judge(
        [rows[i] for i in range(4)], transform[0], transform[1:3], transform[3],
        eye, *angles, *dimensions, args.world_fov)
    print(f"CAMERA-METRIC={'PASS' if passed else 'FAIL'} rightPx={lengths[0]:.6f} "
          f"upPx={lengths[1]:.6f} expectedPx={expected:.6f} centerErrorPx={center_error:.6f}")
    return 0 if passed else 1


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, ZeroDivisionError) as error:
        print(f"CAMERA-METRIC=ERROR {error}")
        raise SystemExit(2)
