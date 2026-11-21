#!/usr/bin/env python3
"""task-37 Half A: read the leaf ambient lumps for real, not from the header sizes.

Every lump here is LZMA-packed on disk ("LZMA" + u32 uncompressed + u32 packed +
5 props bytes + payload), so the lump's on-disk length says nothing about the
record count; only the uncompressed size does. The first version of this survey
divided the compressed length and concluded "no usable page" on a map that has
one -- that is the exact class of error this script now refuses to repeat: it
decompresses first, then checks the modulo.
"""
import json
import lzma
import os
import struct
import sys

LEAF = 10
IDX = {"ldr": 52, "hdr": 51}
DAT = {"ldr": 56, "hdr": 55}
SAMPLE_BYTES = 28
INDEX_BYTES = 4


def unpack_lump(blob, off, length):
    if length <= 0:
        return b''
    raw = blob[off:off + length]
    if raw[:4] != b'LZMA':
        return raw
    usize, packed = struct.unpack_from('<II', raw, 4)
    if packed != length - 17:
        raise RuntimeError('packed size mismatch %d vs %d' % (packed, length - 17))
    props = raw[12:17]
    dec = lzma.LZMADecompressor(format=lzma.FORMAT_RAW, filters=[
        {'id': lzma.FILTER_LZMA1, 'dict_size': struct.unpack_from('<I', props, 1)[0],
         'lc': props[0] % 9, 'lp': (props[0] // 9) % 5, 'pb': props[0] // 45}])
    out = dec.decompress(raw[17:], max_length=usize)
    if len(out) != usize:
        raise RuntimeError('decode %d != %d' % (len(out), usize))
    return out


def cube(rec):
    """6 x ColorRGBExp32 -> linear float rgb, the arithmetic of bsp_map.cpp:241-244."""
    out = []
    for i in range(6):
        m = rec[i * 4:i * 4 + 4]
        e = struct.unpack('<b', m[3:4])[0]
        scale = (2.0 ** e) / 255.0
        out.append([m[0] * scale, m[1] * scale, m[2] * scale])
    return out


def survey(path, dump_leaves=0):
    blob = open(path, 'rb').read()
    lumps = [struct.unpack_from('<ii', blob, 8 + i * 16) for i in range(64)]
    leaves = unpack_lump(blob, *lumps[LEAF])
    rec = {"map": os.path.basename(path), "bytes": len(blob), "leaves": len(leaves) // 32,
           "leafBytesRaw": lumps[LEAF][1]}
    pages = {}
    for tag in ("ldr", "hdr"):
        i = unpack_lump(blob, *lumps[IDX[tag]])
        d = unpack_lump(blob, *lumps[DAT[tag]])
        pages[tag] = {"indexBytes": len(i), "dataBytes": len(d),
                      "entries": len(i) // INDEX_BYTES if len(i) % INDEX_BYTES == 0 else -1,
                      "samples": len(d) // SAMPLE_BYTES if len(d) % SAMPLE_BYTES == 0 else -1}
        rec[tag] = pages[tag]
    # the page rule this task has to state out loud: prefer LDR when it is present,
    # exactly as bsp_map.cpp:292-305 picks the lightmap page (the brief asks for the
    # opposite; the report has to say which rule the code follows and why).
    page = None
    for tag in ("ldr", "hdr"):
        p = pages[tag]
        if p["entries"] == rec["leaves"] and p["samples"] > 0:
            page = tag
            break
    rec["usablePage"] = page or "none"
    if page is None:
        return rec
    i = unpack_lump(blob, *lumps[IDX[page]])
    d = unpack_lump(blob, *lumps[DAT[page]])
    counts = []
    empty = 0
    out_of_range = 0
    zsum = [0, 0]
    for k in range(rec["leaves"]):
        n, first = struct.unpack_from('<HH', i, k * INDEX_BYTES)
        counts.append(n)
        if n == 0:
            empty += 1
            continue
        if (first + n) * SAMPLE_BYTES > len(d):
            out_of_range += 1
            continue
        for s in range(n):
            at = (first + s) * SAMPLE_BYTES
            zsum[0] += d[at + 26]
            zsum[1] += 1
    rec["leavesWithSamples"] = rec["leaves"] - empty
    rec["leavesEmpty"] = empty
    rec["samplesOutOfRange"] = out_of_range
    rec["samplesPerLeafMax"] = max(counts) if counts else 0
    histogram = {}
    for c in counts:
        histogram[str(c)] = histogram.get(str(c), 0) + 1
    rec["samplesPerLeaf"] = dict(sorted(histogram.items(), key=lambda kv: int(kv[0])))
    rec["meanSampleZ"] = round(zsum[0] / zsum[1], 2) if zsum[1] else -1
    if dump_leaves:
        rows = []
        for k in range(min(dump_leaves, rec["leaves"])):
            n, first = struct.unpack_from('<HH', i, k * INDEX_BYTES)
            if n == 0:
                rows.append({"leaf": k, "samples": 0})
                continue
            at = first * SAMPLE_BYTES
            rows.append({"leaf": k, "samples": n, "xyz": list(d[at + 24:at + 27]),
                         "cube": [[round(v, 6) for v in c] for c in cube(d[at:at + 24])]})
        rec["dumped"] = rows
    return rec


def main(argv):
    if '--maps' in argv:
        root = argv[argv.index('--maps') + 1]
        rows = [survey(os.path.join(root, n)) for n in sorted(os.listdir(root)) if n.endswith('.bsp')]
        usable = [r for r in rows if r.get('usablePage') != 'none']
        summary = {"maps": len(rows), "usable": len(usable),
                   "ldr": sum(1 for r in usable if r['usablePage'] == 'ldr'),
                   "hdrOnly": sum(1 for r in usable if r['usablePage'] == 'hdr'),
                   "none": [r['map'] for r in rows if r.get('usablePage') == 'none'],
                   "samplesPerLeafMax": max([r['samplesPerLeafMax'] for r in usable], default=0),
                   "leavesEmptyTotal": sum(r['leavesEmpty'] for r in usable),
                   "leavesTotal": sum(r['leaves'] for r in usable)}
        print(json.dumps(summary))
        for r in rows:
            print(json.dumps(r))
        return 0
    if len(argv) < 2:
        print('usage: leaf_ambient_survey.py <map.bsp> [--dump N] | --maps <dir>')
        return 2
    n = int(argv[argv.index('--dump') + 1]) if '--dump' in argv else 0
    print(json.dumps(survey(argv[1], n), indent=1))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
