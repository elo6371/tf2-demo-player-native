#!/usr/bin/env bash
# third-person-weapon-check.sh -- T83: every class the recording draws holds its
# own weapon in its hand, and only when the flag asks for it.
#
# WHY THIS EXISTS
#   "the frame changed when I turned the flag on" is not evidence. The world pass
#   already draws every weapon entity where the demo left it, so an on/off pair
#   can differ simply because a gun vanished from the floor while no gun appeared
#   in a hand. This gate therefore asks the product to paint each drawn instance
#   its own id (--world-entity-id) and measures masks:
#
#     1. OFF IS THE BASELINE. The attach-off frame uses exactly the flags
#        class-pixels-check.sh used for its animated frame (same demo, tick,
#        camera, settings, speed); its sha256 must equal that frame in
#        evidence/t62-matrix/class-pixels/<class>/animated.bmp. Same for the
#        identity frame, which must equal <class>/identity.bmp. If a default path
#        pixel moved, this fires before any count is quoted.
#     2. ON DIFFERS FROM OFF and ON twice is byte-identical (the replay runs on
#        wall-clock time, so a frame is only a reading when two processes with one
#        request agree).
#     3. THE WEAPON IS AT THE BODY, measured on the identity capture: the ledger
#        line 'entity: ... attach=1 heldEnt=<player>' names the id of the instance
#        the attach path pushed, t83-weapon-pixels.py takes that id's mask and
#        requires it to touch the body mask, sit in the body's upper part and not
#        be a second body -- and requires the model-space hold point to be at hand
#        height, away from the model origin (0,0,0 is the FEET, the exact bug).
#     4. THE TRACE NAMES THE HOLD POINT: one 'weaponAttach: class=... source=...
#        bone=... weapon=...' line per placement and a per-frame
#        'scene: weaponAttach held=... drawn=...' ledger. The off run must contain
#        NEITHER: the feature has to be inert when the flag is absent.
#
# NEGATIVE CONTROLS (a gate that cannot fail proves nothing)
#   N1  the identity capture of the OFF run is fed to the pixel checker: with no
#       attach instance it must exit non-zero.
#   N2  the attach ledger line's id is rewritten to the BODY id (same picture):
#       the weapon mask becomes a body-sized blob and the checker must reject it
#       on its shape thresholds, not only on a missing line.
#   N3  '--player-weapon-attach bogus' must exit 13 with the refusal line, so a
#       typo cannot silently fall back to on|off and turn an A/B into two clones.
#
# PREREQUISITES (checked before the first capture, FATAL exit 2 if absent)
#   PRODUCT   native/build-nmake/tf2_demo_native.exe built from THIS tree with
#             --player-weapon-attach, --world-entity-id, --player-animation and
#             --debug-camera; its sha256 is printed and recorded in every row.
#   TF        the TF2 "tf" directory (game assets referenced by the demo).
#   DEMO      the Snakewater recording the class-pixels baseline was taken from.
#   PY        a Python with numpy (the pixel checker reads BMPs by hand).
#   BASELINE  evidence/t62-matrix/class-pixels -- settings.json, ui.json,
#             targets.txt and <class>/{animated,identity}.bmp.
# The baseline comparison mode is SHA EQUALITY against those files (mode 1).
# It is deliberately NOT a soft comparison: if a baseline file is missing this
# gate exits 2 rather than skipping the assertion, because "off changed nothing"
# is the one claim a reviewer must be able to trust. When the gate runs on a tree
# that does not carry evidence/t62-matrix (for example a fresh worktree), point
# T83_WEAPON_BASELINE at a copy -- do not weaken the check.
#
# MUST RUN UNDER THE SHARED HOST LOCK (it launches captures):
#   bash /d/TF2_Native_Worktrees/_shared/hostlock.sh run t83-weapon \
#     bash -c 'cd /d/TF2_Native_Worktrees/t83-weapon && bash third-person-weapon-check.sh'
#
# Usage: bash third-person-weapon-check.sh [--dry-run]
# Exit:  0 = PASS, 1 = FAIL, 2 = a required input is missing (FATAL).
set -uo pipefail
cd "$(dirname "$0")"

OUT=${T83_WEAPON_OUT:-evidence/t83-weapon/weapon-attach}
PRODUCT=${T83_WEAPON_PRODUCT:-native/build-nmake/tf2_demo_native.exe}
PY=${T83_WEAPON_PYTHON:-C:/Users/Administrator/.dsh/dsh-runtimes/dsh-primary-runtime/dependencies/python/python.exe}
BASELINE=${T83_WEAPON_BASELINE:-evidence/t62-matrix/class-pixels}
TARGETS=${T83_WEAPON_TARGETS:-$BASELINE/targets.txt}
TF=${T83_WEAPON_TF_ROOT:-D:/SteamLibrary/steamapps/common/Team Fortress 2/tf}
DEMO=${T83_WEAPON_DEMO:-D:/TF2_Demo_Player/testdata/demos/bb841c6d379ff7c40d0c8baf99f59d8d_matcha-20260927-1347-cp_snakewater_final1.dem}
SPEED=${T83_WEAPON_SPEED:-8}
CAMERA_OFFSET_X=-250
CAMERA_OFFSET_Z=58
CAMERA_PITCH=4.1
SIDE_DISTANCE=250
SIDE_YAW=45.0

fail=0
say_fail() { echo "  FAIL $*"; fail=1; }
sha_of() {
  local f="$1"
  [ -f "$f" ] || { echo "<none>"; return 0; }
  sha256sum "$f" 2>/dev/null | cut -d' ' -f1
}

for path in "$PRODUCT" "$TF" "$DEMO" "$PY" "$TARGETS" \
            "$BASELINE/settings.json" "$BASELINE/ui.json"; do
  [ -e "$path" ] || { echo "FATAL: missing input: $path"; exit 2; }
done
[ -x "$PRODUCT" ] || { echo "FATAL: not executable: $PRODUCT"; exit 2; }
mkdir -p "$OUT"
PRODUCT_SHA=$(sha_of "$PRODUCT")

# --dry-run: prove the plan and the inputs without launching anything (no
# capture, no host lock needed). Same target file, same camera arithmetic as
# the real run, so a wrong target line or a missing input shows up here.
if [ "${1:-}" = "--dry-run" ]; then
  echo "=== T83 third-person weapon attach: DRY RUN (nothing is launched) ==="
  echo "  product=$PRODUCT"
  echo "  product sha256=$PRODUCT_SHA"
  echo "  baseline=$BASELINE targets=$TARGETS out=$OUT"
  echo "  settings=$BASELINE/settings.json ui=$BASELINE/ui.json"
  echo "  demo=$DEMO"
  echo "  tf=$TF speed=$SPEED"
  echo "  camera behind: eye=origin+($CAMERA_OFFSET_X,0,$CAMERA_OFFSET_Z) pitch=$CAMERA_PITCH yaw=0"
  echo "  camera side:   eye=origin-$SIDE_DISTANCE*(cos,sin) at ${SIDE_YAW} degrees pitch=$CAMERA_PITCH"
  planned=0
  while read -r CLASS ENT TICK ORIGIN_X ORIGIN_Y ORIGIN_Z; do
    case "$CLASS" in ''|'#'*) continue;; esac
    planned=$((planned + 1))
    eyeX=$(awk -v x="$ORIGIN_X" -v d="$CAMERA_OFFSET_X" 'BEGIN { printf "%.1f", x + d }')
    eyeY="$ORIGIN_Y"
    eyeZ=$(awk -v z="$ORIGIN_Z" -v d="$CAMERA_OFFSET_Z" 'BEGIN { printf "%.1f", z + d }')
    sideX=$(awk -v x="$ORIGIN_X" -v d="$SIDE_DISTANCE" -v a="$SIDE_YAW" 'BEGIN { printf "%.1f", x - d * cos(a * 0.01745329252) }')
    sideY=$(awk -v y="$ORIGIN_Y" -v d="$SIDE_DISTANCE" -v a="$SIDE_YAW" 'BEGIN { printf "%.1f", y - d * sin(a * 0.01745329252) }')
    echo "--- $CLASS: ent=$ENT tick=$TICK origin=$ORIGIN_X,$ORIGIN_Y,$ORIGIN_Z"
    echo "    off             eye=$eyeX,$eyeY,$eyeZ pitch=$CAMERA_PITCH yaw=0 flags=--player-animation"
    echo "    attached        eye=$eyeX,$eyeY,$eyeZ pitch=$CAMERA_PITCH yaw=0 flags=--player-animation --player-weapon-attach on"
    echo "    attached-2      eye=$eyeX,$eyeY,$eyeZ pitch=$CAMERA_PITCH yaw=0 flags=--player-animation --player-weapon-attach on"
    echo "    identity        eye=$eyeX,$eyeY,$eyeZ pitch=$CAMERA_PITCH yaw=0 flags=--player-animation --world-entity-id"
    echo "    identity-attached eye=$sideX,$sideY,$eyeZ pitch=$CAMERA_PITCH yaw=$SIDE_YAW flags=--player-animation --world-entity-id --player-weapon-attach on"
  done < "$TARGETS"
  if [ "$planned" -eq 0 ]; then
    echo "FATAL: no target rows in $TARGETS"
    exit 2
  fi
  echo "DRY-RUN cases=$planned frames=$((planned * 5)) inputs=ok"
  exit 0
fi

# run_frame <dir> <tag> <tick> <eyeX> <eyeY> <eyeZ> <pitch> <yaw> <flag...>
# Same request shape as class-pixels-check.sh's helper so the off frame is that
# gate's animated frame down to the settings file.
run_frame() {
  local dir="$1" tag="$2" tick="$3" eyeX="$4" eyeY="$5" eyeZ="$6" pitch="$7" yaw="$8"; shift 8
  local bmp="$dir/$tag.bmp" trace="$dir/$tag.trace"
  rm -f "$bmp" "$trace"
  TF2_NATIVE_TRACE="$trace" timeout 1200 "$PRODUCT" \
    --tf-root "$TF" --hidden-window --audio-disabled \
    --settings-file "$BASELINE/settings.json" --ui-settings-file "$BASELINE/ui.json" \
    --demo "$DEMO" --capture-tick "$tick" --playback-speed "$SPEED" \
    --capture-frame "$bmp" \
    --debug-camera "$eyeX" "$eyeY" "$eyeZ" "$pitch" "$yaw" \
    "$@" > "$dir/$tag.log" 2>&1
  local rc=$?
  local frameTick sceneTick
  frameTick=$(grep -a -o 'capture: armed requested=[0-9]* frame_tick=[0-9]*' "$trace" 2>/dev/null | tail -1 | sed 's/.*frame_tick=//')
  sceneTick=$(grep -a 'scene: anim ' "$trace" 2>/dev/null | tail -1 | grep -o 'tick=[0-9]*' | tail -1 | sed 's/tick=//')
  # The attach ledger and the identity ledger are kept as a small file: the trace
  # itself is hundreds of MB on a long replay, but these lines are the evidence
  # that the hold point came from a named bone and that the weapon instance was
  # given an id of its own.
  grep -a -E 'weaponAttach:|scene: weaponAttach ' "$trace" > "$dir/$tag.weapon.txt" 2>/dev/null
  grep -a 'entity: id=' "$trace" > "$dir/$tag.entity.txt" 2>/dev/null
  {
    echo "tag=$tag rc=$rc frameTick=$frameTick sceneTick=$sceneTick"
    grep -a 'scene: weaponAttach ' "$trace" 2>/dev/null | tail -1
    grep -a 'weaponAttach: ' "$trace" 2>/dev/null
    cat "$dir/$tag.entity.txt" 2>/dev/null
    echo "frame: bmp=$tag.bmp sha256=$(sha_of "$bmp")"
    echo "frame: product sha256=$PRODUCT_SHA"
    echo "frame: request demo=$DEMO tick=$tick speed=$SPEED camera=$eyeX,$eyeY,$eyeZ,$pitch,$yaw flags=$*"
  } > "$dir/$tag.summary"
  rm -f "$trace"
  printf '%s %s %s\n' "$rc" "$frameTick" "$sceneTick"
}

echo "=== T83 third-person weapon attach: per class, off is the baseline ==="
echo "  demo=$DEMO"
echo "  output=$OUT baseline=$BASELINE targets=$TARGETS speed=$SPEED"
echo "  product=$PRODUCT sha256=$PRODUCT_SHA"
echo

cases=0
attach_det_ok=0        # rows whose two attached captures agreed byte for byte
attach_ok=0            # rows that held a weapon AND painted it at the hand
refused_ok=0           # rows where the recording gives the class no weapon at all
baseline_ok=0
pixels_ok=0
while read -r CLASS ENT TICK ORIGIN_X ORIGIN_Y ORIGIN_Z; do
  case "$CLASS" in ''|'#'*) continue;; esac
  dir="$OUT/$CLASS"
  mkdir -p "$dir"
  eyeX=$(awk -v x="$ORIGIN_X" -v d="$CAMERA_OFFSET_X" 'BEGIN { printf "%.1f", x + d }')
  eyeY="$ORIGIN_Y"
  eyeZ=$(awk -v z="$ORIGIN_Z" -v d="$CAMERA_OFFSET_Z" 'BEGIN { printf "%.1f", z + d }')
  sideX=$(awk -v x="$ORIGIN_X" -v d="$SIDE_DISTANCE" -v a="$SIDE_YAW" 'BEGIN { printf "%.1f", x - d * cos(a * 0.01745329252) }')
  sideY=$(awk -v y="$ORIGIN_Y" -v d="$SIDE_DISTANCE" -v a="$SIDE_YAW" 'BEGIN { printf "%.1f", y - d * sin(a * 0.01745329252) }')
  sideZ="$eyeZ"
  cases=$((cases + 1))
  echo "--- $CLASS: ent=$ENT tick=$TICK origin=$ORIGIN_X,$ORIGIN_Y,$ORIGIN_Z"
  echo "    behind eye=$eyeX,$eyeY,$eyeZ yaw=0 / side eye=$sideX,$sideY,$sideZ yaw=$SIDE_YAW"

  read -r off_rc off_ft off_st < <(run_frame "$dir" off "$TICK" "$eyeX" "$eyeY" "$eyeZ" "$CAMERA_PITCH" 0.0 --player-animation)
  read -r on_rc on_ft on_st < <(run_frame "$dir" attached "$TICK" "$eyeX" "$eyeY" "$eyeZ" "$CAMERA_PITCH" 0.0 --player-animation --player-weapon-attach on)
  read -r on2_rc on2_ft on2_st < <(run_frame "$dir" attached-2 "$TICK" "$eyeX" "$eyeY" "$eyeZ" "$CAMERA_PITCH" 0.0 --player-animation --player-weapon-attach on)
  read -r id_rc id_ft id_st < <(run_frame "$dir" identity "$TICK" "$eyeX" "$eyeY" "$eyeZ" "$CAMERA_PITCH" 0.0 --player-animation --world-entity-id)
  read -r ia_rc ia_ft ia_st < <(run_frame "$dir" identity-attached "$TICK" "$sideX" "$sideY" "$sideZ" "$CAMERA_PITCH" "$SIDE_YAW" --player-animation --world-entity-id --player-weapon-attach on)
  echo "    rc/landed: off=$off_rc/$off_ft attached=$on_rc/$on_ft attached-2=$on2_rc/$on2_ft identity=$id_rc/$id_ft identity-attached=$ia_rc/$ia_ft"

  for pair in "off:$off_rc" "attached:$on_rc" "attached-2:$on2_rc" "identity:$id_rc" "identity-attached:$ia_rc"; do
    [ "$(echo "$pair" | cut -d: -f2)" = "0" ] || say_fail "$CLASS: $(echo "$pair" | cut -d: -f1) capture exited $(echo "$pair" | cut -d: -f2)"
  done
  for pair in "off:$off_ft" "attached:$on_ft" "attached-2:$on2_ft" "identity:$id_ft" "identity-attached:$ia_ft"; do
    [ "$(echo "$pair" | cut -d: -f2)" = "$TICK" ] || say_fail "$CLASS: $(echo "$pair" | cut -d: -f1) frame landed tick $(echo "$pair" | cut -d: -f2), not $TICK"
  done
  [ "$off_st" = "$on_st" ] && [ "$off_st" = "$id_st" ] && [ "$off_st" = "$ia_st" ] \
    || say_fail "$CLASS: the passes resolved different scene ticks (off=$off_st on=$on_st id=$id_st idon=$ia_st)"

  OFF_SHA=$(sha_of "$dir/off.bmp")
  ON_SHA=$(sha_of "$dir/attached.bmp")
  ON2_SHA=$(sha_of "$dir/attached-2.bmp")
  ID_SHA=$(sha_of "$dir/identity.bmp")
  IA_SHA=$(sha_of "$dir/identity-attached.bmp")
  BASE_OFF_SHA=$(sha_of "$BASELINE/$CLASS/animated.bmp")
  BASE_ID_SHA=$(sha_of "$BASELINE/$CLASS/identity.bmp")
  echo "    sha off=$OFF_SHA"
  echo "    sha attached=$ON_SHA attached-2=$ON2_SHA"
  echo "    sha identity=$ID_SHA baseline(identity)=$BASE_ID_SHA"
  echo "    sha identity-attached=$IA_SHA"

  # 1. off, and the identity pass with the flag absent, are the baseline.
  if [ "$BASE_OFF_SHA" = "<none>" ]; then
    say_fail "$CLASS: no baseline animated.bmp at $BASELINE/$CLASS/animated.bmp, so 'off is unchanged' cannot be asserted"
  elif [ "$OFF_SHA" = "$BASE_OFF_SHA" ]; then
    echo "  OK   off frame is byte-identical to the class-pixels baseline"
    baseline_ok=$((baseline_ok + 1))
  else
    say_fail "$CLASS: off frame ($OFF_SHA) is NOT the class-pixels baseline ($BASE_OFF_SHA): the default path moved"
  fi
  if [ "$BASE_ID_SHA" = "<none>" ]; then
    say_fail "$CLASS: no baseline identity.bmp, so 'the entity-id path is unchanged' cannot be asserted"
  elif [ "$ID_SHA" = "$BASE_ID_SHA" ]; then
    echo "  OK   identity frame is byte-identical to the class-pixels identity"
  else
    say_fail "$CLASS: identity frame ($ID_SHA) is NOT the class-pixels identity ($BASE_ID_SHA): the entity-id path moved"
  fi

  # 0. what the attach ledger says this frame actually did. Parsed BEFORE the
  # on/off comparison, because "did this frame hold a weapon at all" decides
  # which assertion is the honest one: a recording in which the class carries no
  # active weapon has no weapon to attach, and demanding pixels from it would
  # only reward drawing something that should not be there.
  held=$(grep -a 'scene: weaponAttach ' "$dir/attached.summary" | tail -1 | grep -o 'held=[0-9]*' | tail -1 | sed 's/held=//')
  drawn=$(grep -a 'scene: weaponAttach ' "$dir/attached.summary" | tail -1 | grep -o 'drawn=[0-9]*' | tail -1 | sed 's/drawn=//')
  skipped=$(grep -a 'scene: weaponAttach ' "$dir/attached.summary" | tail -1 | grep -o 'skippedStandalone=[0-9]*' | tail -1 | sed 's/skippedStandalone=//')
  refused=$(grep -a 'scene: weaponAttach ' "$dir/attached.summary" | tail -1 | grep -o 'refused=[0-9]*' | tail -1 | sed 's/refused=//')
  first_refusal=$(grep -a 'scene: weaponAttach ' "$dir/attached.summary" | tail -1 | grep -o 'firstRefusal=[^ ]*' | tail -1 | sed 's/firstRefusal=//')
  echo "  attach ledger: held=${held:-?} drawn=${drawn:-?} refused=${refused:-?} skippedStandalone=${skipped:-?} firstRefusal=${first_refusal:-none}"

  # 2. determinism, asked of every row: one request, two processes, one answer.
  if [ "$ON_SHA" != "<none>" ] && [ "$ON_SHA" = "$ON2_SHA" ]; then
    echo "  OK   attached twice is byte-identical ($ON_SHA)"
    attach_det_ok=$((attach_det_ok + 1))
  else
    say_fail "$CLASS: the same attached capture twice is not byte-identical (on=$ON_SHA on2=$ON2_SHA)"
  fi

  if [ "${held:-0}" -ge 1 ] 2>/dev/null; then
    # POSITIVE ROW: this class had a weapon this frame. The flag must change the
    # picture, the trace must name the bone it hung the weapon on, and the
    # painted instance must be a weapon-sized blob at the hand.
    if [ "$ON_SHA" = "$OFF_SHA" ]; then
      say_fail "$CLASS: attached and off frames are byte-identical: the flag changed nothing"
    else
      echo "  OK   attached differs from off"
    fi
    if grep -qa 'weaponAttach' "$dir/off.summary"; then
      say_fail "$CLASS: the OFF run printed weapon-attach lines: the feature is not inert when the flag is absent"
    else
      echo "  OK   the off run prints no weapon-attach line"
    fi
    W_ATTACH=$(grep -a 'weaponAttach: class=' "$dir/attached.summary" | tail -3)
    if [ -n "$W_ATTACH" ]; then
      echo "  OK   attach placement lines:"
      printf '%s\n' "$W_ATTACH" | sed 's/^/      /'
    else
      say_fail "$CLASS: the attached run printed no weaponAttach: class= line: nothing was placed on a hold point"
    fi
    # Dedupe, measured in the picture rather than in the counter: the weapon
    # entity's id may appear exactly once and that one appearance must be the
    # attached instance. A second line saying ent=<weapon> would be the same gun
    # drawn twice in one frame.
    # The attach line must belong to THIS body: several players in one frame each
    # push their own attached instance, and "the last attach line" is not ours.
    weapon_ent=$(grep -a 'attach=1 heldEnt=' "$dir/identity-attached.summary" | grep -a "heldEnt=${ENT} " | tail -1 | grep -o ' ent=[0-9-]*' | head -1 | sed 's/^ ent=//')
    if [ -n "${weapon_ent:-}" ]; then
      dup=$(grep -a 'entity: id=' "$dir/identity-attached.summary" | grep -v 'attach=1' | grep -c "ent=${weapon_ent} ")
      if [ "${dup:-0}" = "0" ]; then
        echo "  OK   weapon entity ent=$weapon_ent is drawn only as the attached instance (no standalone twin)"
      else
        say_fail "$CLASS: weapon entity ent=$weapon_ent is drawn $dup time(s) standalone as well as attached: the same gun is on screen twice"
      fi
    else
      say_fail "$CLASS: no 'attach=1 heldEnt=' ledger line, so the attached instance cannot be identified"
    fi
    if [ "${held:-0}" -ge 1 ] 2>/dev/null && [ "${drawn:-0}" -ge 1 ] 2>/dev/null; then
      echo "  OK   this frame holds $held weapon(s), drew $drawn, and left ${skipped:-0} standalone draw(s) out"
    else
      say_fail "$CLASS: held=${held:-0} drawn=${drawn:-0}; the frame carries no attached weapon"
    fi

    # The weapon pixels are at the body (identity capture, side view).
    "$PY" t83-weapon-pixels.py "$dir/identity-attached.bmp" "$dir/identity-attached.summary" "$ENT" \
      > "$dir/weapon-pixels.txt" 2>&1
    px_rc=$?
    sed 's/^/    /' "$dir/weapon-pixels.txt"
    echo "  weapon-pixels rc=$px_rc"
    if [ "$px_rc" = "0" ]; then
      pixels_ok=$((pixels_ok + 1))
    else
      say_fail "$CLASS: the attach instance's pixels are not a weapon at this body (see above)"
    fi
    attach_ok=$((attach_ok + 1))
  elif [ "${refused:-0}" -ge 1 ] 2>/dev/null || [ -n "${first_refusal:-}" ]; then
    # REFUSAL ROW: the recording gives this class no active weapon at its tick
    # (an empty m_hActiveWeapon handle, a weapon entity that is not in the
    # snapshot, a world-model index of 0). The correct behaviour is exactly the
    # one the product's fallback promises: nothing drawn, a traced reason, and
    # the default path untouched. Asserting a pixel blob here would be asserting
    # a weapon that TF2 does not have in this frame.
    echo "  OK   refusal row: no weapon to attach (firstRefusal=${first_refusal:-none}); asserting the fallback"
    if [ "$ON_SHA" = "$OFF_SHA" ]; then
      echo "  OK   attached equals off, as a frame with no weapon must"
    else
      say_fail "$CLASS: the frame carries no weapon (held=${held:-0}, refused=${refused:-0}) yet attached differs from off"
    fi
    if grep -qa 'weaponAttach: class=' "$dir/attached.summary"; then
      say_fail "$CLASS: the attached run placed a hold point while reporting no weapon (held=${held:-0})"
    else
      echo "  OK   no placement line for a frame with no weapon"
    fi
    if grep -qa 'scene: weaponAttach ' "$dir/attached.summary"; then
      echo "  OK   the refusal is in the trace, not silent"
    else
      say_fail "$CLASS: no 'scene: weaponAttach' ledger line at all: the refusal is invisible"
    fi
    px_rc="refused"
    refused_ok=$((refused_ok + 1))
  else
    say_fail "$CLASS: neither a held weapon nor a traced refusal: the flag did nothing and said nothing"
    px_rc="none"
  fi

  # N1: the identity capture of the OFF run has no attach instance at all.
  "$PY" t83-weapon-pixels.py "$dir/identity.bmp" "$dir/identity.summary" "$ENT" \
    > "$dir/weapon-pixels-n1.txt" 2>&1
  n1_rc=$?
  echo "  N1 off identity capture into the checker rc=$n1_rc (must be non-zero)"
  if [ "$n1_rc" = "0" ]; then
    say_fail "$CLASS: N1 the pixel checker PASSED on a frame with no attached instance"
  fi

  # N2: point the attach ledger at the BODY id -- the mask becomes a body-sized
  # blob and the shape thresholds must reject it.
  n2_body=$(awk -v ent="$ENT" '
    /entity: id=/ && $0 !~ /attach=1/ {
      isbody = 0
      for (i = 1; i <= NF; i++) if ($i == "ent=" ent) isbody = 1
      if (isbody) for (j = 1; j <= NF; j++) if ($j ~ /^id=[0-9]+$/) body = substr($j, 4)
    }
    END { print body }' "$dir/identity-attached.summary")
  if [ -n "$n2_body" ]; then
    sed -E "s/entity: id=[0-9]+/entity: id=$n2_body/" "$dir/identity-attached.summary" > "$dir/n2-decoy.summary"
    "$PY" t83-weapon-pixels.py "$dir/identity-attached.bmp" "$dir/n2-decoy.summary" "$ENT" \
      > "$dir/weapon-pixels-n2.txt" 2>&1
    n2_rc=$?
    echo "  N2 attach id rewritten to the body id ($n2_body) rc=$n2_rc (must be non-zero)"
    if [ "$n2_rc" = "0" ]; then
      say_fail "$CLASS: N2 the pixel checker PASSED when the attach id was the body id"
    fi
  else
    n2_rc="skipped"
    say_fail "$CLASS: N2 could not find the body id in the identity ledger"
  fi

  {
    echo "frame: class=$CLASS ent=$ENT tick=$TICK"
    echo "frame: bmp off sha256=$OFF_SHA baseline=$BASE_OFF_SHA"
    echo "frame: bmp attached sha256=$ON_SHA attached-2 sha256=$ON2_SHA"
    echo "frame: bmp identity sha256=$ID_SHA baseline=$BASE_ID_SHA"
    echo "frame: bmp identity-attached sha256=$IA_SHA"
    echo "frame: attach held=$held drawn=$drawn skipped=$skipped pixels_rc=$px_rc n1_rc=$n1_rc n2_rc=$n2_rc"
    echo "frame: product sha256=$PRODUCT_SHA"
  } >> "$dir/weapon-pixels.txt"
  echo
done < "$TARGETS"

# N3, once: the flag rejects a value it cannot honour.
#
# The refusal is an exit code plus the absence of an artefact, not a line of
# stdout: every refused argument in native/src/main.cpp announces itself on the
# debugger channel (OutputDebugStringW, see the --outline, --model-*,
# --viewmodel-*, --logstf-* and this flag's own rejection block) and this
# program writes nothing to stdout or stderr at all. An earlier form of this
# control grepped the log for 'invalid --player-weapon-attach' and therefore
# could never pass -- it was asserting a channel the product has never used
# (first full run, 2026-11-05 19:49, log 0 bytes, rc 13). What is observable,
# and what actually protects the A/B pair, is that the refusal happens before
# anything is drawn: exit 13 and no frame written.
echo "--- N3 invalid --player-weapon-attach value"
N3_LOG="$OUT/negative-flag.log"
N3_BMP="$OUT/n3-refused.bmp"
rm -f "$N3_BMP"
"$PRODUCT" --tf-root "$TF" --hidden-window --audio-disabled \
  --settings-file "$BASELINE/settings.json" --ui-settings-file "$BASELINE/ui.json" \
  --demo "$DEMO" --capture-tick 0 --playback-speed "$SPEED" \
  --capture-frame "$N3_BMP" \
  --player-weapon-attach bogus > "$N3_LOG" 2>&1
n3_rc=$?
if [ "$n3_rc" = "13" ]; then
  echo "  OK   N3 --player-weapon-attach bogus exits 13 (the documented rejection code)"
else
  say_fail "N3 --player-weapon-attach bogus exited $n3_rc (want 13): a typo may be silently accepted"
fi
if [ ! -e "$N3_BMP" ]; then
  echo "  OK   the refused run wrote no frame: the typo never reached the render loop"
else
  say_fail "N3 the refused run still wrote $N3_BMP: the bad value was honoured somewhere"
fi

echo "==========================================================================="
echo "cases=$cases attached-deterministic=$attach_det_ok off-is-baseline=$baseline_ok attach-row=$attach_ok refused-row=$refused_ok weapon-pixels=$pixels_ok"
if [ "$fail" = "0" ] && [ "$cases" -gt 0 ]; then
  echo "THIRD-PERSON-WEAPON=PASS"
else
  [ "$cases" -gt 0 ] || echo "  FAIL no target rows were read from $TARGETS"
  echo "THIRD-PERSON-WEAPON=FAIL (see the FAIL lines above)"
fi
[ "$fail" = "0" ] && [ "$cases" -gt 0 ]
