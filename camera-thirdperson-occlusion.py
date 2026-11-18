
"""C4 pre-measurement: how often would a real third-person camera be inside a wall
or lose the player behind one?

Reads the BSP the same way the product does -- lumps 1 (planes), 5 (nodes) and 10
(leaves), with the cluster field at leaf+4, mirroring BspParser::clusterAt in
native/src/bsp_map.cpp:1655 and the leaf parse at :327 -- and answers, for a grid of
standable points on the map, how many placements of a camera 80/140/200 map units
behind the player's head land inside geometry or lose the head behind a wall. It
then asks how far each broken placement must shrink before it becomes valid, which
is the number a collision-aware third-person camera has to be built around.

No product binary, no GPU, no hostlock: this is a read-only measurement of the
map, so it can run while a build owns the lock.

Usage: camera-thirdperson-occlusion.py [map.bsp] [out.json]
Defaults: the pl_upward_f12 install path, and .scratch/c4-occlusion/result.json.
"""
import json, lzma, math, os, struct, sys, time

MAP = sys.argv[1] if len(sys.argv) > 1 else \
    r"D:/SteamLibrary/steamapps/common/Team Fortress 2/tf/maps/pl_upward_f12.bsp"
OUT = sys.argv[2] if len(sys.argv) > 2 else \
    os.path.join(".scratch", "c4-occlusion", "result.json")

data = open(MAP, "rb").read()

def lump(i):
    off, length, ver, fourcc = struct.unpack_from("<IIII", data, 8 + i * 16)
    raw = data[off:off + length]
    if raw[:4] != b"LZMA":
        return raw
    actual = struct.unpack_from("<I", raw, 4)[0]
    props = raw[12:17]
    pb, r = divmod(props[0], 45)
    lp, lc = divmod(r, 9)
    ds = struct.unpack_from("<I", props, 1)[0]
    dec = lzma.LZMADecompressor(format=lzma.FORMAT_RAW,
        filters=[{"id": lzma.FILTER_LZMA1, "lc": lc, "lp": lp, "pb": pb, "dict_size": ds}])
    return dec.decompress(raw[17:], max_length=actual)[:actual]

planes_raw, nodes_raw, leaves_raw = lump(1), lump(5), lump(10)
planes = [struct.unpack_from("<ffff", planes_raw, i * 20) for i in range(len(planes_raw) // 20)]
nodes = [struct.unpack_from("<iii", nodes_raw, i * 32) for i in range(len(nodes_raw) // 32)]
leaves = [struct.unpack_from("<h", leaves_raw, i * 32 + 4)[0] for i in range(len(leaves_raw) // 32)]
models = lump(14)   # LUMP_MODELS is 14, not 0 (lump 0 is the entity string)
mins = [1e30, 1e30, 1e30]
maxs = [-1e30, -1e30, -1e30]
for i in range(len(models) // 48):
    lo = struct.unpack_from("<fff", models, i * 48)
    hi = struct.unpack_from("<fff", models, i * 48 + 12)
    for k in range(3):
        mins[k] = min(mins[k], lo[k])
        maxs[k] = max(maxs[k], hi[k])
mins = tuple(mins); maxs = tuple(maxs)

nplanes, nnodes, nleaves = len(planes), len(nodes), len(leaves)

def cluster_at(x, y, z):
    node, guard = 0, 0
    while node >= 0 and guard < 4096:
        guard += 1
        if node >= nnodes:
            return -1
        plane, c0, c1 = nodes[node]
        if plane < 0 or plane >= nplanes:
            return -1
        nx, ny, nz, d = planes[plane]
        node = c0 if (x * nx + y * ny + z * nz) >= d else c1
    leaf = -node - 1
    if leaf < 0 or leaf >= nleaves:
        return -1
    return leaves[leaf]

spanX = maxs[0] - mins[0]
spanY = maxs[1] - mins[1]
spanZ = maxs[2] - mins[2]
horizontal_span = max(spanX, spanY)
# native/src/native_renderer_world.cpp:94 -- horizontalScale = 1.8 / horizontalSpan,
# so one whole box unit is horizontal_span / 1.8 map units.
map_units_per_box = horizontal_span / 1.8

# ---------------------------------------------------------------- stand points
# Join a grid over the map, drop a vertical probe from the top and keep the first
# place where open space turns into solid going down: that is a floor top, and a
# player can stand on it. The head is 64 units above the floor (TF2 eye height).
def find_stands(step):
    stands = []
    z_start = maxs[2] - 8.0
    z_end = mins[2] + 8.0
    x = mins[0] + step * 0.5
    columns = 0
    while x < maxs[0]:
        y = mins[1] + step * 0.5
        while y < maxs[1]:
            columns += 1
            z = z_start
            floor = None
            in_open = False
            while z > z_end:
                open_here = cluster_at(x, y, z) >= 0
                if open_here:
                    in_open = True
                elif in_open:
                    floor = z + 16.0 * 0.5
                    break
                z -= 16.0
            if floor is not None:
                head = floor + 64.0
                if cluster_at(x, y, head) >= 0 and cluster_at(x, y, head + 36.0) >= 0:
                    stands.append((x, y, floor, head))
            y += step
        x += step
    return stands, columns

step_xy = max(96.0, horizontal_span / 90.0)
t0 = time.time()
stands, columns = find_stands(step_xy)
print("planes=%d nodes=%d leaves=%d bounds=(%.0f %.0f %.0f)-(%.0f %.0f %.0f)"
      % (nplanes, nnodes, nleaves, mins[0], mins[1], mins[2], maxs[0], maxs[1], maxs[2]))
print("grid step=%.1f columns=%d stands=%d elapsed=%.1fs" % (step_xy, columns, len(stands), time.time() - t0))
sys.stdout.flush()

# ------------------------------------------------------ third-person candidates
# The shipped orbit is not a third-person camera: its distance is in box units.
# One box unit is %d map units here, so the shipped minimum of 0.8 box is already
# %.0f map units -- a map-overview camera.
CONFIGS = []
for pitch_deg in (-10.0, 0.0, 15.0, 30.0):
    for distance in (80.0, 140.0, 200.0):
        CONFIGS.append({"pitchDeg": pitch_deg, "distance": distance})
YAWS = [i * (360.0 / 8.0) for i in range(8)]
STEP = 16.0   # along the sight line, map units

def segment_hits_solid(eye, head):
    dx = head[0] - eye[0]; dy = head[1] - eye[1]; dz = head[2] - eye[2]
    length = math.sqrt(dx * dx + dy * dy + dz * dz)
    if length < 1e-3:
        return False
    count = int(length / STEP) + 1
    for i in range(1, count):
        t = i / count
        if cluster_at(eye[0] + dx * t, eye[1] + dy * t, eye[2] + dz * t) < 0:
            return True
    return False

def census(stand_list, yaws, configs, step):
    global STEP
    STEP = step
    rows = []
    for cfg in configs:
        p = math.radians(cfg["pitchDeg"]); d = cfg["distance"]
        eye_solid = blocked = samples = 0
        examples = []
        for (px, py, floor, head_z) in stand_list:
            head = (px, py, head_z)
            for yaw_deg in yaws:
                yaw = math.radians(yaw_deg)
                hd = math.cos(p) * d
                eye = (px + math.cos(yaw) * hd, py + math.sin(yaw) * hd, head_z + math.sin(p) * d)
                samples += 1
                if cluster_at(*eye) < 0:
                    eye_solid += 1
                    if len(examples) < 4:
                        examples.append({"x": round(px, 1), "y": round(py, 1), "z": round(floor, 1),
                                         "yaw": yaw_deg, "why": "eye-in-solid"})
                    continue
                if segment_hits_solid(eye, head):
                    blocked += 1
                    if len(examples) < 8:
                        examples.append({"x": round(px, 1), "y": round(py, 1), "z": round(floor, 1),
                                         "yaw": yaw_deg, "why": "sightline-blocked"})
        if samples == 0:
            print("no samples; aborting")
            sys.exit(2)
        rows.append({"pitchDeg": cfg["pitchDeg"], "distance": d, "samples": samples,
                     "eyeInSolid": eye_solid, "sightlineBlocked": blocked,
                     "eyeInSolidPct": round(100.0 * eye_solid / samples, 2),
                     "blockedPct": round(100.0 * blocked / samples, 2),
                     "selfOccludedPct": round(100.0 * (eye_solid + blocked) / samples, 2),
                     "examples": examples})
        print("pitch=%+5.1f d=%3.0f  eye-solid=%5.2f%%  blocked=%5.2f%%  combined=%5.2f%%"
              % (cfg["pitchDeg"], d, 100.0 * eye_solid / samples, 100.0 * blocked / samples,
                 100.0 * (eye_solid + blocked) / samples))
        sys.stdout.flush()
    return rows

t1 = time.time()
third_person = census(stands, YAWS, CONFIGS, 16.0)
print("third-person census elapsed=%.1fs" % (time.time() - t1))
sys.stdout.flush()

# ------------------------------------------------- shipped orbit, for contrast
orbit_cfgs = []
for d_box in (0.8, 1.65, 12.0):
    for pitch_deg in (14.3, 83.1):   # 0.25 rad and 1.45 rad, the clamp ends
        orbit_cfgs.append({"distance": d_box * map_units_per_box, "pitchDeg": pitch_deg,
                           "boxDistance": d_box})
# ------------------------------------------------- collision shrink, for C4
# For a placement that is inside a wall or loses the player, how far must the
# camera come in before it is valid? Step in from the requested distance in
# 16-unit steps and stop at the first valid placement; 0 means even 16 units
# behind the head is invalid (no third-person camera possible there).
def shrink_census(stand_list, yaws, pitch_deg, distance, step):
    global STEP
    STEP = 16.0
    result_rows = []
    shrunk = []
    impossible = 0
    for (px, py, floor, head_z) in stand_list:
        head = (px, py, head_z)
        for yaw_deg in yaws:
            yaw = math.radians(yaw_deg)
            p = math.radians(pitch_deg)
            bad = False
            eye = None
            for dd in (distance,):
                hd = math.cos(p) * dd
                eye = (px + math.cos(yaw) * hd, py + math.sin(yaw) * hd, head_z + math.sin(p) * dd)
                bad = cluster_at(*eye) < 0 or segment_hits_solid(eye, head)
            if not bad:
                continue
            found = None
            dd = distance
            while dd > 16.0:
                dd -= step
                hd = math.cos(p) * dd
                cand = (px + math.cos(yaw) * hd, py + math.sin(yaw) * hd, head_z + math.sin(p) * dd)
                if cluster_at(*cand) >= 0 and not segment_hits_solid(cand, head):
                    found = dd
                    break
            if found is None:
                impossible += 1
            else:
                shrunk.append(found)
    shrunk.sort()
    def pct(q):
        if not shrunk:
            return None
        return round(shrunk[min(len(shrunk) - 1, int(q * len(shrunk)))], 1)
    total = len(stand_list) * len(yaws)
    return {"pitchDeg": pitch_deg, "requested": distance,
            "placements": total, "neededShrink": len(shrunk), "impossible": impossible,
            "neededShrinkPct": round(100.0 * (len(shrunk) + impossible) / total, 2),
            "impossiblePct": round(100.0 * impossible / total, 2),
            "shrunkDistance": {"min": shrunk[0] if shrunk else None, "p50": pct(0.5),
                               "p90": pct(0.9), "max": shrunk[-1] if shrunk else None}}

t2 = time.time()
shrink_rows = []
for pitch_deg in (-10.0, 0.0, 15.0, 30.0):
    for distance in (80.0, 200.0):
        row = shrink_census(stands, YAWS, pitch_deg, distance, 16.0)
        shrink_rows.append(row)
        print("shrink pitch=%+5.1f d=%3.0f  need=%5.2f%% impossible=%5.2f%% p50=%.0f p90=%.0f"
              % (row["pitchDeg"], row["requested"], row["neededShrinkPct"],
                 row["impossiblePct"], row["shrunkDistance"]["p50"] or 0,
                 row["shrunkDistance"]["p90"] or 0))
        sys.stdout.flush()
print("shrink census elapsed=%.1fs" % (time.time() - t2))

t3 = time.time()
orbit_rows = census(stands, [0.0, 90.0], orbit_cfgs, 128.0)
print("orbit census elapsed=%.1fs" % (time.time() - t2))

result = {
    "map": MAP.split("/")[-1],
    "lumps": {"planes": nplanes, "nodes": nnodes, "leaves": nleaves},
    "bounds": {"mins": list(mins), "maxs": list(maxs), "span": [spanX, spanY, spanZ]},
    "box": {"horizontalSpan": horizontal_span, "mapUnitsPerBoxUnit": map_units_per_box},
    "standSampling": {"gridStep": step_xy, "columns": columns, "stands": len(stands),
                      "headAboveFloor": 64.0},
    "yaws": YAWS,
    "thirdPerson": third_person,
    "shrink": shrink_rows,
    "shippedOrbit": orbit_rows,
    "note": "clusterAt is point-in-leaf on the BSP tree; a sight line is called "
            "blocked when any sample every 16 units between the eye and the head "
            "lands in a solid leaf. Eye height 64 units above the sampled floor.",
}
os.makedirs(os.path.dirname(OUT) or ".", exist_ok=True)
open(OUT, "w").write(json.dumps(result, indent=1))
print("WROTE", OUT)