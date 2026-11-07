#!/usr/bin/env bash
# playback-error-paths-check.sh -- T93 gate for the four T85 playback defects (A-D).
#
# Every case launches the product through run(), which REFUSES an argv without
# --hidden-window and --audio-disabled (return 99): a gate that can open the
# audio device on someone else machine is not a gate. run() ALSO adds
# --settings-file / --ui-settings-file pointing into a mktemp -d scratch dir,
# because the product writes both unconditionally on exit (and the demo MRU with
# them, D-22): without this a gate run rewrites the real
# %LOCALAPPDATA%/TF2 Demo Player/ ini files and verify-release.sh's machine guard
# turns the whole chain red. The scratch dir is removed by an EXIT trap.
#
#   bash playback-error-paths-check.sh [--only A1,A2,B,C,D,E] [--tag green] [--out DIR]
#
# TF2_EXE overrides the binary (the pre-fix red run points it at the frozen t79
# build); TF2_ROOT overrides the Team Fortress 2/tf root.
set -u

REPO=$(cd "$(dirname "$0")" && pwd)
EXE=$TF2_EXE
if [ -z "$EXE" ]; then EXE=$REPO/native/build-nmake/tf2_demo_native.exe; fi
TF=$TF2_ROOT
if [ -z "$TF" ]; then TF="D:/SteamLibrary/steamapps/common/Team Fortress 2/tf"; fi
AUDIO_DEMO="D:/SteamLibrary/steamapps/common/Team Fortress 2/tf/demos/autorecord_2026-07-02_13-26-46.dem"
CAM_DEMO="D:/TF2_Demo_Player/testdata/demos/dfbce608c80a4d72a390c70c597f0377_match-20261101-1959-koth_bagel_rc11.dem"
OUT=$REPO/evidence/t93-playback
TAG=green
ONLY=

while [ $# -gt 0 ]; do
  case $1 in
    --only) ONLY=$2; shift 2 ;;
    --tag) TAG=$2; shift 2 ;;
    --out) OUT=$2; shift 2 ;;
    *) echo "usage: $0 [--only A1,A2,B,C,D,E] [--tag NAME] [--out DIR]"; exit 64 ;;
  esac
done
mkdir -p "$OUT" || exit 1
OUTW=$(cygpath -m "$OUT")
# The two ini files the product writes on exit, in a throwaway directory. Nothing
# this gate starts may touch the real per-user settings (D-22).
ISO_DIR=$(mktemp -d) || exit 1
trap 'rm -rf "$ISO_DIR"' EXIT
ISOW=$(cygpath -m "$ISO_DIR")

PASS=0; RED=0; CASES=0; START=$SECONDS
green() { PASS=$((PASS+1)); }
red() { RED=$((RED+1)); echo "RED   $1  -- $2"; }
want() { [ -z "$ONLY" ] || echo ",$ONLY," | grep -q ",$1,"; }
has() { grep -qF -- "$2" "$1"; }
nhas() { ! grep -qF -- "$2" "$1"; }

# fnum <trace> <line-substring> <field=...>  -> value of the LAST match, or -
fnum() {
  awk -v pat="$2" -v key="$3" 'index($0,pat){ for(i=1;i<=NF;i++){ if(index($i,key)==1){ split($i,KV,"="); v=KV[2]; f=1 } } } END{ if(f) print v; else print "-" }' "$1"
}
maxseen() {
  awk 'index($0,"audio: advance"){ for(i=1;i<=NF;i++){ if(index($i,"seen=")==1){ split($i,KV,"="); n=KV[2]+0; if(n>m) m=n } } } END{ print m+0 }' "$1"
}
sha() { sha256sum "$1" 2>/dev/null | cut -d" " -f1; }

# run <name> -- launches the argv in "$@" with the trace, rc and stderr kept.
run() {
  NAME=$1; shift
  O=$OUT/$TAG-$NAME
  G1=0; G2=0
  for a in "$@"; do
    if [ "$a" = "--hidden-window" ]; then G1=1; fi
    if [ "$a" = "--audio-disabled" ]; then G2=1; fi
  done
  if [ "$G1" = 0 ] || [ "$G2" = 0 ]; then
    echo "REFUSED $NAME: argv lacks --hidden-window / --audio-disabled"
    return 99
  fi
  NAME_START=$SECONDS
  # The two flags go on EVERY launch, from one place, so a case cannot forget
  # them and quietly write the real settings files.
  SETTINGS_ARGS=(--settings-file "$ISOW/$NAME.settings.ini"
    --ui-settings-file "$ISOW/$NAME.ui.ini")
  TF2_NATIVE_TRACE=$O.trace timeout -k 5 300 "$@" "${SETTINGS_ARGS[@]}" >$O.out 2>$O.err
  RC=$?
  TOOK=$((SECONDS-NAME_START))
  echo $RC >$O.rc
  echo "case  $TAG/$NAME rc=$RC ${TOOK}s" >>$OUT/results.tsv
  return 0
}

echo "gate  exe=$EXE"
echo "gate  tf=$TF"
echo "gate  out=$OUT tag=$TAG only=${ONLY:-all}"

# ---------------------------------------------------------------- A: missing demo
if want A1; then
  CASES=$((CASES+1))
  run A1-missing-demo "$EXE" --tf-root "$TF" --demo "$OUTW/no-such.dem" --hidden-window --audio-disabled
  T=$OUT/$TAG-A1-missing-demo.trace
  R=$(cat $OUT/$TAG-A1-missing-demo.rc)
  if [ "$R" = 2 ]; then green; else red A1-exit "rc=$R want 2"; fi
  if has $OUT/$TAG-A1-missing-demo.err "demo open failed"; then green; else red A1-stderr "stderr has no demo open failed"; fi
  if has $T "demo: open failed"; then green; else red A1-trace "trace has no demo: open failed"; fi
  if nhas $T "mode: window_created=1"; then green; else red A1-window "a window was created for a demo that does not exist"; fi
  if nhas $T "map: lookup"; then green; else red A1-map "map resolution ran for a demo that does not exist"; fi
  if [ "$TOOK" -le 15 ]; then green; else red A1-time "refusal took ${TOOK}s (want <=15, i.e. before resource startup)"; fi
fi

# ------------------------------------------------- A2: no --demo still starts
if want A2; then
  CASES=$((CASES+1))
  { echo "frames 3"; echo "quit"; } >"$OUT/$TAG-a2.script"
  run A2-default-map "$EXE" --tf-root "$TF" --hidden-window --audio-disabled --camera-script "$OUTW/$TAG-a2.script"
  T=$OUT/$TAG-A2-default-map.trace
  R=$(cat $OUT/$TAG-A2-default-map.rc)
  if [ "$R" = 0 ]; then green; else red A2-exit "rc=$R want 0"; fi
  if has $T "mode: window_created=1"; then green; else red A2-window "no window without --demo"; fi
  if has $T "map: lookup"; then green; else red A2-map "no map lookup without --demo"; fi
fi

# --------------------------------------------------- B: camera mode while paused
if want B; then
  CASES=$((CASES+1))
  {
    echo "resume"
    echo "tick 400"
    echo "pause"
    echo "frames 4"
    echo "snapshot before"
    echo "capture $OUTW/$TAG-b-before.bmp"
    echo "frames 4"
    echo "freefly on"
    echo "frames 4"
    echo "capture $OUTW/$TAG-b-after.bmp"
    echo "frames 4"
    echo "snapshot after"
    echo "expect-same-tick"
    echo "playback-state"
    echo "quit"
  } >"$OUT/$TAG-b.script"
  run B-freefly-paused "$EXE" --tf-root "$TF" --demo "$CAM_DEMO" --hidden-window --audio-disabled --camera-script "$OUTW/$TAG-b.script"
  T=$OUT/$TAG-B-freefly-paused.trace
  R=$(cat $OUT/$TAG-B-freefly-paused.rc)
  if [ "$R" = 0 ]; then green; else red B-exit "rc=$R want 0"; fi
  if has $T "cmd=freefly result=ok detail=on mode=free-look"; then green; else red B-step "the freefly step does not report the mode it left the product in (want detail=on mode=free-look)"; fi
  if has $T "camera: mode applied=free-look"; then green; else red B-applied "no frame-level reading of the free-look mode change"; fi
  if has $T "camera: mode applied=free-look tick=400 paused=1"; then green; else red B-paused "the mode change was not applied on a frame drawn while paused"; fi
  MA=$(fnum $T "camera: summary" "modeApplies=")
  if [ "$MA" != "-" ] && [ "$MA" != "0" ]; then green; else red B-summary "camera: summary modeApplies=$MA (want >=1)"; fi
  B1=$(sha $OUT/$TAG-b-before.bmp); B2=$(sha $OUT/$TAG-b-after.bmp)
  if [ -n "$B1" ] && [ -n "$B2" ] && [ "$B1" != "$B2" ]; then green; else red B-pixels "frames before/after the mode change are identical or missing ($B1 / $B2)"; fi
  FB=$(fnum $T "camera: accept steps=" "failures=")
  if [ "$FB" = 0 ]; then green; else red B-script "camera: accept failures=$FB (want 0)"; fi
  TB=$(fnum $T "camera: snapshot label=before" "tick="); TA=$(fnum $T "camera: snapshot label=after" "tick=")
  if [ "$TB" = 400 ] && [ "$TA" = 400 ]; then green; else red B-clock "snapshots at tick=$TB/$TA (want 400/400: the mode change must not need a tick to arrive)"; fi
fi

# ---------------------------------------------------------- C: forward seek audio
if want C; then
  CASES=$((CASES+1))
  {
    echo "resume"
    echo "tick 5"
    echo "seek 3000"
    echo "frames 25"
    echo "playback-state"
    echo "quit"
  } >"$OUT/$TAG-c.script"
  run C-forward-seek "$EXE" --tf-root "$TF" --demo "$AUDIO_DEMO" --hidden-window --audio-disabled --camera-script "$OUTW/$TAG-c.script"
  T=$OUT/$TAG-C-forward-seek.trace
  R=$(cat $OUT/$TAG-C-forward-seek.rc)
  if [ "$R" = 0 ]; then green; else red C-exit "rc=$R want 0"; fi
  if has $T "audio: seek reset from="; then green; else red C-trace "no audio: seek reset line for a forward seek"; fi
  if has $T "to=3000"; then green; else red C-target "the seek reset does not name tick 3000"; fi
  MS=$(maxseen $T)
  if [ "$MS" -le 60 ]; then green; else red C-burst "one audio: advance saw=$MS events (want <=60: the skipped span must not be dumped into one frame)"; fi
  if has $T "audio: advance tick=3000"; then green; else red C-landing "no audio: advance at the seek target"; fi
fi

# ------------------------------------------- D: --start-paused resume keeps tick 0
if want D; then
  CASES=$((CASES+1))
  { echo "resume"; echo "frames 30"; echo "pause"; echo "frames 4"; echo "quit"; } >"$OUT/$TAG-d.script"
  run D-start-paused "$EXE" --tf-root "$TF" --demo "$AUDIO_DEMO" --start-paused --hidden-window --audio-disabled --camera-script "$OUTW/$TAG-d.script"
  T=$OUT/$TAG-D-start-paused.trace
  R=$(cat $OUT/$TAG-D-start-paused.rc)
  if [ "$R" = 0 ]; then green; else red D-exit "rc=$R want 0"; fi
  S0=$(fnum $T "audio: advance tick=0 " "seen=")
  if [ "$S0" != "-" ] && [ "$S0" -ge 700 ]; then green; else red D-tick0 "the resumed tick 0 saw=$S0 events (want >=700: the 746 events at tick 0 are dropped)"; fi
  DUP=$(fnum $T "audio: advance tick=0 " "dup=")
  if [ "$DUP" = 0 ]; then green; else red D-dup "the resumed tick 0 was a duplicate (dup=$DUP): a resume must not consume the tick it re-enters at"; fi
fi

# -------------------------------------------------------------- E: unchanged runs
if want E; then
  CASES=$((CASES+1))
  run E-baseline "$EXE" --tf-root "$TF" --demo "$AUDIO_DEMO" --hidden-window --audio-disabled --capture-tick 600 --capture-frame "$OUTW/$TAG-e.bmp"
  T=$OUT/$TAG-E-baseline.trace
  R=$(cat $OUT/$TAG-E-baseline.rc)
  if [ "$R" = 0 ]; then green; else red E-exit "rc=$R want 0"; fi
  SC=$(fnum $T "audio: timeline" "scheduled=")
  if [ "$SC" = 5323 ]; then green; else red E-timeline "audio: timeline scheduled=$SC (want 5323)"; fi
  ES=$(fnum $T "mode: summary" "events_seen=")
  if [ "$ES" = 759 ]; then green; else red E-events "mode: summary events_seen=$ES (want 759)"; fi
  DO=$(fnum $T "mode: summary" "audio_device_opened=")
  if [ "$DO" = 0 ]; then green; else red E-device "audio_device_opened=$DO (want 0: --audio-disabled must never open the device)"; fi
  if nhas $T "audio: seek reset"; then green; else red E-noseek "an unseeked run printed audio: seek reset"; fi
  MA=$(fnum $T "camera: summary" "modeApplies=")
  if [ "$MA" = 0 ]; then green; else red E-modeapplies "camera: summary modeApplies=$MA (want 0 on a run that never changes mode)"; fi
  if [ -s "$OUT/$TAG-e.bmp" ]; then green; else red E-capture "no capture frame written"; fi
fi

ENDED=$((SECONDS-START))
echo "GATE tag=$TAG cases=$CASES pass=$PASS red=$RED seconds=$ENDED"
if [ "$RED" = 0 ]; then
  echo "GATE-RESULT=PASS"
  echo "PLAYBACK-ERROR-PATHS=PASS"
  exit 0
fi
echo "GATE-RESULT=FAIL"
echo "PLAYBACK-ERROR-PATHS=FAIL"
exit 1
