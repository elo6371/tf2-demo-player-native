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
#   C. NEGATIVE CONTROL. --no-world-sky on the same pose must reproduce the
#      pre-fix frame: 2,072 enclosed clear px, "state=unavailable draws=0".
#      If this stops reproducing, the fix is not the thing that filled B.
#   D. THE DEPTH VIEW STILL DOES NOT DRAW THE SKY (T52's contract: a depth
#      frame is scalar). --world-depth must stay "state=unavailable draws=0".
#   E. AN EXPLICIT KEY STILL WINS. A settings file that says preset=1 AND
#      skybox=0 must NOT draw the sky: the precedence is CLI > explicit key >
#      compiled default, and the preset must have stopped deciding this in either
#      direction. Before W-1 preset=1 forced the sky on; now the key does what it
#      says even under the preset that used to disable nothing and enable it.
#
# COST: four captures at ~60 s each on the reference machine. --reuse keeps
# frames and traces already under the output directory.
#
# Usage: bash world-sky-gate-check.sh [--reuse]
# Exit:  0 = every assertion held; 1 = an assertion or a capture failed;
#        2 = a required input (product, demo, python) is missing.
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

[ -x "$PRODUCT" ] || { echo "world-sky-gate: no product at $PRODUCT (build first)"; exit 2; }
[ -f "$DEMO" ] || { echo "world-sky-gate: no demo at $DEMO"; exit 2; }
command -v "$PYTHON" >/dev/null 2>&1 || { echo "world-sky-gate: no python ($PYTHON)"; exit 2; }

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
capture default

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

# ---- B: the user-visible population is gone --------------------------------
default_account=$(account "$OUT/default.bmp")
default_total=$(field "$default_account" clear_total)
default_enclosed=$(field "$default_account" enclosed_clear)
[ "${default_enclosed:-}" = "0" ] \
  && ok "B: enclosed clear px == 0 (the 2,072 px population)" \
  || bad "B: enclosed clear px = ${default_enclosed:-<none>} (expected 0)"
[ "${default_total:-}" = "0" ] \
  && ok "B: no clear-colour pixel remains in the frame" \
  || bad "B: ${default_total:-<none>} clear-colour px remain (expected 0)"

# ---- C: negative control ---------------------------------------------------
capture skyoff --no-world-sky
skyoff_account=$(account "$OUT/skyoff.bmp")
skyoff_enclosed=$(field "$skyoff_account" enclosed_clear)
[ "${skyoff_enclosed:-}" = "2072" ] \
  && ok "C: --no-world-sky reproduces 2,072 enclosed clear px" \
  || bad "C: --no-world-sky enclosed clear = ${skyoff_enclosed:-<none>} (expected 2072)"
if grep -aq 'gpu: world sky state=unavailable.*draws=0' "$OUT/skyoff.trace" 2>/dev/null; then
  ok "C: --no-world-sky records state=unavailable draws=0"
else
  bad "C: --no-world-sky did not record state=unavailable draws=0"
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
explicit_account=$(account "$OUT/explicitoff.bmp")
explicit_enclosed=$(field "$explicit_account" enclosed_clear)
[ "${explicit_enclosed:-}" = "2072" ] \
  && ok "E: preset=1 with skybox=0 reproduces the 2,072 px population" \
  || bad "E: preset=1 with skybox=0 enclosed clear = ${explicit_enclosed:-<none>} (expected 2072)"

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
