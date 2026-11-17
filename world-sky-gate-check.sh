#!/usr/bin/env bash
# world-sky-gate-check.sh -- W-1: the sky is not a quality item.
#
# WHAT IT ASSERTS
#   A. A FRESH INSTALL DRAWS THE SKY. The capture uses an EMPTY settings file:
#      no [renderer] key at all, which is what loadSettings() reads on a machine
#      that never touched the setting (preset falls back to 0 = Performance).
#      The trace must say "gpu: world sky state=active ... draws=1". Before W-1
#      the same isolate said "state=unavailable ... reason=skybox draw disabled
#      by the caller", because render.skybox was derived from the preset.
#   B. THE USER-VISIBLE SYMPTOM IS GONE. On the pinned user-view pose
#      (pl_upward tick 1800) the frame must contain no clear-colour pixel at
#      all, and in particular no ENCLOSED clear pixel: that population was
#      2,072 px before the fix and is the "black block" the report is about.
#      The clear colour is RGB(14,18,22); a depth test cannot see the sky
#      because the sky writes no depth, so colour is the only sound test.
#   C. NEGATIVE CONTROL, RECONCILED PIXEL FOR PIXEL. --no-world-sky on the
#      same pose must paint nothing ("state=unavailable draws=0") and the two
#      frames must differ by exactly the pixels that (a) are clear with the sky
#      off, (b) are not clear with the sky on, and (c) have NO world geometry in
#      front of them in the sky-on pass-depth plane; plus the identity
#      enclosed_clear(off) - enclosed_clear(on) == enclosed pixels the sky
#      filled. History: C used to pin "2,072 enclosed clear px". That number
#      was a property of a defect that has since been fixed elsewhere (the
#      transposed displacement parameterisation, work/disp-param P0 -- those
#      pixels are now painted by terrain), so pinning it would turn a correct
#      merge red while asserting nothing about the sky. The reconciled form
#      holds under any world geometry.
#   D. THE DEPTH VIEW STILL DOES NOT DRAW THE SKY (T52's contract: a depth
#      frame is scalar). --world-depth must stay "state=unavailable draws=0".
#   E. AN EXPLICIT KEY STILL WINS. A settings file that says preset=1 AND
#      skybox=0 must NOT draw the sky, and the frame it produces must be the
#      same picture as --no-world-sky (frame_diff=0): the precedence is CLI >
#      explicit key > compiled default, and the preset must have stopped
#      deciding this in either direction. Before W-1 preset=1 forced the sky
#      on. The picture comparison replaces an old pin on the 2,072 px
#      population, for the reason given under C.
#
# COST: four captures at ~60 s each on the reference machine; two of them also
# dump the per-pass depth planes (--capture-pass-depth), so allow a little more
# and a few hundred MB of spare space under the output directory. --reuse keeps
# frames, traces and planes already there.
#
# Usage: bash world-sky-gate-check.sh [--reuse]
# Exit:  0 = every assertion held; 1 = an assertion or a capture failed;
#        3 = SKIPPED: a required input is missing. The reason is printed as
#            "SKIP <reason>" on its own line so a calling chain can count it as
#            a skip -- not as a pass, and not as a silent no-op.
#
# The gate is otherwise self-contained: it writes its own settings/ui files (an
# EMPTY settings file is the fresh-install input) and derives the camera from
# the demo plus --capture-tick. Its only external inputs are the product, the
# demo and a python; any of those missing is the SKIP above.
set -u
here="$(cd "$(dirname "$0")" && pwd)"
cd "$here" || exit 2

PYTHON="${PYTHON:-python}"
TF="${TF2_TF_ROOT:-/d/SteamLibrary/steamapps/common/Team Fortress 2/tf}"
DEMO="${WORLD_SKY_DEMO:-/e/demo测试/pl_upward_f12/STV/a9630c6557468b24f0b8c2e7dab38ed8_match-20261030-2145-pl_upward_f12.dem}"
TICK="${WORLD_SKY_TICK:-1800}"
OUT="${WORLD_SKY_OUT:-$here/.scratch/w1-world-sky}"
PRODUCT="${WORLD_SKY_PRODUCT:-native/build-nmake/tf2_demo_native.exe}"
REUSE=0
[ "${1:-}" = "--reuse" ] && REUSE=1

win() {
  if command -v cygpath >/dev/null 2>&1; then cygpath -m "$1"; else printf '%s' "$1"; fi
}

[ -x "$PRODUCT" ] || { echo "SKIP no product at $PRODUCT (build first)"; exit 3; }
[ -f "$DEMO" ] || { echo "SKIP no demo at $DEMO"; exit 3; }
command -v "$PYTHON" >/dev/null 2>&1 || { echo "SKIP no python ($PYTHON)"; exit 3; }

mkdir -p "$OUT/iso"
pass=0
fail=0
ok() { echo "  PASS $*"; pass=$((pass + 1)); }
bad() { echo "  FAIL $*"; fail=$((fail + 1)); }

# Every capture starts from an EMPTY settings file, so the compiled default is
# the input and a run cannot inherit what an earlier capture saved on exit.
# ISO_CONTENT is the [renderer] body the next capture reads; empty means "no key
# at all", which is the fresh-install case the A/B assertions are about.
ISO_CONTENT=""
fresh_isolate() {
  printf '%s' "$ISO_CONTENT" > "$OUT/iso/settings-empty.ini"
  : > "$OUT/iso/ui-empty.ini"
}

capture() { # name -- flags...
  local name="$1"; shift
  local frame="$OUT/$name.bmp" trace="$OUT/$name.trace" log="$OUT/$name.log"
  if [ "$REUSE" = 1 ] && [ -f "$frame" ] && [ -f "$trace" ]; then
    echo "  reuse $name"
    return 0
  fi
  fresh_isolate
  TF2_NATIVE_TRACE="$(win "$trace")" timeout 300 "$PRODUCT" \
    --tf-root "$TF" --demo "$DEMO" --capture-tick "$TICK" \
    --capture-frame "$(win "$frame")" \
    --settings-file "$(win "$OUT/iso/settings-empty.ini")" \
    --ui-settings-file "$(win "$OUT/iso/ui-empty.ini")" \
    --metrics-file "$(win "$OUT/$name.csv")" \
    --audio-disabled --hidden-window --world-water off "$@" > "$log" 2>&1
  local rc=$?
  [ "$rc" = 0 ] || bad "capture $name exited $rc"
  return 0
}

account() { # frame -> "clear_total=N enclosed_clear=M"
  "$PYTHON" world-sky-gate-account.py "$1" 2>/dev/null
}

field() { # "clear_total=12 enclosed_clear=3" key -> value
  printf '%s' "$1" | sed -n "s/.*$2=\([0-9]*\).*/\1/p"
}

echo "== W-1 sky gate: preset-0 isolate, pose pl_upward tick $TICK =="
# This capture carries the pass-depth planes C reconciles against, and the
# trace both A and C read.
capture default --capture-pass-depth "$(win "$OUT/default-depth")"

# ---- A: a fresh install draws the sky --------------------------------------
if grep -aq 'gpu: world sky state=active' "$OUT/default.trace" 2>/dev/null; then
  ok "A: preset 0 (empty settings file) draws the sky (state=active)"
else
  bad "A: preset 0 did not draw the sky"
fi
if grep -aq 'gpu: world sky state=active.*draws=1' "$OUT/default.trace" 2>/dev/null; then
  ok "A: the sky pass recorded draws=1"
else
  bad "A: the sky pass did not record draws=1"
fi
# The two words that make the pass what it is (W-1 perf A/B): the box is pinned
# to the far plane (clip.z == clip.w) instead of being a small box the map is
# drawn over, and the pixel shader is the discard-free sky one -- the world
# shader contains a discard, which forfeits early depth rejection, so the draw
# order alone would not cut the cost. Asserted from the trace, not the picture:
# a small box drawn first looks the same in these poses and would fail no pixel
# test.
if grep -aq 'gpu: world sky state=active.*farplane=1' "$OUT/default.trace" 2>/dev/null; then
  ok "A: the sky box is pinned to the far plane (farplane=1)"
else
  bad "A: the sky pass did not record farplane=1"
fi
if grep -aq 'gpu: world sky state=active.*shader=dedicated' "$OUT/default.trace" 2>/dev/null; then
  ok "A: the sky pass uses the dedicated discard-free shader (shader=dedicated)"
else
  bad "A: the sky pass did not record shader=dedicated"
fi

# ---- B: the user-visible population is gone --------------------------------
default_account=$(account "$OUT/default.bmp")
default_total=$(field "$default_account" clear_total)
default_enclosed=$(field "$default_account" enclosed_clear)
[ "${default_enclosed:-}" = "0" ] \
  && ok "B: enclosed clear px == 0 (the hard condition; the old 2,072 pin now belongs to P0)" \
  || bad "B: enclosed clear px = ${default_enclosed:-<none>} (expected 0)"
[ "${default_total:-}" = "0" ] \
  && ok "B: no clear-colour pixel remains in the frame" \
  || bad "B: ${default_total:-<none>} clear-colour px remain (expected 0)"

# ---- C: negative control, reconciled against the sky-on frame ---------------
capture skyoff --no-world-sky --capture-pass-depth "$(win "$OUT/skyoff-depth")"
if grep -aq 'gpu: world sky state=unavailable.*draws=0' "$OUT/skyoff.trace" 2>/dev/null; then
  ok "C: --no-world-sky records state=unavailable draws=0"
else
  bad "C: --no-world-sky did not record state=unavailable draws=0"
fi
# The plane is selected by NAME, never by index: --capture-pass-depth numbers
# its dumps with a running counter, so a skipped pass shifts every later index
# (this very control skips the sky, so its own dump list is one plane shorter).
# 10-sky is dumped inside the deferred sky draw, i.e. with the world's depth
# already present, which is what makes "far here" mean "no world geometry owns
# this pixel".
sky_plane=$(ls "$OUT"/default-depth-*-10-sky.pgm 2>/dev/null | head -1)
[ -n "${sky_plane:-}" ] || sky_plane=$(ls "$OUT"/default-depth-*-30-world-done.pgm 2>/dev/null | head -1)
if [ -n "${sky_plane:-}" ] && [ -f "$OUT/default.bmp" ] && [ -f "$OUT/skyoff.bmp" ]; then
  reconciled=$("$PYTHON" world-sky-gate-reconcile.py "$OUT/default.bmp" "$OUT/skyoff.bmp" "$sky_plane" 2>/dev/null)
  rdiff=$(field "$reconciled" diff)
  rfar=$(field "$reconciled" far)
  renc_off=$(field "$reconciled" enclosed_off)
  renc_on=$(field "$reconciled" enclosed_on)
  renc_diff=$(field "$reconciled" enclosed_diff)
  if [ -n "${rdiff:-}" ] && [ "${rdiff:-0}" -gt 0 ]; then
    ok "C: the sky painted ${rdiff} px the world did not (diff > 0)"
  else
    bad "C: sky-on and sky-off differ by ${rdiff:-<none>} px (expected > 0)"
  fi
  [ "${rfar:-x}" = "${rdiff:-y}" ] \
    && ok "C: every px the sky painted is world-free at the far plane (${rfar:-?}/${rdiff:-?})" \
    || bad "C: ${rfar:-<none>} of ${rdiff:-<none>} sky-painted px are world-free (expected all)"
  if [ -n "${renc_off:-}" ] && [ -n "${renc_on:-}" ] && [ -n "${renc_diff:-}" ] \
     && [ "$((renc_off - renc_on))" = "$renc_diff" ]; then
    ok "C: enclosed(off) - enclosed(on) == enclosed px the sky filled (${renc_off} - ${renc_on} == ${renc_diff})"
  else
    bad "C: enclosed account does not reconcile (off=${renc_off:-<none>} on=${renc_on:-<none>} filled=${renc_diff:-<none>})"
  fi
else
  bad "C: no pass-depth plane for the sky-on frame (expected 10-sky or 30-world-done)"
fi

# ---- D: the depth view contract --------------------------------------------
capture depth --world-depth
if grep -aq 'gpu: world sky state=unavailable.*draws=0' "$OUT/depth.trace" 2>/dev/null; then
  ok "D: --world-depth draws no sky (state=unavailable draws=0)"
else
  bad "D: --world-depth did not record state=unavailable draws=0"
fi

# ---- E: an explicit key still wins -----------------------------------------
ISO_CONTENT=$'[renderer]\npreset=1\nskybox=0\n'
capture explicitoff
ISO_CONTENT=""
if grep -aq 'gpu: world sky state=unavailable.*draws=0' "$OUT/explicitoff.trace" 2>/dev/null; then
  ok "E: preset=1 with skybox=0 draws no sky (the explicit key wins)"
else
  bad "E: preset=1 with skybox=0 drew the sky (the explicit key lost)"
fi
# The picture, not a population constant: the key must produce exactly what the
# CLI flag produces, so "the key lost" and "the flag works" cannot both be true.
explicit_diff=$("$PYTHON" world-sky-gate-account.py "$OUT/explicitoff.bmp" \
  --diff "$OUT/skyoff.bmp" 2>/dev/null | sed -n 's/^frame_diff=\(-\{0,1\}[0-9]*\).*/\1/p')
[ "${explicit_diff:-}" = "0" ] \
  && ok "E: the explicit-key frame is the same picture as --no-world-sky (frame_diff=0)" \
  || bad "E: the explicit-key frame differs from --no-world-sky by ${explicit_diff:-<none>} px (expected 0)"

# ---- cost, recorded not asserted -------------------------------------------
fps=$(awk -F, 'NR > 1 && $4 >= 1000 && $3 > 0 { v[++n] = $3 }
  END { if (!n) exit 1
        for (i = 1; i <= n; i++) for (j = i + 1; j <= n; j++) if (v[j] < v[i]) { t = v[i]; v[i] = v[j]; v[j] = t }
        print v[int((n + 1) / 2)] }' "$OUT/default.csv" 2>/dev/null)
if [ -n "${fps:-}" ]; then
  ms=$(awk -v f="$fps" 'BEGIN { printf "%.2f", 1000.0 / f }')
  echo "  INFO A: preset-0 steady fps=$fps (frame time ${ms} ms) -- record it in the task doc;"
  echo "        the before-W-1 number cannot be produced by this build"
else
  echo "  INFO A: no metrics rows (the capture did not write default.csv)"
fi

echo "W-1 SKY GATE: $pass passed, $fail failed"
[ "$fail" = 0 ] || exit 1
exit 0
