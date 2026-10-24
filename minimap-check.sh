#!/usr/bin/env bash
# minimap-check.sh -- T69 gate: the minimap's PURE logic.
#
# WHY THIS GATE EXISTS
#   T69's minimap has a drawing half and a logic half. This gate owns the logic
#   half only: world->pixel projection, the world<->pixel round trip, the y
#   flip, "which player did I click", and the collapsed/zoom/reset state
#   machine. Every one of those is a value, so every assertion here is a number
#   from the product's own minimap.cpp -- not a screenshot, not a second copy of
#   the formula.
#
# WHAT IT DOES NOT CLAIM
#   It does not claim that a panel appears, that the overview bitmap looks like
#   the map, or that a click lands on the right dot on screen. Those need T67's
#   HUD primitive renderer and are NOT RUN here, stated as such in the report.
#
# THE EXPECTED PIXELS ARE HAND-DERIVED, NOT COPIED FROM THE PROBE
#   The probe's synthetic map is 4096x4096 world units drawn into 512x512
#   pixels, so:
#       scale   = min(512/4096, 512/4096) = 0.125 px per unit
#       offsetX = (512 - 4096*0.125)/2 - 0*0.125 = 0
#       offsetY = (512 - 4096*0.125)/2 + 4096*0.125 = 512
#       px = x*0.125 + 0        py = 512 - y*0.125
#   which gives (0,0)->(0,512), (4096,4096)->(512,0), (1024,3072)->(128,128),
#   (2048,1024)->(256,384). If the formula and the expectation were both copied
#   from the probe they would agree while both were wrong.
#
# Usage: bash minimap-check.sh
# Exit:  0 = every assertion held; 1 = at least one FAIL; 2 = probe missing or
#        could not run (NOT RUN is never PASS).

set -uo pipefail
cd "$(dirname "$0")"
export LC_ALL=C

PROBE=native/build-nmake/minimap_probe.exe
OUT="${MINIMAP_OUT:-.scratch/minimap}"
mkdir -p "$OUT"

ok_count=0
bad_count=0
fail=0

ok() {
  echo "OK   $*"
  ok_count=$((ok_count + 1))
}

bad() {
  echo "FAIL $*"
  bad_count=$((bad_count + 1))
  fail=1
}

# Tolerant numeric equality: num_eq <actual> <expected> <tolerance>.
num_eq() {
  awk -v a="$1" -v b="$2" -v tol="$3" 'BEGIN { d = a - b; if (d < 0) d = -d; exit !(d <= tol) }'
}

num_lt() {
  awk -v a="$1" -v b="$2" 'BEGIN { exit !(a < b) }'
}

# field <line> <key> -> the token after " key=" on that line.
field() {
  printf '%s\n' "$1" | sed -n "s/.*[[:space:]]$2=\([^[:space:]]*\).*/\1/p"
}

for tool in grep sed awk; do
  command -v "$tool" >/dev/null 2>&1 || { echo "FATAL: missing tool: $tool"; exit 2; }
done

# --- 1. the subject exists and runs -----------------------------------------
# A missing probe is NOT RUN, never PASS: a gate that reads green because there
# was nothing to read is the defect this whole rule set exists for.
if [ ! -f "$PROBE" ]; then
  echo "NOT RUN: probe missing: $PROBE"
  echo "  build it with: bash build-target.sh minimap_probe"
  echo "MINIMAP-CHECK=FAIL"
  echo "RC=2"
  exit 2
fi
"$PROBE" --minimap-test > "$OUT/probe.txt" 2> "$OUT/probe.err"
probe_rc=$?
if [ "$probe_rc" -ne 0 ]; then
  echo "NOT RUN: probe exited rc=$probe_rc"
  sed 's/^/  /' "$OUT/probe.err" | head -10
  echo "MINIMAP-CHECK=FAIL"
  echo "RC=$probe_rc"
  exit "$probe_rc"
fi
if ! grep -q '^minimap: probe done ok=1$' "$OUT/probe.txt"; then
  echo "NOT RUN: probe printed no completion line (output empty or truncated)"
  echo "MINIMAP-CHECK=FAIL"
  echo "RC=3"
  exit 3
fi
ok "probe ran: $PROBE --minimap-test rc=0 ($(wc -l < "$OUT/probe.txt" | tr -d ' ') lines)"

# --- 2. projection constants -------------------------------------------------
proj_line=$(grep -m1 '^minimap: projection ' "$OUT/probe.txt" || true)
if [ -z "$proj_line" ]; then
  bad "projection: no projection reading"
else
  p_valid=$(field "$proj_line" valid)
  p_scale=$(field "$proj_line" scale)
  p_offx=$(field "$proj_line" offsetX)
  p_offy=$(field "$proj_line" offsetY)
  p_flip=$(field "$proj_line" yFlip)
  if [ "$p_valid" = "1" ] && num_eq "$p_scale" 0.125 0.000001 \
     && num_eq "$p_offx" 0 0.000001 && num_eq "$p_offy" 512 0.000001 \
     && [ "$p_flip" = "1" ]; then
    ok "projection valid=1 scale=$p_scale offsetX=$p_offx offsetY=$p_offy yFlip=$p_flip"
  else
    bad "projection valid=${p_valid:-<missing>} scale=${p_scale:-<missing>} offsetX=${p_offx:-<missing>} offsetY=${p_offy:-<missing>} yFlip=${p_flip:-<missing>} expected valid=1 scale=0.125 offsetX=0 offsetY=512 yFlip=1"
  fi
fi

# --- 3. four known world points, pixel-exact ---------------------------------
while read -r idx expected_x expected_y; do
  [ -n "$idx" ] || continue
  point_line=$(grep -F "minimap: point index=$idx " "$OUT/probe.txt" || true)
  if [ -z "$point_line" ]; then
    bad "point index=$idx: no reading"
    continue
  fi
  got_x=$(field "$point_line" px)
  got_y=$(field "$point_line" py)
  got_inside=$(field "$point_line" inside)
  if num_eq "$got_x" "$expected_x" 0.001 && num_eq "$got_y" "$expected_y" 0.001 \
     && [ "$got_inside" = "1" ]; then
    ok "point index=$idx px=$got_x py=$got_y inside=$got_inside (expected $expected_x,$expected_y tol=0.001)"
  else
    bad "point index=$idx px=${got_x:-<missing>} py=${got_y:-<missing>} inside=${got_inside:-<missing>} expected=$expected_x,$expected_y tol=0.001"
  fi
done <<'POINTS'
0 0 512
1 512 0
2 128 128
3 256 384
POINTS

# --- 4. round trip < 1e-4 px -------------------------------------------------
roundtrip_count=$(grep -c '^minimap: roundtrip index=' "$OUT/probe.txt" || true)
if [ "$roundtrip_count" -ne 4 ]; then
  bad "roundtrip: expected 4 readings, got $roundtrip_count"
fi
for idx in 0 1 2 3; do
  rt_line=$(grep -F "minimap: roundtrip index=$idx " "$OUT/probe.txt" || true)
  rt_err=$(field "$rt_line" errorPx)
  if [ -n "$rt_err" ] && num_lt "$rt_err" 0.0001; then
    ok "roundtrip index=$idx errorPx=$rt_err < 1e-4 px"
  else
    bad "roundtrip index=$idx errorPx=${rt_err:-<missing>} not < 1e-4 px"
  fi
done

# --- 5. the y flip points the right way --------------------------------------
# World +y is north/up; pixel +y is down. A point with a LARGER world y must
# have a SMALLER pixel y. Asserted on the two corner points so the direction is
# checked, not merely "some flip happened".
py_low=$(field "$(grep -F 'minimap: point index=0 ' "$OUT/probe.txt" || true)" py)
py_high=$(field "$(grep -F 'minimap: point index=1 ' "$OUT/probe.txt" || true)" py)
if [ -n "$py_low" ] && [ -n "$py_high" ] && num_lt "$py_high" "$py_low" \
   && num_eq "$py_high" 0 0.001 && num_eq "$py_low" 512 0.001; then
  ok "yflip: world y=4096 -> py=$py_high is above world y=0 -> py=$py_low"
else
  bad "yflip: world y=4096 -> py=${py_high:-<missing>} must be smaller than world y=0 -> py=${py_low:-<missing>} (expected 0 < 512)"
fi

# --- 6. hit test: nearest / tie / outside / invisible / order ----------------
hit_case() { # hit_case <case> <expected> <name>
  local case_name="$1" expected="$2" label="$3"
  local line got
  line=$(grep -F "minimap: hittest case=$case_name " "$OUT/probe.txt" || true)
  got=$(field "$line" result)
  if [ "$got" = "$expected" ]; then
    ok "$label: result=$got (expected $expected)"
  else
    bad "$label: result=${got:-<missing>} (expected $expected)"
  fi
}
hit_case nearest 3 "hittest nearest"
hit_case tie 0 "hittest tie"
hit_case outside -1 "hittest outside"
hit_case invisible -1 "hittest invisible"

order_line=$(grep -m1 '^minimap: hittest order ' "$OUT/probe.txt" || true)
if [ -z "$order_line" ]; then
  bad "hittest order: no reading"
else
  f_near=$(field "$order_line" forward_nearest)
  r_near=$(field "$order_line" reversed_nearest)
  f_tie=$(field "$order_line" forward_tie)
  r_tie=$(field "$order_line" reversed_tie)
  if [ "$f_near" = "3" ] && [ "$r_near" = "3" ] && [ "$f_tie" = "0" ] && [ "$r_tie" = "0" ]; then
    ok "hittest order: forward/reversed agree (nearest $f_near/$r_near, tie $f_tie/$r_tie)"
  else
    bad "hittest order: forward/reversed disagree or wrong (nearest $f_near/$r_near expected 3/3, tie $f_tie/$r_tie expected 0/0)"
  fi
fi

# --- 7. the panel starts collapsed ------------------------------------------
initial_line=$(grep -m1 '^minimap: panel initial ' "$OUT/probe.txt" || true)
initial_collapsed=$(field "$initial_line" collapsed)
if [ "$initial_collapsed" = "1" ]; then
  ok "panel collapsed initial: collapsed=1"
else
  bad "panel collapsed initial: collapsed=${initial_collapsed:-<missing>} (expected 1)"
fi

expand_line=$(grep -m1 '^minimap: panel after_expand ' "$OUT/probe.txt" || true)
collapse_line=$(grep -m1 '^minimap: panel after_collapse ' "$OUT/probe.txt" || true)
exp_c=$(field "$expand_line" collapsed)
exp_n=$(field "$expand_line" expandCount)
col_c=$(field "$collapse_line" collapsed)
col_n=$(field "$collapse_line" collapseCount)
if [ "$exp_c" = "0" ] && [ "$exp_n" = "1" ] && [ "$col_c" = "1" ] && [ "$col_n" = "1" ]; then
  ok "panel expand/collapse: collapsed $exp_c then $col_c, expandCount=$exp_n collapseCount=$col_n"
else
  bad "panel expand/collapse: collapsed ${exp_c:-<missing>}/${col_c:-<missing>} expandCount=${exp_n:-<missing>} collapseCount=${col_n:-<missing>} (expected 0/1 1 1)"
fi

# --- 8. zoom 2x scales around the centre, and hit testing follows it --------
zoom_line=$(grep -m1 '^minimap: panel after_zoom ' "$OUT/probe.txt" || true)
zoom_value=$(field "$zoom_line" zoom)
zoom_changes=$(field "$zoom_line" zoomChanges)
if num_eq "$zoom_value" 2 0.000001 && [ "$zoom_changes" = "1" ]; then
  ok "panel zoom: zoom=$zoom_value zoomChanges=$zoom_changes"
else
  bad "panel zoom: zoom=${zoom_value:-<missing>} zoomChanges=${zoom_changes:-<missing>} (expected 2.000000 and 1)"
fi

z1_line=$(grep -m1 '^minimap: zoom1 ' "$OUT/probe.txt" || true)
z2_line=$(grep -m1 '^minimap: zoom2 ' "$OUT/probe.txt" || true)
z1_dist=$(field "$z1_line" distance)
z2_dist=$(field "$z2_line" distance)
z2_px=$(field "$z2_line" px)
z2_py=$(field "$z2_line" py)
if [ -n "$z1_dist" ] && [ -n "$z2_dist" ] && awk -v a="$z1_dist" -v b="$z2_dist" 'BEGIN { if (a <= 0) exit 1; r = b / a; exit !(r > 1.999 && r < 2.001) }'; then
  ok "zoom2 distance: $z2_dist = 2 x $z1_dist from the panel centre"
else
  bad "zoom2 distance: zoom1=${z1_dist:-<missing>} zoom2=${z2_dist:-<missing>} (expected zoom2 = 2 x zoom1)"
fi
if num_eq "$z2_px" 256 0.001 && num_eq "$z2_py" 512 0.001; then
  ok "zoom2 pixel: point=3 px=$z2_px py=$z2_py (expected 256,512)"
else
  bad "zoom2 pixel: point=3 px=${z2_px:-<missing>} py=${z2_py:-<missing>} (expected 256,512)"
fi

zoom2_rt=$(field "$(grep -m1 '^minimap: roundtrip_zoom2 ' "$OUT/probe.txt" || true)" errorPx)
if [ -n "$zoom2_rt" ] && num_lt "$zoom2_rt" 0.0001; then
  ok "zoom2 roundtrip: errorPx=$zoom2_rt < 1e-4 px"
else
  bad "zoom2 roundtrip: errorPx=${zoom2_rt:-<missing>} not < 1e-4 px"
fi

zoom2_hit_line=$(grep -m1 '^minimap: zoom2_hittest ' "$OUT/probe.txt" || true)
zoom2_hit=$(field "$zoom2_hit_line" result)
if [ "$zoom2_hit" = "0" ]; then
  ok "zoom2 hittest: result=0 (the hit test uses the zoomed projection)"
else
  bad "zoom2 hittest: result=${zoom2_hit:-<missing>} (expected 0: the hit test must use the same zoomed projection it draws with)"
fi

# --- 9. unavailable maps carry a named reason -------------------------------
unavailable_line=$(grep -m1 '^minimap: unavailable map=koth_bagel_rc13 ' "$OUT/probe.txt" || true)
un_avail=$(field "$unavailable_line" available)
un_proj=$(field "$unavailable_line" projectionValid)
un_reason=$(field "$unavailable_line" reason)
if [ "$un_avail" = "0" ] && [ "$un_proj" = "0" ] && [ -n "$un_reason" ] && [ "$un_reason" != "-" ]; then
  ok "unavailable: available=0 projectionValid=0 reason=$un_reason"
else
  bad "unavailable: available=${un_avail:-<missing>} projectionValid=${un_proj:-<missing>} reason=${un_reason:-<missing>} (expected 0/0 and a non-empty reason)"
fi

degenerate_line=$(grep -m1 '^minimap: unavailable map=degenerate ' "$OUT/probe.txt" || true)
deg_avail=$(field "$degenerate_line" available)
deg_reason=$(field "$degenerate_line" reason)
if [ "$deg_avail" = "0" ] && [ -n "$deg_reason" ] && [ "$deg_reason" != "-" ]; then
  ok "unavailable degenerate: available=0 reason=$deg_reason"
else
  bad "unavailable degenerate: available=${deg_avail:-<missing>} reason=${deg_reason:-<missing>} (expected 0 and a non-empty reason)"
fi

# --- 10. the T65 ledger constants are what this builder produces --------------
# Expected values are NOT invented here. They are the projection constants T65
# published after auditing the real BSPs, in
# evidence/t65-minimap/data/projection_ledger.json
#   sha256 e2d35aae52942cba14157b5f6fb7f798d161302c52c43b084a3c7427af4a67bc
# for cp_snakewater_final1 and pl_badwater. The canvas rectangles fed to the
# builder are that ledger's projection.minX/maxX/minY/maxY at 1024x1024; the
# expected scale is its scalePxPerUnit. They are fixture INPUTS here, not
# product constants: the product derives the rectangle from BSP bounds at load
# time (see native/src/minimap.cpp usableBounds/makeMinimapProjection).
while read -r ledger_map ledger_expected; do
  [ -n "$ledger_map" ] || continue
  ledger_line=$(grep -m1 "^minimap: ledger map=$ledger_map " "$OUT/probe.txt" || true)
  ledger_scale=$(field "$ledger_line" scale)
  ledger_valid=$(field "$ledger_line" valid)
  if [ -n "$ledger_scale" ] && [ "$ledger_valid" = "1" ] && num_eq "$ledger_scale" "$ledger_expected" 0.000001; then
    ok "ledger $ledger_map: scale=$ledger_scale matches T65 ledger $ledger_expected (tol 1e-6)"
  else
    bad "ledger $ledger_map: scale=${ledger_scale:-<missing>} valid=${ledger_valid:-<missing>} expected $ledger_expected (tol 1e-6; source evidence/t65-minimap/data/projection_ledger.json)"
  fi
done <<'LEDGER'
cp_snakewater_final1 0.07829320806420043
pl_badwater 0.1309328968903437
LEDGER

# --- summary ----------------------------------------------------------------
echo "assertions_ok=$ok_count assertions_bad=$bad_count"
if [ "$fail" -eq 0 ]; then
  echo "MINIMAP-CHECK=PASS"
  echo "RC=0"
  exit 0
fi
echo "MINIMAP-CHECK=FAIL"
echo "RC=1"
exit 1
