#!/usr/bin/env python3
"""task-37 Half A: how large is a decoded value on each lighting page, per map.

documents/LEAF-AMBIENT-2026-11-21.md 的两张表都要能从提交的东西里重算：
- §4 的 LDR/HDR 逐叶差异 = tools/page_diff.py 的输出；
- §0.5 的「光贴图页与叶子环境页最大解码值差三个数量级」= 本脚本的输出。

两个量必须分行问，因为它们不是一回事：光贴图页是 luxel(4 字节 ColorRGBExp32)，
叶子环境页是 28 字节一条的立方体样本，步长不同，混着算会把 max 算小。
"""
import json
import math
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import leaf_ambient_survey as S

# 页名 -> lump 号。8/53 是光贴图(LDR/HDR)，56/55 是叶子环境(LDR/HDR)。
PAGES = (('lightLdr', 8), ('lightHdr', 53), ('ambLdr', 56), ('ambHdr', 55))


def exponent_histogram(buf, stride):
    """每个 ColorRGBExp32 的第 4 个字节是指数，统计它说明量级，不靠单个 max。"""
    h = {}
    for i in range(0, len(buf) - stride + 1, stride):
        e = struct.unpack_from('<b', buf, i + 3)[0]
        h[e] = h.get(e, 0) + 1
    return dict(sorted(h.items(), key=lambda kv: -kv[1])[:6])


def max_decoded(buf, stride):
    """max(r,g,b) * 2^exp / 255 —— 与 bsp_map.cpp:255 decodeColorRgbExp32 同一个算式。"""
    best = 0.0
    for i in range(0, len(buf) - stride + 1, stride):
        e = struct.unpack_from('<b', buf, i + 3)[0]
        scale = math.ldexp(1.0, e) / 255.0
        best = max(best, max(buf[i], buf[i + 1], buf[i + 2]) * scale)
    return best


def measure(path):
    blob = open(path, 'rb').read()
    lumps = [struct.unpack_from('<ii', blob, 8 + i * 16) for i in range(64)]
    rec = {'map': os.path.basename(path)}
    for name, lump in PAGES:
        buf = S.unpack_lump(blob, *lumps[lump])
        stride = 28 if name.startswith('amb') else 4
        rec[name] = {'bytes': len(buf), 'records': len(buf) // stride if len(buf) % stride == 0 else -1,
                     'maxDecoded': round(max_decoded(buf, stride), 6),
                     'expTop': exponent_histogram(buf, stride)}
    return rec


def main(argv):
    if len(argv) < 2:
        print('usage: page_magnitude.py <map.bsp> [<map.bsp> ...]')
        return 2
    for p in argv[1:]:
        print(json.dumps(measure(p)))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))