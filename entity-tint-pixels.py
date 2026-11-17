#!/usr/bin/env python3
"""entity-tint-pixels.py -- T105 A: the PIXEL half of the entity-tint claim.

The team tint was applied to every entity draw instance (main.cpp, draw stage:
team==2 -> (0.86,0.28,0.22), team==3 -> (0.28,0.52,0.86)) and the pixel shader
multiplies vertex colour into the texel (native_renderer_shaders.cpp:311
albedo = lerp(colour.rgb, sampled * colour.rgb, colour.a), entity pass has
colour.a=1). So a painted model drew its own texture TIMES a team colour the
skin family had already baked in, and a Scout read as a solid red blob.

WHAT THIS MEASURES, AND WHY NOT THE COVERAGE SENTINEL
  The first version of this tool took its mask from --world-entity-coverage
  (envFeatures.x > 9.5 -> float4(1,0,1,1)). Measured on pl_upward_f12 tick 0
  that mask holds 688,388 pixels of 1264x681 -- most of the frame -- because the
  ENTITY SHADER also draws the map's static props, and 5,506 of those pixels
  differ from the raw texel by up to 144 for a reason that has nothing to do with
  the tint (fog, i.e. lerp(texel, fogColour, factor), applied after the diagnostic
  arm returns). A claim "every sentinel pixel equals the raw texel" is therefore
  red on a correct product, so it cannot be the claim.

  The mask used here is the one the NEGATIVE CONTROL measures: a pixel whose
  colour CHANGES between the normal frame and the --entity-tint-mutation frame is
  a pixel the team tint reached. That population is exactly what the claim is
  about, it needs no hand-drawn box, and it cannot go vacuous: if the product
  ever tinted the normal frame too, normal and mutation would agree and the mask
  would collapse, which the --min-mask guard turns into a FAIL.

CLAIMS
  1. mask (tint-touched) pixels >= min-mask, so the claim has a population.
  2. every masked pixel of the NORMAL frame equals the RAW-TEXEL frame
     (--world-texel: sampled texture, no tint, no lightmap, no blend) within tol
     per channel -- exceed must be 0.
  3. the SAME comparison on the MUTATION frame must fail loudly (exceed > 0,
     maxDiff >= 16) -- a control that cannot go red is not measuring anything.

Usage:
  entity-tint-pixels.py NORMAL.bmp TEXEL.bmp MUTATION.bmp [COVERAGE.bmp] [--tol N] [--min-mask N]

Prints one machine-readable line per claim:
  mask pixels=N bbox=x,y,w,h (tint-touched: |normal-mutation| > tol)
  normal-vs-texel pixels=N withinTol=M exceed=K maxDiff=D mean=(r,g,b)
  mutation-vs-texel pixels=N withinTol=M exceed=K maxDiff=D mean=(r,g,b)
  offender x=.. y=.. normal=(..) texel=(..) mutation=(..) diff=..
  FAILED n=.. pixels=.. (up to 4 clusters of normal-vs-texel misses)
  sentinel pixels=N normalExceed=K  (informational, from COVERAGE when given)
Exit 0 when claims 1-3 hold; exit 1 otherwise; exit 2 when the frames cannot be
read (a tool that cannot decide must not report green).
"""
import sys
import struct


def load_bmp(path):
    with open(path, 'rb') as handle:
        data = handle.read()
    if data[:2] != b'BM':
        raise SystemExit('not a bmp: %s' % path)
    pixel_offset = struct.unpack_from('<I', data, 10)[0]
    width = struct.unpack_from('<i', data, 18)[0]
    height = struct.unpack_from('<i', data, 22)[0]
    bpp = struct.unpack_from('<H', data, 28)[0]
    compression = struct.unpack_from('<I', data, 30)[0]
    if bpp != 24 or compression != 0:
        raise SystemExit('%s is %d bpp compression=%d; this reader only takes 24bpp BI_RGB'
                         % (path, bpp, compression))
    row_size = ((width * 3) + 3) & ~3
    pixels = []
    for y in range(height):
        start = pixel_offset + y * row_size
        row = data[start:start + width * 3]
        pixels.append([(row[x * 3 + 2], row[x * 3 + 1], row[x * 3])
                       for x in range(width)])
    # BMP rows are bottom-up; flip so y=0 is the top of the frame.
    pixels.reverse()
    return width, height, pixels


def diff(a, b):
    return max(abs(a[0] - b[0]), abs(a[1] - b[1]), abs(a[2] - b[2]))


def main(argv):
    tol = 2
    min_mask = 50000
    paths = []
    index = 1
    while index < len(argv):
        if argv[index] == '--tol':
            tol = int(argv[index + 1])
            index += 2
        elif argv[index] == '--min-mask':
            min_mask = int(argv[index + 1])
            index += 2
        else:
            paths.append(argv[index])
            index += 1
    if len(paths) not in (3, 4):
        print('usage: entity-tint-pixels.py NORMAL TEXEL MUTATION [COVERAGE] [--tol N] [--min-mask N]')
        return 2
    normal_path, texel_path, mutation_path = paths[0], paths[1], paths[2]
    coverage_path = paths[3] if len(paths) == 4 else None
    width, height, normal = load_bmp(normal_path)
    nw, nh, texel = load_bmp(texel_path)
    mw, mh, mutation = load_bmp(mutation_path)
    if (nw, nh) != (width, height) or (mw, mh) != (width, height):
        print('frames disagree on size: normal=%dx%d texel=%dx%d mutation=%dx%d'
              % (width, height, nw, nh, mw, mh))
        return 2

    # Claim 1's population: pixels whose colour the tint CHANGES, measured from
    # the negative control. Same tol on both sides of the mask so a pixel that
    # hovers on the threshold cannot be counted as tint-touched and then called a
    # violation of equality.
    mask = [[False] * width for _ in range(height)]
    mask_count = 0
    min_x, min_y, max_x, max_y = width, height, -1, -1
    for y in range(height):
        normal_row = normal[y]
        mutation_row = mutation[y]
        mask_row = mask[y]
        for x in range(width):
            if diff(mutation_row[x], normal_row[x]) > tol:
                mask_row[x] = True
                mask_count += 1
                if x < min_x:
                    min_x = x
                if y < min_y:
                    min_y = y
                if x > max_x:
                    max_x = x
                if y > max_y:
                    max_y = y
    if mask_count:
        print('mask pixels=%d bbox=%d,%d,%d,%d (tint-touched: |normal-mutation| > %d)'
              % (mask_count, min_x, min_y, max_x - min_x + 1, max_y - min_y + 1, tol))
    else:
        print('mask pixels=0 bbox=none (tint-touched: |normal-mutation| > %d)' % tol)

    def compare(label, frame):
        within = 0
        exceed = 0
        max_diff = 0
        worst = None
        sums = [0, 0, 0]
        for y in range(height):
            frame_row = frame[y]
            texel_row = texel[y]
            mask_row = mask[y]
            for x in range(width):
                if not mask_row[x]:
                    continue
                reference = texel_row[x]
                value = frame_row[x]
                delta = diff(value, reference)
                if delta <= tol:
                    within += 1
                else:
                    exceed += 1
                if delta > max_diff:
                    max_diff = delta
                    worst = (x, y, value, reference, normal[y][x])
                sums[0] += value[0]
                sums[1] += value[1]
                sums[2] += value[2]
        mean = tuple(int(component / max(mask_count, 1) + 0.5) for component in sums)
        print('%s pixels=%d withinTol=%d exceed=%d maxDiff=%d mean=(%d,%d,%d)'
              % (label, mask_count, within, exceed, max_diff, mean[0], mean[1], mean[2]))
        if worst is not None:
            x, y, value, reference, normal_value = worst
            print('offender x=%d y=%d normal=(%d,%d,%d) texel=(%d,%d,%d) '
                  'mutation=(%d,%d,%d) diff=%d'
                  % (x, y, normal_value[0], normal_value[1], normal_value[2],
                     reference[0], reference[1], reference[2],
                     value[0], value[1], value[2], max_diff))
        return within, exceed, max_diff

    normal_within, normal_exceed, normal_max = compare('normal-vs-texel', normal)
    mutation_within, mutation_exceed, mutation_max = compare('mutation-vs-texel', mutation)

    # Where the normal claim fails, cluster the misses so a failure names a body
    # instead of a count. 4-connected, largest first, four clusters printed.
    misses = [[False] * width for _ in range(height)]
    for y in range(height):
        mask_row = mask[y]
        normal_row = normal[y]
        texel_row = texel[y]
        for x in range(width):
            if mask_row[x] and diff(normal_row[x], texel_row[x]) > tol:
                misses[y][x] = True
    seen = [[False] * width for _ in range(height)]
    clusters = []
    for y in range(height):
        for x in range(width):
            if not misses[y][x] or seen[y][x]:
                continue
            stack = [(x, y)]
            seen[y][x] = True
            members = []
            while stack:
                cx, cy = stack.pop()
                members.append((cx, cy))
                for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                    nx, ny = cx + dx, cy + dy
                    if 0 <= nx < width and 0 <= ny < height and misses[ny][nx] and not seen[ny][nx]:
                        seen[ny][nx] = True
                        stack.append((nx, ny))
            clusters.append(members)
    clusters.sort(key=len, reverse=True)
    for order, members in enumerate(clusters[:4]):
        worst = 0
        for (x, y) in members:
            delta = diff(normal[y][x], texel[y][x])
            if delta > worst:
                worst = delta
        print('FAILED n=%d pixels=%d maxDiff=%d' % (order, len(members), worst))

    # Informational: the coverage sentinel population and how much of it the raw
    # texel comparison misses. This is the reading that showed the sentinel is not
    # a tint mask (static props ride the entity shader); it is printed, not
    # asserted, so the reason survives in the gate log.
    if coverage_path is not None:
        cw, ch, coverage = load_bmp(coverage_path)
        if (cw, ch) == (width, height):
            sentinel = 0
            sentinel_exceed = 0
            for y in range(height):
                coverage_row = coverage[y]
                normal_row = normal[y]
                texel_row = texel[y]
                for x in range(width):
                    pixel = coverage_row[x]
                    if pixel[0] > 200 and pixel[1] < 60 and pixel[2] > 200:
                        sentinel += 1
                        if diff(normal_row[x], texel_row[x]) > tol:
                            sentinel_exceed += 1
            print('sentinel pixels=%d normalExceed=%d (informational: the sentinel '
                  'includes static props, so this is not the tint claim)'
                  % (sentinel, sentinel_exceed))
        else:
            print('sentinel skipped: coverage frame is %dx%d, normal is %dx%d' % (cw, ch, width, height))

    if mask_count < min_mask:
        print('VERDICT normal claim RED: mask holds %d tint-touched pixels, fewer than --min-mask=%d; '
              'the control cannot show the tint is gone (a product that tinted both frames would read like this)'
              % (mask_count, min_mask))
        return 1
    if normal_exceed > 0:
        print('VERDICT normal claim RED: %d of %d tint-touched pixels differ from raw texel by more than %d'
              % (normal_exceed, mask_count, tol))
        return 1
    if mutation_exceed == 0 or mutation_max < 16:
        print('VERDICT mutation control did not go red (exceed=%d maxDiff=%d); the comparison is not measuring the tint'
              % (mutation_exceed, mutation_max))
        return 1
    print('VERDICT normal claim GREEN: all %d tint-touched pixels equal raw texel within %d; '
          'mutation control RED (exceed=%d maxDiff=%d)'
          % (mask_count, tol, mutation_exceed, mutation_max))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
