#!/usr/bin/env bash
# frame-capture-check.sh -- the running program must be able to hand back the
# frame it just composed, and that file must be checkable without a human eye.
#
# Why this exists
# ---------------
# Every reading the chain had before this step was either a counter or a value
# pulled out of the decoder. None of them could speak to the thing the round is
# actually about: whether the picture is right. "The renderer received the right
# z" is not "the screen shows the right thing", and the gap is structural -- no
# amount of extra counters closes it, because the failure mode is a plausible
# picture assembled from wrong inputs (step 12's weapon drawn as its arms is the
# worked example: it resolved, to a real asset, and every count stayed green).
#
# What this step does NOT do
# --------------------------
# It does not judge the picture. It pins the *instrument*: that a frame can be
# taken from the running program at a chosen tick, that the file is a well-formed
# BMP whose own header agrees with its own size, that the same input twice gives
# the same bytes, and that a different scene gives different bytes. Those four are
# what let a later step (material wiring, bone animation) assert "this changed the
# picture" instead of "this changed a counter". A capture pipeline that is not
# deterministic, or that returns the same bytes for every scene, would make every
# such later assertion vacuously true.
#
# The capture is taken inside draw(), *before* Present(): after Present the back
# buffer contents are undefined, so that is the only point at which "the frame we
# just composed" is still readable. That is why the flag is a one-shot request
# consumed by the renderer rather than a mode the program stays in.
#
# Usage: bash frame-capture-check.sh [--mutation]
#   --mutation drives both content checks with inputs they must refuse (a
#   one-byte change for the hash comparison, the fallback picture for the
#   fallback comparison) and requires this script to go red on each, which is
#   how "the content checks can fail" is shown rather than assumed.
# Exit:  0 = every assertion held (or, with --mutation, the checks went red).
#
# 2026-10-08: the demo half of this script was passing for the wrong reason. The
# only demo it named was koth_bagel_rc13, whose map is not installed here, so the
# capture it produced was the renderer's fallback full-screen quad textured with the
# fallback `texture_` -- the vgui spray decal -- and "differs from the paused scene"
# held on that. A second demo on an installed map, plus a cap on how many distinct
# colours the frame may have, is what makes this half of the script mean what its
# name says. See resource-reachability-check.sh for the underlying resource facts.
#
# 2026-10-13: the cap is gone -- not relaxed, replaced. The world-material round
# gave the world per-face lightmaps, which is exactly what the cap's premise
# ("a shaded world with no textures is a handful of flat greys") excluded: the
# installed-map capture went from 563 distinct colours to 15909, while the
# fallback decal stayed at 139121 (its frame is byte-identical across rounds,
# sha256 fa1bb194...). The cap had 858x of room when it was chosen (139121 / 162)
# and 247x when this round started (139121 / 563); it now has 8.7x, and every
# further realism step (albedo textures are next) moves the world's count toward
# the decal's with no ceiling. What replaces it measures the thing the cap was a
# proxy for: the installed-map capture and the fallback capture are taken in the
# same run, and the installed-map frame must differ from the fallback frame in a
# large fraction of its payload bytes. Measured byte distance: 0.9840 on
# 2026-10-13 and 0.9899 on 2026-10-12; a capture that came back as the fallback
# shares the decal artwork and lands near zero. The reference being captured in
# the same run rather than assumed is the point -- the check does not carry this
# machine's fallback asset inside its threshold. The distinct-colour count is
# still printed, as a reading with no assertion on it.
set -uo pipefail
cd "$(dirname "$0")"

EXE=native/build-nmake/tf2_demo_native.exe
TF="D:/SteamLibrary/steamapps/common/Team Fortress 2/tf"
# Two demos, on purpose. The first is the one every other step uses. Its map
# (koth_bagel_rc13) is not installed on this machine, so the renderer falls through
# to its full-screen fallback quad and the capture comes back as the vgui spray
# decal -- a picture that is "not the paused scene" for a reason that has nothing to
# do with the demo advancing. The second demo's map (cp_snakewater_final1) *is*
# installed, so it produces a real, flat-shaded world. Asserting on both is what
# separates "the capture is scene-sensitive" from "the capture is scene-sensitive
# because two different fallbacks were drawn".
BAGEL="D:/TF2_Demo_Player/testdata/demos/4a9bfb9276509d0ec5f5fdc722a95b17_match-20260927-0239-koth_bagel_rc13.dem"
SNAKE="D:/TF2_Demo_Player/testdata/demos/bb841c6d379ff7c40d0c8baf99f59d8d_matcha-20260927-1347-cp_snakewater_final1.dem"
OUT="${FRAME_CAPTURE_OUT:-evidence/frame-capture}"
mkdir -p "$OUT"

MUTATION=0
[ "${1:-}" = "--mutation" ] && MUTATION=1

fail=0
# Missing input is a failure, not a skip. A capture gate that quietly SKIPs when
# the binary or the demo is absent is exactly the shape the P0 pass shipped four
# of (see verify-all.sh's header), so this exits non-zero instead.
for path in "$EXE" "$TF" "$BAGEL" "$SNAKE"; do
  [ -e "$path" ] || { echo "FATAL: missing input: $path"; exit 1; }
done

PY="C:/Users/Administrator/.workbuddy-ai/binaries/python/versions/3.13.12/python.exe"
[ -x "$PY" ] || { echo "FATAL: missing interpreter: $PY"; exit 1; }

assert_eq() { # assert_eq <label> <actual> <expected>
  if [ "${2:-<none>}" = "$3" ]; then
    echo "  OK   $1 = $3"
  else
    echo "  FAIL $1 = ${2:-<none>}, expected $3"; fail=1
  fi
}

# How long the program is given before the capture is declared missing. Two
# regimes: with no demo the loop starts in a few seconds, with a demo the VPK
# index (104041 entries) and the demo load have to finish first. Both are generous
# multiples of the measured times, and 300s is the ceiling rather than the norm --
# a capture that needs a bigger number than this is a step that should be
# re-measured, not a step to wait out.
PAUSED_TIMEOUT=120
DEMO_TIMEOUT=300

# Audio: this machine has a real output device alongside the system default, and
# the default is the wrong one to make noise on. Every launch here therefore names
# the device explicitly -- including the paused ones, because the audio device is
# opened during startup and pausing playback does not stop it from being opened.
# A gate that makes the machine beep on the wrong device is a gate people turn off.
AUDIO_DEVICE=6

run_capture() { # run_capture <out.bmp> <timeout> [extra args...]
  local out="$1" limit="$2"; shift 2
  rm -f "$out" "$out.csv"
  timeout "$limit" "$EXE" --tf-root "$TF" --audio-device "$AUDIO_DEVICE" "$@" \
    --capture-frame "$out" --metrics-file "$out.csv" > "$out.log" 2>&1
  echo "$?"
}

# The installed-map capture must not be the fallback picture -- and "the fallback
# picture" is not assumed: it is captured in the same run. `demo.bmp` is the
# koth_bagel_rc13 demo, whose map is not installed on this machine (the resource
# gate asserts `bagel BSP bytes = 0` as its negative control), so whatever that
# capture shows IS what this build draws in place of a world -- measured: the
# spray decal. The two payloads are compared byte for byte. A capture that came
# back as the fallback shares the decal artwork and lands near zero; a real world
# measured 0.9840 (2026-10-13) and 0.9899 (2026-10-12), so the threshold sits an
# order of magnitude below the honest reading and well above the defect -- and it
# does not carry this machine's fallback asset inside it, because the reference
# frame is taken fresh on every run. No colour transform is applied to either
# side: a lossy transform can hide a difference instead of measuring it.
FALLBACK_MIN_DIFF_PERCENT=10
not_fallback_frame() { # not_fallback_frame <installed.bmp> <fallback.bmp>
  local readout
  readout=$("$PY" - "$1" "$2" <<'PYEOF'
import struct, sys
def payload(path):
    data = open(path, 'rb').read()
    off, = struct.unpack_from('<I', data, 10)
    return data[off:]
a = payload(sys.argv[1])
b = payload(sys.argv[2])
if len(a) != len(b):
    print('length-mismatch %d %d' % (len(a), len(b)))
    raise SystemExit(0)
n = len(a)
x = int.from_bytes(a, 'little') ^ int.from_bytes(b, 'little')
diff = n - x.to_bytes(n, 'little').count(0)
print('%d %d %d %.4f' % (diff, n, int(100.0 * diff / n), diff / n))
PYEOF
)
  if [ -z "$readout" ]; then
    echo "  FAIL the fallback comparison produced no reading"; return 1
  fi
  set -- $readout
  if [ "$1" = "length-mismatch" ]; then
    echo "  FAIL the two captures have different payload sizes ($2 vs $3)"; return 1
  fi
  echo "installed-map vs fallback-capture: differing $1 / $2 = $4 (need >= ${FALLBACK_MIN_DIFF_PERCENT}%)"
  if [ "$3" -ge "$FALLBACK_MIN_DIFF_PERCENT" ]; then
    echo "  OK   the installed-map frame is not the fallback picture"
    return 0
  fi
  echo "  FAIL the installed-map frame is the fallback picture ($3% of its bytes differ)"
  return 1
}

echo "=== 1/4 a frame comes back from the running program (no demo) ==="
# --start-paused with no demo. Correction (2026-10-13): the description that used
# to stand here -- "the renderer's fallback is a full-screen quad plus the UI
# overlay" -- had gone stale. The paused run draws a real world: with no demo the
# program loads koth_abbey, and a traced paused capture reads its cubemap plus
# 541665 world vertices, of which the world-material round lightmapped 169428
# (= 3 x the probe's 56476 lightmapped triangles on 2koth_abbey.bsp; the two
# sides agree). That is also why the paused frame moved in this round -- 180 ->
# 4419 distinct colours, sha256 a61a3e4d -> dd65787f. What this section needs
# from the capture is that it is cheap to reach and identical every run; both
# still hold. What it must not be read as is "an empty scene".
PAUSED_BMP="$OUT/paused.bmp"
PAUSED_RC=$(run_capture "$PAUSED_BMP" "$PAUSED_TIMEOUT" --start-paused)
echo "paused capture rc=$PAUSED_RC size=$(stat -c%s "$PAUSED_BMP" 2>/dev/null || echo 0)"
# rc 0 is "wrote a frame and left on its own"; 124 would be the timeout, which
# means the frame never arrived. Both are distinguishable from 16 below.
assert_eq "paused capture rc" "$PAUSED_RC" "0"

echo
echo "=== 2/4 the file is a well-formed BMP and its header agrees with its size ==="
# Every field here is read back out of the file and cross-checked against another
# field of the same file, so this needs no external tool and no expectation of a
# particular resolution: the header has to be internally consistent whatever the
# window size turns out to be.
read_bmp() { # read_bmp <file> -> "fileBytes width height bpp dataOffset dibSize imageBytes"
  "$PY" - "$1" <<'PYEOF'
import struct, sys
data = open(sys.argv[1], 'rb').read()
if len(data) < 54 or data[0:2] != b'BM':
    print("notbmp"); raise SystemExit(1)
fileBytes, = struct.unpack_from('<I', data, 2)
dataOffset, dibSize = struct.unpack_from('<II', data, 10)
width, height = struct.unpack_from('<ii', data, 18)
planes, bpp = struct.unpack_from('<HH', data, 26)
compression, imageBytes = struct.unpack_from('<II', data, 30)
print(fileBytes, width, height, bpp, dataOffset, dibSize, imageBytes, planes, compression, len(data))
PYEOF
}
PAUSED_FIELDS=$(read_bmp "$PAUSED_BMP" || echo notbmp)
if [ "$PAUSED_FIELDS" = "notbmp" ]; then
  echo "  FAIL the capture is not a BMP at all"; fail=1
else
  set -- $PAUSED_FIELDS
  FB=$1; W=$2; H=$3; BPP=$4; OFF=$5; DIB=$6; IMG=$7; PLANES=$8; COMP=$9; ACTUAL=${10}
  echo "fileBytes=$FB ${W}x${H} bpp=$BPP dataOffset=$OFF dibSize=$DIB imageBytes=$IMG planes=$PLANES compression=$COMP actual=$ACTUAL"
  # The BMP row stride: 24bpp rows are padded to a 4-byte boundary. Recomputed
  # here rather than taken from the file, so a capture that wrote the wrong stride
  # would disagree with its own header.
  STRIDE=$(( (W * 3 + 3) / 4 * 4 ))
  assert_eq "dataOffset (14-byte file header + 40-byte DIB)" "$OFF" "54"
  assert_eq "DIB header size" "$DIB" "40"
  assert_eq "bits per pixel" "$BPP" "24"
  assert_eq "planes" "$PLANES" "1"
  assert_eq "compression (BI_RGB)" "$COMP" "0"
  assert_eq "imageBytes == stride*height" "$IMG" "$((STRIDE * H))"
  assert_eq "fileBytes == dataOffset + imageBytes" "$FB" "$((54 + IMG))"
  assert_eq "fileBytes == the bytes actually on disk" "$FB" "$ACTUAL"
  # A captured frame that is one flat colour is the no-scene case or a renderer
  # that never drew; either way it is not evidence about a picture, so the
  # distinct-colour count is asserted to be more than a handful.
  COLORS=$("$PY" - "$PAUSED_BMP" <<'PYEOF'
import sys
data = open(sys.argv[1], 'rb').read()
seen = set()
for i in range(54, len(data), 3):
    seen.add(data[i:i+3])
    if len(seen) > 8: break
print(len(seen))
PYEOF
)
  echo "distinct colours (capped at 9) = $COLORS"
  if [ "${COLORS:-0}" -gt 1 ]; then
    echo "  OK   the frame is not a single flat colour"
  else
    echo "  FAIL the frame is one flat colour -- nothing was drawn"; fail=1
  fi
  # The resolution is the swap chain's, which the code derives from the window's
  # client area, so it is pinned only to being positive and 4-byte-closeable --
  # asserting 1280x720 here would fail on any other window size for no good reason.
  if [ "${W:-0}" -gt 0 ] && [ "${H:-0}" -gt 0 ]; then
    echo "  OK   the frame has a positive extent"
  else
    echo "  FAIL extent is ${W:-?}x${H:-?}"; fail=1
  fi
fi

echo
echo "=== 3/4 the same input twice gives the same bytes, and a different scene does not ==="
PAUSED_BMP2="$OUT/paused-again.bmp"
PAUSED_RC2=$(run_capture "$PAUSED_BMP2" "$PAUSED_TIMEOUT" --start-paused)
echo "second paused capture rc=$PAUSED_RC2 size=$(stat -c%s "$PAUSED_BMP2" 2>/dev/null || echo 0)"
assert_eq "second paused capture rc" "$PAUSED_RC2" "0"
# Determinism is what makes a byte comparison meaningful at all. The paused scene
# is static, so two runs must agree; if they do not, no later "the picture
# changed" claim can be trusted.
HASH1=$("$PY" -c "import hashlib,sys;print(hashlib.sha256(open(sys.argv[1],'rb').read()).hexdigest())" "$PAUSED_BMP")
HASH2=$("$PY" -c "import hashlib,sys;print(hashlib.sha256(open(sys.argv[1],'rb').read()).hexdigest())" "$PAUSED_BMP2")
echo "paused sha256 #1 = $HASH1"
echo "paused sha256 #2 = $HASH2"
if [ -n "$HASH1" ] && [ "$HASH1" = "$HASH2" ]; then
  echo "  OK   two captures of the same static scene are byte-identical"
else
  echo "  FAIL two captures of the same static scene differ, so no content check below can mean anything"; fail=1
fi

# The scene with a demo loaded must differ from the paused one: otherwise the
# capture is not actually reading the composed frame (or the demo went nowhere),
# and "the picture is right" would be untestable.
DEMO_BMP="$OUT/demo.bmp"
DEMO_RC=$(run_capture "$DEMO_BMP" "$DEMO_TIMEOUT" --demo "$BAGEL" --capture-tick 0)
echo "demo capture rc=$DEMO_RC size=$(stat -c%s "$DEMO_BMP" 2>/dev/null || echo 0)"
if [ "$DEMO_RC" = "0" ]; then
  DEMO_FIELDS=$(read_bmp "$DEMO_BMP" || echo notbmp)
  if [ "$DEMO_FIELDS" = "notbmp" ]; then
    echo "  FAIL the demo capture is not a BMP"; fail=1
  else
    set -- $DEMO_FIELDS
    echo "demo ${2}x${3} bpp=$4 fileBytes=$1 actual=${10}"
    DEMO_HASH=$("$PY" -c "import hashlib,sys;print(hashlib.sha256(open(sys.argv[1],'rb').read()).hexdigest())" "$DEMO_BMP")
    echo "demo sha256 = $DEMO_HASH"
    if [ -n "$DEMO_HASH" ] && [ "$DEMO_HASH" != "$HASH1" ]; then
      echo "  OK   a demo scene and the paused scene are different pictures"
    else
      echo "  FAIL the demo capture is byte-identical to the paused one, so the capture is not scene-sensitive"; fail=1
    fi
  fi
else
  # 124 here is a timeout and 16 is a capture failure; both are failures of this
  # step. Reported separately from the assertions so a reader can tell "the demo
  # never reached tick 0 in time" from "the file came back malformed".
  echo "  FAIL the demo capture did not complete in ${DEMO_TIMEOUT}s (rc=$DEMO_RC)"; fail=1
fi

# The second demo, on a map that is actually installed. Its frame must differ
# from the paused one, and it must not be the fallback picture: the bagel demo
# above is this run's fallback reference (its map is not installed), and the two
# frames are compared directly by not_fallback_frame. The colour-count cap that
# used to stand here is gone for the reason in the 2026-10-13 note at the top;
# the count is kept as a printed reading. History, for whoever needs the shape of
# it: 2026-10-08 measured bagel 139121 / snakewater 162 / paused 69 and the cap
# at 1000 sat between with orders of magnitude to spare; the world-material round
# measured bagel 139121 / snakewater 15909 / paused 4419 with the same cap.
SNAKE_BMP="$OUT/demo-installed-map.bmp"
SNAKE_RC=$(run_capture "$SNAKE_BMP" "$DEMO_TIMEOUT" --demo "$SNAKE" --capture-tick 0)
echo "installed-map demo capture rc=$SNAKE_RC size=$(stat -c%s "$SNAKE_BMP" 2>/dev/null || echo 0)"
if [ "$SNAKE_RC" = "0" ]; then
  SNAKE_HASH=$("$PY" -c "import hashlib,sys;print(hashlib.sha256(open(sys.argv[1],'rb').read()).hexdigest())" "$SNAKE_BMP")
  echo "installed-map demo sha256 = $SNAKE_HASH"
  if [ -n "$SNAKE_HASH" ] && [ "$SNAKE_HASH" != "$HASH1" ]; then
    echo "  OK   the installed-map demo scene differs from the paused scene"
  else
    echo "  FAIL the installed-map demo capture equals the paused capture"; fail=1
  fi
  # A reading, not an assertion: this is the number the old cap asserted on, and
  # with the world's own lightmaps it no longer separates the two shapes (the
  # world climbed 563 -> 15909 while the decal stayed at 139121).
  SNAKE_COLORS=$("$PY" - "$SNAKE_BMP" <<'PYEOF'
import sys
data = open(sys.argv[1], 'rb').read()
seen = set()
for i in range(54, len(data), 3):
    seen.add(data[i:i+3])
print(len(seen))
PYEOF
)
  echo "installed-map distinct colours (full count) = $SNAKE_COLORS  [reading only]"
  not_fallback_frame "$SNAKE_BMP" "$DEMO_BMP" || fail=1
else
  echo "  FAIL the installed-map demo capture did not complete in ${DEMO_TIMEOUT}s (rc=$SNAKE_RC)"; fail=1
fi

echo
echo "=== 4/4 a bad tick is refused instead of guessed ==="
# --capture-tick is parsed, not defaulted: a nonsense value must exit 13 rather
# than silently capture tick 0, because a gate that asks for tick N and gets the
# first frame instead would pass while capturing the wrong moment.
BAD_RC=0
timeout "$PAUSED_TIMEOUT" "$EXE" --tf-root "$TF" --audio-device "$AUDIO_DEVICE" --start-paused \
  --capture-frame "$OUT/bad.bmp" --capture-tick not-a-number > "$OUT/bad.log" 2>&1 || BAD_RC=$?
echo "invalid --capture-tick rc=$BAD_RC"
assert_eq "invalid --capture-tick exit code" "$BAD_RC" "13"
if [ ! -e "$OUT/bad.bmp" ]; then
  echo "  OK   no frame was written for the rejected request"
else
  echo "  FAIL a frame was written despite the rejected tick"; fail=1
fi

echo
if [ "$MUTATION" -eq 1 ]; then
  # The content checks are the ones a later picture claim will lean on, so the
  # mutations target them, one per check: a one-byte change for the hash
  # comparison (a hash comparison that cannot tell one byte apart is not a
  # comparison) and the fallback picture for the fallback comparison (a fallback
  # check that accepts the fallback picture is not a check).
  caught=0
  missed=0

  echo "=== mutation 1/2: one byte of the image is flipped ==="
  MUT="$OUT/mutated.bmp"
  cp "$PAUSED_BMP" "$MUT"
  "$PY" - "$MUT" <<'PYEOF'
import sys
p = sys.argv[1]
data = bytearray(open(p, 'rb').read())
data[54] ^= 0xFF
open(p, 'wb').write(bytes(data))
PYEOF
  MUT_HASH=$("$PY" -c "import hashlib,sys;print(hashlib.sha256(open(sys.argv[1],'rb').read()).hexdigest())" "$MUT")
  if [ -n "$MUT_HASH" ] && [ "$MUT_HASH" != "$HASH1" ]; then
    echo "mutation 1/2 caught: a one-byte change moves the content hash"
    caught=$((caught + 1))
  else
    echo "mutation 1/2 NOT caught: a one-byte change did not move the content hash"
    missed=$((missed + 1))
  fi

  # Mutation 2 drives the replacement criterion with the shape it exists to
  # refuse: an installed-map capture that is the fallback picture. The copy gets
  # 2% of its payload bytes perturbed, so the question is not "are two identical
  # files identical" but "does the comparison refuse a frame that is 98% the
  # fallback picture" -- which is what a fallback capture looks like.
  echo "=== mutation 2/2: the installed-map capture becomes the fallback picture ==="
  MUT2="$OUT/demo-installed-map.as-fallback.bmp"
  cp "$DEMO_BMP" "$MUT2"
  "$PY" - "$MUT2" <<'PYEOF'
import sys
p = sys.argv[1]
data = bytearray(open(p, 'rb').read())
for i in range(54, len(data), 50):
    data[i] ^= 0x5A
open(p, 'wb').write(bytes(data))
PYEOF
  if not_fallback_frame "$MUT2" "$DEMO_BMP"; then
    echo "mutation 2/2 NOT caught: a fallback-shaped frame was accepted as the world"
    missed=$((missed + 1))
  else
    echo "mutation 2/2 caught: the comparison refused the fallback-shaped frame"
    caught=$((caught + 1))
  fi

  echo
  if [ "$missed" -eq 0 ]; then
    echo "MUTATION-CAUGHT=PASS ($caught/2 mutations red)"
    exit 0
  fi
  echo "MUTATION-CAUGHT=FAIL ($missed/2 mutations stayed green)"
  exit 1
fi

if [ "$fail" -eq 0 ]; then
  echo "FRAME-CAPTURE=PASS"
else
  echo "FRAME-CAPTURE=FAIL"
fi
exit "$fail"
