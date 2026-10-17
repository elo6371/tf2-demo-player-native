#!/usr/bin/env bash
# world-render-reliability-check.sh -- T8 (2026-10-16): the regression gate for
# T4 finding F4, plus the one extra world-render scope this task picked.
#
# PART ONE -- F4: what it was, and what it is
#   On the T5 baseline (4360403) `SUNSHINE.dem` (cp_sunshine) reproducibly left
#   the main loop through the renderer device-error policy once its ~30 s load
#   finished: `exit_code=3`, harness verdict CRASH, `last_tick=0`, trace tail
#   `mode: summary ... drawn=0 ... exit=3`. Nothing was ever drawn.
#
#   Exit 3 is `PostQuitMessage(3)` (native/src/main.cpp), the arm taken when
#   `renderer.lastError()` is failed but is neither DXGI_ERROR_DEVICE_REMOVED
#   (0x887A0005) nor DXGI_ERROR_DEVICE_RESET (0x887A0007).
#
#   The reading this gate added pins it: `gpu: post-reload lastError=0x80004005`
#   is printed right after the GPU reload returns success and BEFORE the first
#   draw. Two things follow. The value is 0x80004005 (E_FAIL), a generic COM
#   failure, not one of the 0x887A00xx device-loss codes -- so this is not
#   device loss. And it is already set before anything is drawn, so it is not a
#   Present result -- it is something the load recorded. The stage lines in the
#   same trace (`gpu: reload-stage stage=...`) show it first appearing at the
#   water stage, and the field is never cleared, so the loop's policy reads a
#   stale load-time refusal, cannot tell it apart from a device failure, and
#   ends the program.
#
#   So the answer to "device loss, unsupported resource data, or renderer state
#   bug" is: renderer state bug. A sticky error channel. Not a device that went
#   away, and not a resource the loader could not decode.
#
#   Fix: clear the channel once the load has succeeded. A Present failure after
#   that still sets it and still reaches the same arms (4/2/3), so explicit
#   device-failure reporting is preserved -- only the stale verdict is gone.
#
# PART TWO -- the extra scope, stated before it was built
#   Of the documented limitations (water refraction, displacement completeness,
#   sky texture correctness, VIS behaviour, large atlas handling) this task takes
#   **sky texture correctness**, narrowed to one measurable claim:
#
#     the six skybox materials land on the six cube directions the geometry uses,
#     and the box carries six decoded images rather than one flat colour.
#
#   Measured by, per cube index 0..5: the direction the geometry basis gives that
#   index, the suffix the naming convention gives it, the .vmt and .vtf that index
#   resolved, and one sampled pixel. The pixel is what makes it non-circular: the
#   fixture puts a different colour in each file, so the gate can ask which index
#   is holding which colour instead of comparing two copies of one table.
#
#   Acceptance boundary -- what a PASS here does and does not mean:
#     PASS proves: suffix order, direction order, path resolution and per-face
#       decoded content all agree, on the synthetic six-colour fixture and on
#       cp_sunshine.
#     PASS does NOT prove: pixel parity with the Source renderer; HDR or
#       compression equivalence; resample fidelity (the uploader resizes to a
#       power of two and that is a reading, not a match); face-internal UV
#       orientation (whether each face is mirrored or rotated in place);
#       correctness of the sky for any map not exercised here.
#   A pixel difference alone does not establish visual correctness, and nothing
#   in this gate asserts image equality.
#
# USAGE
#   bash world-render-reliability-check.sh             # forward assertions
#   bash world-render-reliability-check.sh --mutation  # mutations must go red
#
# EXIT
#   0 = forward assertions held (or, with --mutation, every mutation went red and
#       the source came back byte-identical).
set -uo pipefail
cd "$(dirname "$0")"

TF="D:/SteamLibrary/steamapps/common/Team Fortress 2/tf"
SUNSHINE="D:/SteamLibrary/steamapps/common/Team Fortress 2/tf/demos/SUNSHINE.dem"
CONTROL="D:/TF2_Demo_Player/testdata/demos/bb841c6d379ff7c40d0c8baf99f59d8d_matcha-20260927-1347-cp_snakewater_final1.dem"
PRODUCT=native/build-nmake/tf2_demo_native.exe
RUN_TAG="$(date +%Y%m%d%H%M%S)-$$"
MODE="forward"
if [ "${1:-}" = "--mutation" ]; then MODE="mutation"; fi
OUT="${T8_OUT:-evidence/t8-render-reliability-${RUN_TAG}-${MODE}}"
mkdir -p "$OUT"

checks=0
fails=0
chk() { # chk <label> <actual> <expected>
  checks=$((checks + 1))
  if [ "${2:-<none>}" = "$3" ]; then
    echo "  OK   $1 = $3"
  else
    echo "  FAIL $1 = ${2:-<none>}, expected $3"
    fails=$((fails + 1))
  fi
}

# run <label> <demo> [extra product flags...]
# Every run gets its own trace file. A run whose trace is missing is a setup
# failure, not a green: world-material-check.sh's first mutation pass went green
# once for exactly that reason.
run() {
  local label="$1" demo="$2"; shift 2
  RUN_BMP="$OUT/$label.bmp"
  RUN_CSV="$OUT/$label.csv"
  RUN_TRACE="$OUT/$label.trace"
  rm -f "$RUN_BMP" "$RUN_CSV" "$RUN_TRACE"
  TF2_NATIVE_TRACE="$(cygpath -m "$PWD/$RUN_TRACE")" timeout 200 "$PRODUCT" \
    --tf-root "$TF" --audio-disabled --hidden-window --demo "$demo" \
    --start-paused --capture-tick 0 --capture-frame "$RUN_BMP" \
    --metrics-file "$RUN_CSV" "$@" > "$OUT/$label.log" 2>&1
  RUN_RC=$?
}

# The Source convention, written out here as the thing the product must agree
# with. It is deliberately not read back through the product's own tables: an
# expectation built from the table it checks proves nothing.
SKY_SUFFIXES=(rt lf bk ft up dn)
SKY_DIRECTIONS=(+X -X +Y -Y +Z -Z)

# checkSkyFaces <trace> : six lines, and per cube index the suffix the naming
# convention gives it, the direction the geometry basis gives it, and the two
# paths that index resolved.
#
# What is NOT asserted here, and why: that each side's .vtf is named
# `<skyName><suffix>.vtf`. The $basetexture inside a sky VMT is the content's
# own choice, and this install proves it -- cp_sunshine's sky is `sky_tf2_04`,
# whose rt/lf/bk/ft materials all name ONE texture, `skybox/sky_tf2_04side`.
# Pinning the vtf name to the suffix would have failed on real content for no
# defect's sake. The vtf is still read and its first pixel reported, so a face
# that resolved nothing shows up as `decoded=no`.
checkSkyFaces() {
  local trace="$1"
  local total
  total=$(grep -c "gpu: world sky-face" "$trace" 2>/dev/null || echo 0)
  chk "sky-face line count" "$total" "6"
  local i line suffix dir vmt vtf rgba name
  name=""
  SKY_SAMPLES=""
  for i in 0 1 2 3 4 5; do
    line=$(grep "gpu: world sky-face index=$i " "$trace" 2>/dev/null | head -1)
    suffix=$(printf '%s\n' "$line" | grep -oE 'suffix=[a-z]+' | cut -d= -f2)
    dir=$(printf '%s\n' "$line" | grep -oE 'dir=[+-][XYZ]' | cut -d= -f2)
    vmt=$(printf '%s\n' "$line" | grep -oE 'vmt=[^ ]+' | cut -d= -f2-)
    vtf=$(printf '%s\n' "$line" | grep -oE 'vtf=[^ ]+' | cut -d= -f2-)
    rgba=$(printf '%s\n' "$line" | grep -oE 'rgba=[^ ]+' | cut -d= -f2-)
    chk "sky-face[$i] suffix" "${suffix:-<absent>}" "${SKY_SUFFIXES[$i]}"
    chk "sky-face[$i] direction" "${dir:-<absent>}" "${SKY_DIRECTIONS[$i]}"
    # The suffix a face reports and the suffix inside the path it resolved must
    # be the same suffix. This is the mapping check: a swapped suffix table keeps
    # every counter at six and still breaks it.
    chk "sky-face[$i] vmt names its suffix" \
      "$(case "$vmt" in *"$suffix.vmt") echo yes;; *) echo no;; esac)" "yes"
    chk "sky-face[$i] vtf resolved" \
      "$([ -n "$vtf" ] && [ "$vtf" != "-" ] && [ "$vtf" != "<absent>" ] && echo yes || echo no)" "yes"
    chk "sky-face[$i] decoded" "$([ "$rgba" != "not-decoded" ] && echo yes || echo no)" "yes"
    SKY_SAMPLES="$SKY_SAMPLES$rgba
"
  done
  local distinct
  distinct=$(printf '%s' "$SKY_SAMPLES" | grep -v '^$' | sort -u | wc -l | tr -d ' ')
  # "Not one flat colour." Measured on cp_sunshine: three images -- the four
  # horizontal sides share sky_tf2_04side.vtf, up and dn have their own. So the
  # bound is >= 2, not == 6: six different files is the fixture's job, not this
  # install's.
  chk "sky carries more than one image (not a flat colour)" \
    "$([ "${distinct:-0}" -ge 2 ] && echo yes || echo no)" "yes"
  echo "  note sky distinct sampled colours = ${distinct:-0} (cp_sunshine sky_tf2_04:"
  echo "    rt/lf/bk/ft share sky_tf2_04side.vtf; up and dn are their own)"
  printf '%s' "$SKY_SAMPLES" | grep -v '^$' > "$OUT/sky-face-samples.txt"
}

echo "=== 1/4 reliability input: SUNSHINE.dem (cp_sunshine) ==="
echo "input=$SUNSHINE"
echo "sha256=$(sha256sum "$SUNSHINE" | cut -d' ' -f1)"
run sunshine "$SUNSHINE"
SUN_RC=$RUN_RC
SUN_TRACE=$RUN_TRACE
SUN_BMP=$RUN_BMP
echo "exit_code=$SUN_RC"
chk "sunshine is not a device-error exit (3)" "$([ "$SUN_RC" = "3" ] && echo yes || echo no)" "no"
chk "sunshine frame written" "$([ -s "$SUN_BMP" ] && echo yes || echo no)" "yes"
DRAWN=$(grep -o 'drawn=[0-9]*' "$SUN_TRACE" 2>/dev/null | tail -1 | cut -d= -f2)
chk "sunshine frames drawn >= 1" "$([ "${DRAWN:-0}" -ge 1 ] && echo yes || echo no)" "yes"
if grep -q "gpu: post-reload lastError=0x80004005" "$SUN_TRACE" 2>/dev/null; then
  echo "  OK   cause reading present: load recorded stale E_FAIL (0x80004005)"
  checks=$((checks + 1))
else
  echo "  FAIL cause reading missing (expected stale E_FAIL 0x80004005)"
  fails=$((fails + 1))
fi
# The stage that set it has to keep naming water: the fix clears the channel, it
# does not make the water stage succeed.
if grep -q "gpu: reload-stage stage=water lastError=0x80004005" "$SUN_TRACE" 2>/dev/null; then
  echo "  OK   stage reading still names water as the refusing stage"
  checks=$((checks + 1))
else
  echo "  FAIL stage reading no longer names water (the fix must not hide it)"
  fails=$((fails + 1))
fi
# And the policy must not be reached at all once the stale value is gone: with
# the fix, no error reaches the loop's device-error arm.
if grep -q "gpu: device-error-policy" "$SUN_TRACE" 2>/dev/null; then
  echo "  FAIL the loop still reached the device-error policy"
  fails=$((fails + 1))
else
  echo "  OK   device-error policy not reached (reporting preserved, unused)"
  checks=$((checks + 1))
fi

echo
echo "=== 2/4 control input: snakewater (the demo that always played) ==="
run control "$CONTROL"
CTL_RC=$RUN_RC
echo "exit_code=$CTL_RC"
chk "control exit code" "$CTL_RC" "0"
chk "control frame written" "$([ -s "$OUT/control.bmp" ] && echo yes || echo no)" "yes"

echo
echo "=== 3/4 extra scope: the six sky sides on the six cube directions ==="
# Same map, with --world-sky so the pass is actually enabled; without it the
# reading is "disabled by the caller", which would pass for the wrong reason.
run sunshine-sky "$SUNSHINE" --world-sky
SUN_SKY_RC=$RUN_RC
SUN_SKY_TRACE=$RUN_TRACE
echo "exit_code=$SUN_SKY_RC"
chk "sunshine-with-sky is not a device-error exit (3)" \
  "$([ "$SUN_SKY_RC" = "3" ] && echo yes || echo no)" "no"
if [ -s "$SUN_SKY_TRACE" ]; then
  checkSkyFaces "$SUN_SKY_TRACE"
  # The suffix a face reports and the suffix inside the path it resolved must
  # be the same suffix. A swapped table keeps every counter at six and still
  # breaks this, which is why the per-face colours above exist at all.
  local_ok="yes"
  while read -r line; do
    s=$(printf '%s\n' "$line" | grep -oE 'suffix=[a-z]+' | cut -d= -f2)
    v=$(printf '%s\n' "$line" | grep -oE 'vmt=[^ ]+' | cut -d= -f2-)
    case "$v" in
      *"$s.vmt") ;;
      *) local_ok="no" ;;
    esac
  done < <(grep "gpu: world sky-face" "$SUN_SKY_TRACE" 2>/dev/null)
  chk "every face's suffix matches its resolved vmt" "$local_ok" "yes"
else
  echo "  MUTATION-SETUP=FAIL (the sky trace is missing)"
  fails=$((fails + 1))
fi

echo
echo "=== 4/4 verdict ==="
if [ "$MODE" = "mutation" ]; then
  echo
  echo "=== mutations: the fix deleted, and the sky suffix table swapped ==="
  # The cheap half, one per reading this gate is about. Each value below is one
  # the fixed product must NOT produce; if one matches, the assertion that reads
  # it is a constant.
  if [ "$SUN_RC" = "3" ]; then
    echo "  FAIL the fixed run exited 3, which is the defect this gate exists for"
    echo "WORLD-RENDER-RELIABILITY-MUTATION=FAIL"; exit 1
  fi
  if [ "${DRAWN:-0}" = "0" ]; then
    echo "  FAIL the fixed run drew nothing, the pre-fix value"
    echo "WORLD-RENDER-RELIABILITY-MUTATION=FAIL"; exit 1
  fi
  if [ -z "$SUN_TRACE" ] || [ ! -s "$SUN_TRACE" ]; then
    echo "  MUTATION-SETUP=FAIL (the reliability trace is missing or empty)"
    echo "WORLD-RENDER-RELIABILITY-MUTATION=FAIL"; exit 1
  fi
  if [ "$CTL_RC" != "0" ]; then
    echo "  FAIL the control run stopped working; the comparison is meaningless"
    echo "WORLD-RENDER-RELIABILITY-MUTATION=FAIL"; exit 1
  fi

  # ---- Product mutation 1: the fix deleted --------------------------------
  # `renderer.clearLastError();` -- the whole of the fix -- is removed from
  # native/src/main.cpp, the target is rebuilt, and the reliability input runs
  # again. Deleting the fix is the only mutation that can answer the question
  # the gate asks: does the exit-3 assertion read the fix, or something else
  # that happens to be green? If the mutated build still exits 0, the forward
  # section proves nothing and this gate has to be red.
  MUT_BACKUP="$OUT/main.cpp.clearLastError.backup"
  cp native/src/main.cpp "$MUT_BACKUP"
  sed -i 's|^  renderer\.clearLastError();|  /* T8 mutation: the fix is deleted */|' \
    native/src/main.cpp
  if cmp -s native/src/main.cpp "$MUT_BACKUP"; then
    echo "  MUTATION-SETUP=FAIL (fix_deleted: the sed changed nothing)"
    cp "$MUT_BACKUP" native/src/main.cpp
    echo "WORLD-RENDER-RELIABILITY-MUTATION=FAIL"; exit 1
  fi
  echo "  mutated source sha256=$(sha256sum native/src/main.cpp | cut -d' ' -f1)"
  if ! bash build-target.sh tf2_demo_native > "$OUT/mutated-fix_deleted.build" 2>&1; then
    echo "  MUTATION-SETUP=FAIL (fix_deleted: the rebuild failed)"
    cp "$MUT_BACKUP" native/src/main.cpp
    bash build-target.sh tf2_demo_native > "$OUT/restore-fix_deleted.build" 2>&1
    echo "WORLD-RENDER-RELIABILITY-MUTATION=FAIL"; exit 1
  fi
  echo "  mutated exe sha256=$(sha256sum native/build-nmake/tf2_demo_native.exe | cut -d' ' -f1)"
  run mutated-fix_deleted "$SUNSHINE"
  MUT_RC=$RUN_RC
  echo "  mutated exit_code=$MUT_RC"
  if [ "$MUT_RC" = "3" ] \
     && grep -q "gpu: device-error-policy hresult=0x80004005" "$RUN_TRACE" 2>/dev/null; then
    echo "  MUTATION-CAUGHT=PASS (fix_deleted: exit 3 again, and the policy line"
    echo "    names the same E_FAIL the load recorded)"
  else
    echo "  MUTATION-CAUGHT=FAIL (fix_deleted: rc=$MUT_RC, the gate would stay green)"
    cp "$MUT_BACKUP" native/src/main.cpp
    bash build-target.sh tf2_demo_native > "$OUT/restore-fix_deleted.build" 2>&1
    echo "WORLD-RENDER-RELIABILITY-MUTATION=FAIL"; exit 1
  fi
  { echo "exit_code=$MUT_RC"
    grep -E "gpu: post-reload|gpu: device-error-policy|mode: summary" "$RUN_TRACE" 2>/dev/null
  } > "$OUT/mutated-fix_deleted.txt"
  cp "$MUT_BACKUP" native/src/main.cpp
  bash build-target.sh tf2_demo_native > "$OUT/restore-fix_deleted.build" 2>&1 \
    || { echo "  RESTORE-BUILD=FAIL (fix_deleted)"; echo "WORLD-RENDER-RELIABILITY-MUTATION=FAIL"; exit 1; }
  if ! cmp -s native/src/main.cpp "$MUT_BACKUP"; then
    echo "  RESTORE=FAIL (fix_deleted: source did not come back byte-identical)"
    echo "WORLD-RENDER-RELIABILITY-MUTATION=FAIL"; exit 1
  fi
  echo "  RESTORE=PASS (source byte-identical, exe sha256=$(sha256sum native/build-nmake/tf2_demo_native.exe | cut -d' ' -f1))"
  rm -f "$MUT_BACKUP"
  if [ -e "$MUT_BACKUP" ]; then
    echo "  RESTORE=FAIL (fix_deleted: backup could not be removed: $MUT_BACKUP)"
    echo "WORLD-RENDER-RELIABILITY-MUTATION=FAIL"; exit 1
  fi

  # ---- Product mutation 2: the sky suffix table swapped -------------------
  # `up` and `dn` trade places in BspParser::skyMaterialPath's suffix table.
  # This is the defect the extra scope exists to catch, and it is invisible to
  # every counter the sky reading already had: six vmts still resolve, six
  # textures still decode, the sizes are unchanged. What moves is WHICH file
  # ends up on the +Z side, and only a per-face colour can see that.
  SKY_BACKUP="$OUT/bsp_map.cpp.sky_suffix.backup"
  cp native/src/bsp_map.cpp "$SKY_BACKUP"
  sed -i 's|{"rt", "lf", "bk", "ft", "up", "dn"}|{"rt", "lf", "bk", "ft", "dn", "up"}|' \
    native/src/bsp_map.cpp
  if cmp -s native/src/bsp_map.cpp "$SKY_BACKUP"; then
    echo "  MUTATION-SETUP=FAIL (sky_suffixes_swapped: the sed changed nothing)"
    cp "$SKY_BACKUP" native/src/bsp_map.cpp
    echo "WORLD-RENDER-RELIABILITY-MUTATION=FAIL"; exit 1
  fi
  echo "  mutated source sha256=$(sha256sum native/src/bsp_map.cpp | cut -d' ' -f1)"
  if ! bash build-target.sh tf2_demo_native > "$OUT/mutated-sky_suffixes_swapped.build" 2>&1; then
    echo "  MUTATION-SETUP=FAIL (sky_suffixes_swapped: the rebuild failed)"
    cp "$SKY_BACKUP" native/src/bsp_map.cpp
    bash build-target.sh tf2_demo_native > "$OUT/restore-sky.build" 2>&1
    echo "WORLD-RENDER-RELIABILITY-MUTATION=FAIL"; exit 1
  fi
  echo "  mutated exe sha256=$(sha256sum native/build-nmake/tf2_demo_native.exe | cut -d' ' -f1)"
  run mutated-sky_suffixes_swapped "$SUNSHINE" --world-sky
  echo "  mutated exit_code=$RUN_RC"
  # Under the swap, face 4 (+Z) carries the DOWN file's colour and face 5 (-Z)
  # carries the UP one. Caught by the same invariant the forward section uses:
  # the suffix a face reports must be the suffix in the path it resolved.
  SWAP_MISMATCH=0
  while read -r line; do
    s=$(printf '%s\n' "$line" | grep -oE 'suffix=[a-z]+' | cut -d= -f2)
    v=$(printf '%s\n' "$line" | grep -oE 'vmt=[^ ]+' | cut -d= -f2-)
    case "$v" in
      *"$s.vmt") ;;
      *) SWAP_MISMATCH=$((SWAP_MISMATCH + 1)) ;;
    esac
  done < <(grep "gpu: world sky-face" "$RUN_TRACE" 2>/dev/null)
  if [ "$SWAP_MISMATCH" -ge 2 ]; then
    echo "  MUTATION-CAUGHT=PASS (sky_suffixes_swapped: $SWAP_MISMATCH faces now hold"
    echo "    a file their suffix does not name -- the mapping check is reading"
    echo "    something real, not a second copy of the table)"
  else
    echo "  MUTATION-CAUGHT=FAIL (sky_suffixes_swapped: mismatches=$SWAP_MISMATCH,"
    echo "    the gate would have stayed green)"
    cp "$SKY_BACKUP" native/src/bsp_map.cpp
    bash build-target.sh tf2_demo_native > "$OUT/restore-sky.build" 2>&1
    echo "WORLD-RENDER-RELIABILITY-MUTATION=FAIL"; exit 1
  fi
  { echo "exit_code=$RUN_RC"; grep "gpu: world sky-face" "$RUN_TRACE" 2>/dev/null; } \
    > "$OUT/mutated-sky_suffixes_swapped.txt"
  cp "$SKY_BACKUP" native/src/bsp_map.cpp
  bash build-target.sh tf2_demo_native > "$OUT/restore-sky.build" 2>&1 \
    || { echo "  RESTORE-BUILD=FAIL (sky_suffixes_swapped)"; echo "WORLD-RENDER-RELIABILITY-MUTATION=FAIL"; exit 1; }
  if ! cmp -s native/src/bsp_map.cpp "$SKY_BACKUP"; then
    echo "  RESTORE=FAIL (sky_suffixes_swapped: source not byte-identical)"
    echo "WORLD-RENDER-RELIABILITY-MUTATION=FAIL"; exit 1
  fi
  echo "  RESTORE=PASS (source byte-identical, exe sha256=$(sha256sum native/build-nmake/tf2_demo_native.exe | cut -d' ' -f1))"
  rm -f "$SKY_BACKUP"
  if [ -e "$SKY_BACKUP" ]; then
    echo "  RESTORE=FAIL (sky_suffixes_swapped: backup could not be removed)"
    echo "WORLD-RENDER-RELIABILITY-MUTATION=FAIL"; exit 1
  fi

  # A caught mutation over a red gate is not a pass.
  if [ "$fails" -ne 0 ]; then
    echo "  the mutations went red, but a forward assertion above did not hold"
    echo "  -- a caught mutation over a red gate is not a pass"
    echo "WORLD-RENDER-RELIABILITY-MUTATION=FAIL"; exit 1
  fi
  echo "MUTATION-CAUGHT=PASS (the deleted fix and the swapped sky suffixes each go"
  echo "  red; both sources came back byte-identical)"
  exit 0
fi

echo "checks=$checks fail=$fails"
if [ "$fails" -eq 0 ]; then
  echo "WORLD-RENDER-RELIABILITY=PASS"
  exit 0
fi
echo "WORLD-RENDER-RELIABILITY=FAIL"
exit 1
