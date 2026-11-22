#!/usr/bin/env python3
# T105 / task-37 Half B: the OTHER half of the scale-factor argument.
#
# docs/LEAF-AMBIENT-GPU-2026-11-21.md argues that the leaf ambient lump stores
# linear radiance two to three orders of magnitude below what the lightmap lump
# on the same map stores, so a baked leaf cube cannot be handed to the shader
# unscaled. The leaf side of that comparison is already committed
# (evidence/leaf-ambient/real-cp_process_f12.txt: largest face component
# 0.004626). This script produces the lightmap side from the same .bsp files, at
# the same decode, so neither number is quoted from a document.
#
# Decode is the engine's own (native/src/bsp_map.cpp:255 decodeColorRgbExp32):
#     scale = 2**exponent / 255 ; channel = byte * scale
# with the exponent byte read as a SIGNED 8-bit value (exponent - 128).
#
# Prints one JSON line per map:
#   ldr / hdr   {page, luxels, maxChannel, maxExponent, maxByte, p99Channel,
#                p50Channel, meanChannel}
#   usedPage    which of the two the renderer serves (LDR when present, HDR as
#               the fallback -- native/src/bsp_map.cpp:292-305)
#
# numpy is used because a 25 MB map holds millions of luxels; the pure-Python
# equivalent takes minutes per map.
#
# usage: python lightmap-page-max.py <map.bsp> [<map.bsp> ...]
import json, os, struct, sys
import lzma

import numpy as np

LUMP_LIGHTING = 8
LUMP_LIGHTING_HDR = 53

def unpack_lump(blob, off, length):
    """LZMA-compressed lump -> raw bytes, or the bytes as stored."""
    if length <= 0:
        return b''
    raw = blob[off:off + length]
    if raw[:4] != b'LZMA':
        return raw
    usize, packed = struct.unpack_from('<II', raw, 4)
    if packed != length - 17:
        raise RuntimeError('packed %d != lump %d' % (packed, length - 17))
    props = raw[12:17]
    dec = lzma.LZMADecompressor(format=lzma.FORMAT_RAW, filters=[
        {'id': lzma.FILTER_LZMA1,
         'dict_size': struct.unpack_from('<I', props, 1)[0],
         'lc': props[0] % 9, 'lp': (props[0] // 9) % 5, 'pb': props[0] // 45}])
    out = dec.decompress(raw[17:], max_length=usize)
    if len(out) != usize:
        raise RuntimeError('decode %d != %d' % (len(out), usize))
    return out

def page_stats(data, label):
    if not data:
        return None
    arr = np.frombuffer(data, dtype=np.uint8)
    n = arr.size // 4
    arr = arr[:n * 4].reshape(n, 4)
    ex = arr[:, 3].astype(np.int16)
    ex = np.where(ex > 127, ex - 256, ex).astype(np.float64)
    scale = np.ldexp(1.0, ex.astype(np.int32)) / 255.0     # 2**exponent / 255
    out = {"page": label, "luxels": int(n)}
    # Per-channel decode: only R/G/B (byte 3 is the exponent, not a channel).
    chans = [arr[:, c].astype(np.float64) * scale for c in range(3)]
    flat = np.concatenate(chans)
    out["maxChannel"] = round(float(flat.max()), 6)
    out["p99Channel"] = round(float(np.percentile(flat, 99)), 6)
    out["p50Channel"] = round(float(np.percentile(flat, 50)), 6)
    out["meanChannel"] = round(float(flat.mean()), 6)
    # Where the maximum lives, so the number can be spot-checked by hand.
    at = int(np.argmax(flat))
    c = at // n
    i = at % n
    out["maxChannels"] = "rgb"[c]
    out["maxLuxel"] = int(i)
    out["maxByte"] = int(arr[i, c])
    out["maxExponent"] = int(ex[i])
    # Luminance is the quantity the lighting comparison actually uses.
    lum = 0.299 * chans[0] + 0.587 * chans[1] + 0.114 * chans[2]
    out["maxLuminance"] = round(float(lum.max()), 6)
    out["p99Luminance"] = round(float(np.percentile(lum, 99)), 6)
    out["p50Luminance"] = round(float(np.percentile(lum, 50)), 6)
    return out

def main(argv):
    if len(argv) < 2:
        sys.exit('usage: lightmap-page-max.py <map.bsp> ...')
    for path in argv[1:]:
        blob = open(path, 'rb').read()
        lumps = [struct.unpack_from('<ii', blob, 8 + i * 16) for i in range(64)]
        out = {"map": os.path.basename(path), "bytes": len(blob)}
        out["ldr"] = page_stats(unpack_lump(blob, *lumps[LUMP_LIGHTING]), "ldr")
        out["hdr"] = page_stats(unpack_lump(blob, *lumps[LUMP_LIGHTING_HDR]), "hdr")
        out["usedPage"] = "ldr" if out["ldr"] else ("hdr" if out["hdr"] else "none")
        print(json.dumps(out))

main(sys.argv)
