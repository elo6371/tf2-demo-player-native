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
# D-97 (2026-11-11) -- the F4 pins restated, not deleted
#   Both readings PARTS ONE pinned describe the pre-7954527 build. The world atlas
#   is 4112x4112x4 = 67,634,176 B and an ordinary Texture2D is held to 64 MiB, so
#   the atlas upload refused it and recorded E_FAIL; 7954527 gave the atlas its own
#   budget (max(64 MiB, its own bytes)) and the same atlas now binds -- measured:
#   `gpu: world atlas size=4112 bytes=67634176 atlases=3 ... state=bound`. Keeping
#   "E_FAIL must be recorded" would demand the defect back. PART ONE now asserts the
#   property the T8 fix was for -- a load-time error is REPORTED, never swallowed --
#   the reading that shows the old cause is closed, and that every create site in
#   the world uploader still records its failure. The mutation that deletes the
#   clear is replaced by one that restores the old atlas ceiling.
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

# T86/D-22: the product writes settings.ini and ui.ini on every normal exit.
# Both flags below name per-run copies outside the tree: a gate must not edit
# tracked evidence or the machine profile. gate-launch-hygiene-check.sh asserts
# the static shape; this block is the runtime side of the same contract.
ISO_DIR="$(mktemp -d)"
ISO_DIR_WIN="$(cygpath -m "$ISO_DIR")"
ISO_SETTINGS="$ISO_DIR_WIN/settings.ini"
ISO_UI="$ISO_DIR_WIN/ui.ini"
trap 'rm -rf "${ISO_DIR:-}"' EXIT
HOST_PROFILE_DIR="${LOCALAPPDATA:-${HOME}/AppData/Local}/TF2 Demo Player"
if [ -f "$HOST_PROFILE_DIR/settings.ini" ]; then cp "$HOST_PROFILE_DIR/settings.ini" "$ISO_DIR/settings.ini"; else : > "$ISO_DIR/settings.ini"; echo "  ISO SEED absent: host profile settings.ini (product starts from defaults)" >&2; fi
if [ -f "$HOST_PROFILE_DIR/ui.ini" ]; then cp "$HOST_PROFILE_DIR/ui.ini" "$ISO_DIR/ui.ini"; else : > "$ISO_DIR/ui.ini"; fi

RUN_TAG="$(date +%Y%m%d%H%M%S)-$$"
MODE="forward"
if [ "${1:-}" = "--mutation" ]; then MODE="mutation"; fi
OUT="${T8_OUT:-evidence/t8-render-reliability-${RUN_TAG}-${MODE}}"
mkdir -p "$OUT"
# D-96: OUT may be absolute -- T8_OUT names a window directory. Prefixing $PWD to
# an absolute path produces <repo>//d/..., the product cannot open it as a trace
# destination, and the run still exits 0 with its bmp and csv written. Every
# reading taken from that trace then comes back empty and the gate reads as
# "drawn=0" instead of "no trace". Convert once, at the root, and prefix only
# genuinely relative paths. camera-ui-check.sh:90-96 is the same helper, for the
# same reason; world-material-check.sh is unaffected because its scratch paths
# are relative by default.
absolute_out() { # absolute_out <shell path> -- native Windows path
  case "$1" in
    /*|[A-Za-z]:*) cygpath -m "$1" ;;
    *) cygpath -m "$PWD/$1" ;;
  esac
}

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
  TF2_NATIVE_TRACE="$(absolute_out "$RUN_TRACE")" timeout 200 "$PRODUCT" --settings-file "$ISO_SETTINGS" --ui-settings-file "$ISO_UI" \
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
# ---- D-97 (2026-11-11): the two readings below used to pin the CAUSE ---------
# This gate pinned two readings of the T4/F4 defect: that the load records a stale
# E_FAIL, and that the stage which sets it is `water`. Both were true of the
# pre-T8 build and neither is true now, for a reason the product itself names. The
# world atlas is 4112x4112x4 = 67,634,176 B; an ordinary Texture2D is held to
# 64 MiB (native/include/texture2d.h:14-21), so the atlas upload refused it and
# uploadWorldGeometry recorded E_FAIL (native/src/native_renderer_world.cpp:480-486).
# 7954527 (2026-10-18) gave the atlas its own budget -- max(64 MiB, its own bytes) --
# and the same atlas now binds. Pinning "E_FAIL must be recorded" would demand the
# defect back, so the pins are replaced by the property the T8 fix was for: a
# load-time error is REPORTED, never swallowed.
STAGE_SEQ=$(grep -o "gpu: reload-stage stage=[a-z]*" "$SUN_TRACE" 2>/dev/null | sed "s/.*stage=//" | tr "\n" " " | sed "s/ *$//")
chk "load stage sequence (texture..models)" "$STAGE_SEQ" "texture aux lightmap cubemap sky water models"
STAGE_READINGS=$(grep -c "gpu: reload-stage stage=[a-z]* lastError=0x[0-9A-Fa-f]\{8\}" "$SUN_TRACE" 2>/dev/null)
chk "every load stage printed a 32-bit HRESULT" "${STAGE_READINGS:-0}" "7"
POST_RELOAD=$(grep -o "gpu: post-reload lastError=0x[0-9A-Fa-f]*" "$SUN_TRACE" 2>/dev/null | tail -1 | sed "s/.*lastError=//")
chk "post-reload reading present" "$([ -n "$POST_RELOAD" ] && echo yes || echo no)" "yes"
# The product prints the post-reload value and clears the channel immediately
# after it (native/src/main.cpp:8021-8023). So nothing may legitimately disappear
# between the last stage line and that print. Both directions are asserted:
#   no stage recorded anything    -> post-reload must be 0x00000000 (clean channel)
#   a stage did record something  -> post-reload must still carry a failure
# The second is the "not hidden" direction: a build that clears earlier, or stops
# printing the value, goes red here instead of passing on an empty channel.
STAGE_ERRORS=$(grep -o "reload-stage stage=[a-z]* lastError=0x[0-9A-Fa-f]*" "$SUN_TRACE" 2>/dev/null | grep -v "lastError=0x00000000$" | wc -l | tr -d " ")
LAST_NONZERO=$(grep -o "reload-stage stage=[a-z]* lastError=0x[0-9A-Fa-f]*" "$SUN_TRACE" 2>/dev/null | grep -v "lastError=0x00000000$" | tail -1 | sed "s/.*lastError=//")
echo "  note stage readings that recorded an error = ${STAGE_ERRORS:-0} (0 = the load recorded none)"
if [ "${STAGE_ERRORS:-0}" -gt 0 ]; then
  # The channel is sticky and is only cleared after this print, so the value printed
  # at post-reload must still BE the last error a stage recorded. Not merely "some
  # error": the same one. Anything else means a value was cleared, overwritten or
  # dropped between the last stage and the print -- the swallow this gate forbids.
  chk "post-reload equals the last non-zero stage (nothing swallowed)" \
    "${POST_RELOAD:-<missing>}" "${LAST_NONZERO:-<missing>}"
else
  chk "no stage recorded an error, so the post-reload channel is clean" \
    "${POST_RELOAD:-<missing>}" "0x00000000"
fi
# ---- the old cause is closed, read from the run itself ----------------------
# The atlas the pre-7954527 product refused is now built, and the reading says so:
# its byte count is over the 64 MiB ceiling an ordinary texture is held to and its
# state is `bound`. If it is ever refused again this same line names the refusal
# instead of hiding it, and the two checks below go red before the frame does.
ATLAS_LINE=$(grep "gpu: world atlas size=" "$SUN_TRACE" 2>/dev/null | tail -1)
chk "world atlas state" "$(printf "%s" "$ATLAS_LINE" | grep -o "state=.*" | cut -d= -f2-)" "bound"
ATLAS_BYTES=$(printf "%s" "$ATLAS_LINE" | grep -o "bytes=[0-9]*" | head -1 | cut -d= -f2)
chk "the atlas is over the 64 MiB ordinary-texture ceiling" \
  "$([ "${ATLAS_BYTES:-0}" -gt "$((64 * 1024 * 1024))" ] && echo yes || echo no)" "yes"
echo "  note world atlas bytes = ${ATLAS_BYTES:-0} (the pre-7954527 product refused this atlas)"
PLAN_LINE=$(grep "gpu: world atlas-plan" "$SUN_TRACE" 2>/dev/null | tail -1)
# The plan line states budget twice: once as the field, once inside its reason text
# ("... refused=0 (malformed=0, duplicate=0, budget=0)"), so the field is the first hit.
chk "atlas plan budget refusals" "$(printf "%s" "$PLAN_LINE" | grep -o "budget=[0-9]*" | head -1 | cut -d= -f2)" "0"
# ---- the recording side, read from the source ------------------------------
# A reading is only worth pinning if the code that would fail still records it.
# Every create site in the world uploader must set `lastError_` or name a
# world*Reason_ within the next eight lines. A site that does neither is a failure
# this product could hit and never report -- the shape this gate exists to forbid.
UNRECORDED=$(awk '
  { buf[NR] = $0 }
  END {
    bad = 0
    for (n = 1; n <= NR; n++) {
      if (buf[n] !~ /if \(![A-Za-z_]+\.create\(/ && buf[n] !~ /if \(FAILED\(device_->Create/) continue
      ok = 0
      for (i = n; i <= n + 8; i++) {
        if (buf[i] ~ /lastError_ = / || buf[i] ~ /world[A-Za-z]*Reason_ = /) { ok = 1; break }
      }
      if (!ok) { bad++; printf "  unrecorded create site at line %d: %s\n", n, buf[n] > "/dev/stderr" }
    }
    print bad
  }' native/src/native_renderer_world.cpp)
chk "every world-upload create site records its failure" "${UNRECORDED:-x}" "0"
echo "  note native/src/native_renderer_world.cpp: create sites whose failure goes unrecorded = ${UNRECORDED:-x} (11 sites scanned 2026-11-11)"

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
  echo "=== mutations: the old atlas ceiling restored, the sky table swapped ==="
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

  # ---- Product mutation 1: both halves of the T8 fix taken back ------------
  # D-98 rebased this a second time. D-97 put the 64 MiB atlas ceiling back and
  # expected the old ending (exit 3). Measured 2026-11-11 on this tree: the
  # ceiling alone no longer ends the program -- `clearLastError()`
  # (native/src/main.cpp:8023) still runs after the load, so the sticky E_FAIL is
  # dropped, the world falls back to one material texture and the run exits 0
  # (measured trace: `reload-stage stage=water lastError=0x80004005`,
  # `post-reload lastError=0x80004005`, `gpu: world atlas size=0 ... state=the
  # atlas overload was not used; the world was drawn with one material texture`,
  # `mode: summary ... drawn=1 ... exit=0`). The historical defect needs BOTH
  # halves back: the cause (the ceiling) and the arm that used to catch it (no
  # clear). So this mutation reverts the two halves of the T8 fix together and
  # asks for the original ending: exit 3, the water stage recording E_FAIL, and
  # the policy line naming that same value. The atlas line is read too: it must
  # stop saying `bound`, otherwise the cause was not actually restored.
  MUT_BACKUP="$OUT/native_renderer_world.cpp.atlas_ceiling.backup"
  MUT_CLEAR_BACKUP="$OUT/main.cpp.clear_last_error.backup"
  cp native/src/native_renderer_world.cpp "$MUT_BACKUP"
  cp native/src/main.cpp "$MUT_CLEAR_BACKUP"
  sed -i 's|const std::uint64_t atlasBudget = std::max<std::uint64_t>(64ull \* 1024ull \* 1024ull,|const std::uint64_t atlasBudget = (64ull * 1024ull * 1024ull) + 0 * std::max<std::uint64_t>(0ull,|' \
    native/src/native_renderer_world.cpp
  sed -i 's|^\( *\)renderer\.clearLastError();|\1// MUTATION (D-98): the clear taken back|' native/src/main.cpp
  if cmp -s native/src/native_renderer_world.cpp "$MUT_BACKUP" \
     || cmp -s native/src/main.cpp "$MUT_CLEAR_BACKUP"; then
    echo "  MUTATION-SETUP=FAIL (fix_taken_back: a sed changed nothing)"
    cp "$MUT_BACKUP" native/src/native_renderer_world.cpp
    cp "$MUT_CLEAR_BACKUP" native/src/main.cpp
    echo "WORLD-RENDER-RELIABILITY-MUTATION=FAIL"; exit 1
  fi
  echo "  mutated sources sha256=$(sha256sum native/src/native_renderer_world.cpp native/src/main.cpp | cut -d' ' -f1 | tr '\n' ' ')"
  if ! bash build-target.sh tf2_demo_native > "$OUT/mutated-fix_taken_back.build" 2>&1; then
    echo "  MUTATION-SETUP=FAIL (fix_taken_back: the rebuild failed)"
    cp "$MUT_BACKUP" native/src/native_renderer_world.cpp
    cp "$MUT_CLEAR_BACKUP" native/src/main.cpp
    bash build-target.sh tf2_demo_native > "$OUT/restore-fix_taken_back.build" 2>&1
    echo "WORLD-RENDER-RELIABILITY-MUTATION=FAIL"; exit 1
  fi
  echo "  mutated exe sha256=$(sha256sum native/build-nmake/tf2_demo_native.exe | cut -d' ' -f1)"
  run mutated-fix_taken_back "$SUNSHINE"
  MUT_RC=$RUN_RC
  echo "  mutated exit_code=$MUT_RC"
  MUT_STAGE=$(grep -c "gpu: reload-stage stage=water lastError=0x80004005" "$RUN_TRACE" 2>/dev/null)
  MUT_POLICY=$(grep -c "gpu: device-error-policy hresult=0x80004005" "$RUN_TRACE" 2>/dev/null)
  MUT_ATLAS_STATE=$(grep "gpu: world atlas size=" "$RUN_TRACE" 2>/dev/null | tail -1 | grep -o "state=.*" | cut -d= -f2-)
  echo "  mutated atlas state=${MUT_ATLAS_STATE:-<none>} (the restored cause must show here too)"
  if [ "$MUT_RC" = "3" ] && [ "${MUT_STAGE:-0}" -ge 1 ] && [ "${MUT_POLICY:-0}" -ge 1 ] \
     && [ "${MUT_ATLAS_STATE:-bound}" != "bound" ]; then
    echo "  MUTATION-CAUGHT=PASS (fix_taken_back: exit 3 again, the water stage line"
    echo "    records 0x80004005, the policy line names it, and the atlas line no"
    echo "    longer says bound -- the forward block reads the cause it claims to)"
  else
    echo "  MUTATION-CAUGHT=FAIL (fix_taken_back: rc=$MUT_RC, water-stage E_FAIL lines="
    echo "    ${MUT_STAGE:-0}, policy lines=${MUT_POLICY:-0}, atlas state=${MUT_ATLAS_STATE:-<none>};"
    echo "    the gate would have stayed green)"
    cp "$MUT_BACKUP" native/src/native_renderer_world.cpp
    cp "$MUT_CLEAR_BACKUP" native/src/main.cpp
    bash build-target.sh tf2_demo_native > "$OUT/restore-fix_taken_back.build" 2>&1
    echo "WORLD-RENDER-RELIABILITY-MUTATION=FAIL"; exit 1
  fi
  { echo "exit_code=$MUT_RC"
    grep -E "gpu: reload-stage|gpu: post-reload|gpu: device-error-policy|gpu: world atlas |mode: summary" "$RUN_TRACE" 2>/dev/null
  } > "$OUT/mutated-fix_taken_back.txt"
  cp "$MUT_BACKUP" native/src/native_renderer_world.cpp
  cp "$MUT_CLEAR_BACKUP" native/src/main.cpp
  bash build-target.sh tf2_demo_native > "$OUT/restore-fix_taken_back.build" 2>&1 \
    || { echo "  RESTORE-BUILD=FAIL (fix_taken_back)"; echo "WORLD-RENDER-RELIABILITY-MUTATION=FAIL"; exit 1; }
  if ! cmp -s native/src/native_renderer_world.cpp "$MUT_BACKUP" \
     || ! cmp -s native/src/main.cpp "$MUT_CLEAR_BACKUP"; then
    echo "  RESTORE=FAIL (fix_taken_back: a source did not come back byte-identical)"
    echo "WORLD-RENDER-RELIABILITY-MUTATION=FAIL"; exit 1
  fi
  echo "  RESTORE=PASS (both sources byte-identical, exe sha256=$(sha256sum native/build-nmake/tf2_demo_native.exe | cut -d' ' -f1))"
  rm -f "$MUT_BACKUP" "$MUT_CLEAR_BACKUP"
  if [ -e "$MUT_BACKUP" ] || [ -e "$MUT_CLEAR_BACKUP" ]; then
    echo "  RESTORE=FAIL (fix_taken_back: a backup could not be removed)"
    echo "WORLD-RENDER-RELIABILITY-MUTATION=FAIL"; exit 1
  fi

  # ---- Source mutation 3: one create site stops recording its failure ------
  # The forward block's static guard is new with D-97, so it needs teeth on
  # this tree and not only in the offline fixtures. Take one
  # `lastError_ = E_FAIL;` away from a world-uploader create site and run the
  # SAME awk guard over the mutated source: it must count one unrecorded site
  # and name its line. No rebuild -- the guard reads the source, and a rebuild
  # would only prove the compiler still works.
  GUARD_BACKUP="$OUT/native_renderer_world.cpp.guard.backup"
  cp native/src/native_renderer_world.cpp "$GUARD_BACKUP"
  awk '{ if (!done && $0 ~ /^[[:space:]]*lastError_ = E_FAIL;$/) { print "// MUTATION (D-98): this site records nothing"; done = 1; next } print }' \
    "$GUARD_BACKUP" > "$OUT/mutated_guard.cpp"
  cp "$OUT/mutated_guard.cpp" native/src/native_renderer_world.cpp
  if cmp -s native/src/native_renderer_world.cpp "$GUARD_BACKUP"; then
    echo "  MUTATION-SETUP=FAIL (guard_bites: no lastError_ line matched)"
    cp "$GUARD_BACKUP" native/src/native_renderer_world.cpp
    echo "WORLD-RENDER-RELIABILITY-MUTATION=FAIL"; exit 1
  fi
  GUARD_MUT=$(awk '
    { buf[NR] = $0 }
    END {
      bad = 0
      for (n = 1; n <= NR; n++) {
        if (buf[n] !~ /if \(![A-Za-z_]+\.create\(/ && buf[n] !~ /if \(FAILED\(device_->Create/) continue
        ok = 0
        for (i = n; i <= n + 8; i++) {
          if (buf[i] ~ /lastError_ = / || buf[i] ~ /world[A-Za-z]*Reason_ = /) { ok = 1; break }
        }
        if (!ok) { bad++; printf "  unrecorded create site at line %d: %s\n", n, buf[n] > "/dev/stderr" }
      }
      print bad
    }' native/src/native_renderer_world.cpp)
  if [ "${GUARD_MUT:-0}" -ge 1 ]; then
    echo "  MUTATION-CAUGHT=PASS (guard_bites: the guard counts $GUARD_MUT unrecorded"
    echo "    site(s) and names the line, so forward check 8 is not a constant)"
  else
    echo "  MUTATION-CAUGHT=FAIL (guard_bites: the guard still reported 0 with a"
    echo "    recording line deleted)"
    cp "$GUARD_BACKUP" native/src/native_renderer_world.cpp
    echo "WORLD-RENDER-RELIABILITY-MUTATION=FAIL"; exit 1
  fi
  cp "$GUARD_BACKUP" native/src/native_renderer_world.cpp
  if ! cmp -s native/src/native_renderer_world.cpp "$GUARD_BACKUP"; then
    echo "  RESTORE=FAIL (guard_bites: source not byte-identical)"
    echo "WORLD-RENDER-RELIABILITY-MUTATION=FAIL"; exit 1
  fi
  rm -f "$GUARD_BACKUP"
  echo "  RESTORE=PASS (guard_bites: source byte-identical, no rebuild was needed)"
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
  echo "MUTATION-CAUGHT=PASS (both halves of the T8 fix, one create site's"
  echo "  recording and the swapped sky suffixes each go red; every source"
  echo "  came back byte-identical)"
fi

echo "checks=$checks fail=$fails"
if [ "$fails" -eq 0 ]; then
  echo "WORLD-RENDER-RELIABILITY=PASS"
  exit 0
fi
echo "WORLD-RENDER-RELIABILITY=FAIL"
exit 1
