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
#   bash playback-error-paths-check.sh --selftest      # no product, no lock; ~90 s on this host
#   bash playback-error-paths-check.sh --mutation-judge M4 OUT/tag-gate.log
#                                                      # verdict for one product-side mutant
#
# TF2_EXE overrides the binary (the pre-fix red run points it at the frozen t79
# build); TF2_ROOT overrides the Team Fortress 2/tf root.
#
# ASSERTIONS. Every check prints its own line -- "PASS  <id>" / "RED   <id>  -- <why>" --
# so a failing run names what broke instead of only counting. The last two lines are
# still GATE-RESULT= and PLAYBACK-ERROR-PATHS=: verify-release.sh:399 requires
# ^[[:space:]]*PLAYBACK-ERROR-PATHS=PASS and :412 takes the last TOKEN=PASS|FAIL line
# as the step key, so that token must stay last. No check is inferred from the process
# rc alone: rc is one assertion of its case (A1-exit / B-exit / ...), the claim itself
# is a trace reading.
#
# --selftest feeds the SAME judgement functions -- not a copy of them -- synthetic
# traces with deliberately wrong values and requires every one to go RED; it also runs
# one clean fixture per case and requires zero red there (an assertion that cannot be
# satisfied is as broken as one that cannot fail). It launches nothing: no product, no
# demo, no hostlock. Cost: MEASURED 90.4 s wall on this host (2026-11-09) -- cygwin
# process spawn is expensive here (20 greps timed at 9.9 s); the selftest is in no
# chain, so that cost is paid only when someone asks for the negative control.
#
# PRODUCT-SIDE MUTATIONS are built and driven OUTSIDE this script (D-55 on the frozen
# t103 tree; the E5 serial runner after it). This gate judges the result:
# --mutation-judge <M#> <gate-log> turns one run into KILLED / SURVIVED / UNEXPECTED /
# NOT-CAPABLE / GATE-ERROR, so the rule lives here and --selftest can exercise it.
# D-87/D-88 changed M4's contract: D-first must be red, but D-dup is NOT-CAPABLE for a
# mutant that swallows the re-entered tick (dup counts re-entries only, so it stays 0
# by construction), and E-events is known collateral -- neither is a hit. The verdict
# rules are exercised by --selftest (positive: exact sets KILLED; negative: the M4
# shape must come back NOT-CAPABLE, never KILLED and never SURVIVED).
set -u

REPO=$(cd "$(dirname "$0")" && pwd)
# set -u is on, so an unset override must not be an unbound-variable error: the
# chain calls this gate with no environment at all.
EXE=${TF2_EXE:-}
if [ -z "$EXE" ]; then EXE=$REPO/native/build-nmake/tf2_demo_native.exe; fi
TF=${TF2_ROOT:-}
if [ -z "$TF" ]; then TF="D:/SteamLibrary/steamapps/common/Team Fortress 2/tf"; fi
AUDIO_DEMO="D:/SteamLibrary/steamapps/common/Team Fortress 2/tf/demos/autorecord_2026-07-02_13-26-46.dem"
CAM_DEMO="D:/TF2_Demo_Player/testdata/demos/dfbce608c80a4d72a390c70c597f0377_match-20261101-1959-koth_bagel_rc11.dem"
OUT=${PLAYBACK_OUT:-$REPO/evidence/t93-playback}
TAG=green
ONLY=
SELFTEST=0
MJ_MUT=
MJ_LOG=

while [ $# -gt 0 ]; do
  case $1 in
    --only) ONLY=$2; shift 2 ;;
    --tag) TAG=$2; shift 2 ;;
    --out) OUT=$2; shift 2 ;;
    --selftest) SELFTEST=1; shift ;;
    --mutation-judge)
      MJ_MUT=${2:-}; MJ_LOG=${3:-}
      if [ $# -ge 3 ]; then shift 3; else shift $#; fi
      ;;
    *) echo "usage: $0 [--only A1,A2,B,C,D,E] [--tag NAME] [--out DIR] | --selftest | --mutation-judge M1..M4 GATE-LOG"; exit 64 ;;
  esac
done

PASS=0; RED=0; CASES=0
green() { PASS=$((PASS+1)); echo "PASS  $1"; }
red() { RED=$((RED+1)); echo "RED   $1  -- $2"; }
want() { [ -z "$ONLY" ] || echo ",$ONLY," | grep -q ",$1,"; }
has() { grep -qF -- "$2" "$1" 2>/dev/null; }
nhas() { ! grep -qF -- "$2" "$1" 2>/dev/null; }

# fnum <trace> <line-substring> <field=...>  -> value of the LAST match, or -
fnum() {
  awk -v pat="$2" -v key="$3" 'index($0,pat){ for(i=1;i<=NF;i++){ if(index($i,key)==1){ split($i,KV,"="); v=KV[2]; f=1 } } } END{ if(f) print v; else print "-" }' "$1" 2>/dev/null
}
maxseen() {
  awk 'index($0,"audio: advance"){ for(i=1;i<=NF;i++){ if(index($i,"seen=")==1){ split($i,KV,"="); n=KV[2]+0; if(n>m) m=n } } } END{ print m+0 }' "$1" 2>/dev/null
}
# max over the advances that come AFTER the line containing <marker>. The first
# advance of a run legitimately covers the load-time catch-up (tick 0..N, several
# hundred events), so a whole-trace maximum would be red on a correct seek.
maxseen_after() {
  # If the marker line is missing altogether, fall back to the whole trace: a
  # product that never says "seek reset" must not pass this by printing nothing.
  awk -v pat="$2" '
    index($0,pat){p=1;found=1;next}
    index($0,"audio: advance"){
      for(i=1;i<=NF;i++){ if(index($i,"seen=")==1){ split($i,KV,"="); n=KV[2]+0; if(n>ma) ma=n; if(p&&n>mp) mp=n } }
    }
    END{ print (found ? mp : ma)+0 }' "$1" 2>/dev/null
}
# how many <needle> lines come after the first <marker> line. Keeps a
# maximum-over-a-window honest: a window with no advances in it has max 0 and
# would pass any upper bound by vacuity.
count_after() {
  awk -v pat="$2" -v needle="$3" 'index($0,pat){p=1;next} p&&index($0,needle){n++} END{ print n+0 }' "$1" 2>/dev/null
}
# value of <field=> from the FIRST line containing <marker>, or -
firstf() {
  awk -v pat="$2" -v key="$3" 'index($0,pat){ for(i=1;i<=NF;i++){ if(index($i,key)==1){ n=split($i,KV,"="); v=KV[2]; f=1 } } if(f) exit } END{ if(f) print v; else print "-" }' "$1" 2>/dev/null
}
sha() { sha256sum "$1" 2>/dev/null | cut -d" " -f1; }

# ---------------------------------------------------------------- judgements
# One function per case, taking the artefacts it reads. They exist as functions so
# --selftest can hand them synthetic traces: the selftest exercises the shipping
# judgement, not a lookalike.

judge_A1() { # <trace> <rc> <stderr> <seconds-taken>
  local T=$1 R=$2 E=$3 TOOK=$4
  if [ "$R" = 2 ]; then green A1-exit; else red A1-exit "rc=$R want 2 (a named demo that cannot be opened is a hard refusal)"; fi
  if has "$E" "demo open failed"; then green A1-stderr; else red A1-stderr "stderr has no 'demo open failed'"; fi
  if has "$T" "demo: open failed"; then green A1-trace; else red A1-trace "trace has no 'demo: open failed'"; fi
  if nhas "$T" "mode: window_created=1"; then green A1-window; else red A1-window "a window was created for a demo that does not exist"; fi
  if nhas "$T" "map: lookup"; then green A1-map; else red A1-map "map resolution ran for a demo that does not exist"; fi
  if [ "$TOOK" -le 15 ]; then green A1-time; else red A1-time "refusal took ${TOOK}s (want <=15: before resource startup, so before the window)"; fi
}

judge_A2() { # <trace> <rc>
  local T=$1 R=$2
  if [ "$R" = 0 ]; then green A2-exit; else red A2-exit "rc=$R want 0 (no --demo is still a normal start)"; fi
  if has "$T" "mode: window_created=1"; then green A2-window; else red A2-window "no window without --demo"; fi
  if has "$T" "map: lookup"; then green A2-map; else red A2-map "no map lookup without --demo"; fi
}

judge_B() { # <trace> <rc> <before.bmp> <after.bmp>
  local T=$1 R=$2 BF=$3 AF=$4
  if [ "$R" = 0 ]; then green B-exit; else red B-exit "rc=$R want 0 (the camera script must not fail)"; fi
  if has "$T" "cmd=freefly result=ok detail=on mode=free-look"; then green B-step; else red B-step "the freefly step does not report the mode it left the product in (want detail=on mode=free-look)"; fi
  if has "$T" "camera: mode applied=free-look"; then green B-applied; else red B-applied "no frame-level reading of the free-look mode change"; fi
  local PA AB MK
  PA=$(fnum "$T" "camera: mode applied=free-look" "paused=")
  if [ "$PA" = 1 ]; then green B-paused; else red B-paused "camera: mode applied=free-look paused=$PA (want 1: the mode change is applied on a frame drawn while paused)"; fi
  # The mark is the tick the freeze is measured from (main.cpp:1107-1109). Reading it
  # separately from expect-same-tick's verdict means a match=0 can be told apart from
  # a mark-tick that never ran (which leaves the mark at its -1 initial value).
  AB=$(fnum "$T" "camera: mode applied=free-look" "tick=")
  MK=$(fnum "$T" "camera: mark tick=" "tick=")
  if [ "$MK" != "-" ] && [ "$MK" != "-1" ] && [ "$MK" = "$AB" ]; then green B-mark; else red B-mark "mark-tick recorded tick=$MK, the mode change landed at tick=$AB (want the same, and not the -1 initial value)"; fi
  local MA
  MA=$(fnum "$T" "camera: summary" "modeApplies=")
  if [ "$MA" != "-" ] && [ "$MA" != "0" ]; then green B-summary; else red B-summary "camera: summary modeApplies=$MA (want >=1)"; fi
  # NOT a discriminator, measured 2026-11-07: the PRE-FIX run produced the very same
  # two BMPs, byte for byte (before aeb0b5f0..dffe8, after 3a848650..3a3393a2), so this
  # passes on a mutant too. Kept as a "the capture path is alive" check; the mode-change
  # claim rests on B-applied / B-paused / B-mark / B-clock / B-frozen.
  local B1 B2
  B1=$(sha "$BF"); B2=$(sha "$AF")
  if [ -n "$B1" ] && [ -n "$B2" ] && [ "$B1" != "$B2" ]; then green B-pixels; else red B-pixels "frames before/after the mode change are identical or missing ($B1 / $B2)"; fi
  local FB
  FB=$(fnum "$T" "camera: accept steps=" "failures=")
  if [ "$FB" = 0 ]; then green B-script; else red B-script "camera: accept failures=$FB (want 0: a failed step makes quit exit 1)"; fi
  # The absolute tick is not pinned: the script's "tick 400" is a floor ("wait until
  # at least 400"), and the loop is free to overshoot -- measured 403 and 413. What
  # has to hold is that the applied-frame reading and both snapshots are the SAME
  # tick, i.e. the mode change needed no tick to arrive.
  local TB TA
  TB=$(fnum "$T" "camera: snapshot label=before" "tick="); TA=$(fnum "$T" "camera: snapshot label=after" "tick=")
  if [ "$TB" != "-" ] && [ "$TB" = "$TA" ] && [ "$TB" = "$AB" ]; then green B-clock; else red B-clock "mode applied at tick=$AB, snapshots at $TB/$TA (want one and the same tick)"; fi
  local MF
  MF=$(fnum "$T" "camera: expect what=same-tick" "match=")
  if [ "$MF" = 1 ]; then green B-frozen; else red B-frozen "mark-tick/expect-same-tick match=$MF (want 1: the tick stands still across the mode change)"; fi
}

judge_C() { # <trace> <rc>
  local T=$1 R=$2
  if [ "$R" = 0 ]; then green C-exit; else red C-exit "rc=$R want 0"; fi
  if has "$T" "audio: seek reset from="; then green C-trace; else red C-trace "no 'audio: seek reset' line for a forward seek"; fi
  if has "$T" "to=3000"; then green C-target; else red C-target "the seek reset does not name tick 3000"; fi
  # Window start = the product's own "audio: seek reset" line, i.e. the moment the
  # scheduler ledger was thrown away for the seek. That is the first line of the
  # post-seek playback: nothing before it may belong to the skipped span, and it is
  # the only marker the audio subsystem itself prints. The load-time catch-up (the
  # 746-750 events at tick 0..N) is all BEFORE it, which is why the maximum has to be
  # taken after it rather than over the whole trace (2026-11-07: whole trace 750,
  # after the reset 28).
  local N
  N=$(count_after "$T" "audio: seek reset" "audio: advance")
  if [ "$N" -ge 1 ]; then green C-window; else red C-window "no 'audio: advance' after the seek reset (the window is empty, so C-burst would pass by vacuity)"; fi
  local MS ALL
  MS=$(maxseen_after "$T" "audio: seek reset"); ALL=$(maxseen "$T")
  if [ "$MS" -le 60 ]; then green C-burst; else red C-burst "after the seek reset one audio: advance saw=$MS events (want <=60: the skipped span must not be dumped into one frame; whole-trace max=$ALL comes from the load-time catch-up)"; fi
  if has "$T" "audio: advance tick=3000"; then green C-landing; else red C-landing "no audio: advance at the seek target (want tick=3000)"; fi
}

judge_D() { # <trace> <rc>
  local T=$1 R=$2
  if [ "$R" = 0 ]; then green D-exit; else red D-exit "rc=$R want 0"; fi
  # The tick the first advance lands on is not pinned: after --start-paused the loop is
  # already at tick 16 by the frame the resume is applied (measured), so the claim is
  # about the RANGE, read from the first advance -- not about a literal tick=0 line:
  # the first advance must carry the tick-0 events (>=700 of the 746) and must not
  # count the re-entered tick as a duplicate. seen and dup are read together because
  # either alone can be gamed: a run that replays the tick would raise seen just as
  # well, and one that swallows it would keep dup at 0.
  # D-87/D-88: that last clause is the whole point. Only a RE-ENTERED tick raises dup;
  # a mutant that swallows it (lastTick_=tick) collapses seen and leaves dup at 0, so
  # D-dup cannot carry this claim. D-first does; --mutation-judge reports M4 as
  # NOT-CAPABLE for D-dup instead of pretending the pair was a clean kill.
  local FT S0 DUP
  FT=$(firstf "$T" "audio: advance" "tick=")
  S0=$(firstf "$T" "audio: advance" "seen=")
  if [ "$S0" != "-" ] && [ "$S0" -ge 700 ]; then green D-first; else red D-first "the first advance after the resume (tick=$FT) saw=$S0 events (want >=700: the 746 tick-0 events must not be dropped)"; fi
  DUP=$(firstf "$T" "audio: advance" "dup=")
  if [ "$DUP" = 0 ]; then green D-dup; else red D-dup "the first advance after the resume (tick=$FT) saw dup=$DUP (want 0: a resume must not consume the tick it re-enters at)"; fi
}

judge_E() { # <trace> <rc> <captured.bmp>
  local T=$1 R=$2 B=$3
  if [ "$R" = 0 ]; then green E-exit; else red E-exit "rc=$R want 0"; fi
  local SC ES DO MA
  SC=$(fnum "$T" "audio: timeline" "scheduled=")
  if [ "$SC" = 5323 ]; then green E-timeline; else red E-timeline "audio: timeline scheduled=$SC (want 5323)"; fi
  ES=$(fnum "$T" "mode: summary" "events_seen=")
  if [ "$ES" = 759 ]; then green E-events; else red E-events "mode: summary events_seen=$ES (want 759)"; fi
  DO=$(fnum "$T" "mode: summary" "audio_device_opened=")
  if [ "$DO" = 0 ]; then green E-device; else red E-device "audio_device_opened=$DO (want 0: --audio-disabled must never open the device)"; fi
  if nhas "$T" "audio: seek reset"; then green E-noseek; else red E-noseek "an unseeked run printed 'audio: seek reset'"; fi
  MA=$(fnum "$T" "camera: summary" "modeApplies=")
  if [ "$MA" = 0 ]; then green E-modeapplies; else red E-modeapplies "camera: summary modeApplies=$MA (want 0 on a run that never changes mode)"; fi
  if [ -s "$B" ]; then green E-capture; else red E-capture "no capture frame written to $B"; fi
}

# ------------------------------------------------- mutation verdicts (D-87/D-88)
# --mutation-judge <M#> <gate-log>: one gate log -> one verdict. Kept here (not in a
# driver) so --selftest can prove the rule, and so every runner judges the same way.
#
# M4 is deliberately a two-part claim:
#   * D-first MUST be red. The swallowed tick-0 range is visible in the first advance's
#     seen= (measured 2026-11-10 on D-55: 750 -> 4), so this half is a real detection.
#   * D-dup is NOT-CAPABLE, stated out loud. dup counts ticks that were re-entered; a
#     mutant that swallows the tick never re-enters it, so dup stays 0 no matter what
#     the mutant does (judge_D and the comment above say the same thing). D-dup is kept
#     as a REPLAY detector, not as proof of this repair.
#   * E-events (events_seen=759) is KNOWN COLLATERAL: the same loss counted through case
#     E (759-746=13). It is not a hit and never upgrades the verdict.
MUT_EXPECT_M1="A1-exit A1-stderr A1-trace A1-window A1-map A1-time"
MUT_EXPECT_M2="B-applied B-paused B-mark B-summary B-clock"
MUT_EXPECT_M3="C-trace C-target C-window C-burst"
MUT_M4_REQUIRED="D-first"
MUT_M4_COLLATERAL="E-events"
MUT_M4_NOTCAPABLE="D-dup"
MUT_M4_REASON="dup counts re-entered ticks only; a swallowed tick keeps dup=0 by construction (judge_D), so D-dup cannot prove this mutant -- D-first proves the loss, E-events is collateral and is not counted"

mutation_redset() { # <gate-log> -> red ids in gate order
  awk '/^RED   /{print $2}' "$1" 2>/dev/null | tr '\n' ' ' | sed 's/[[:space:]]*$//'
}
word_in() { case " $1 " in *" $2 "*) return 0 ;; esac; return 1; }

judge_mutation() { # <M#> <gate-log> -> verdict on stdout; rc 0 KILLED 1 SURVIVED 2 UNEXPECTED 3 NOT-CAPABLE 9 GATE-ERROR
  local M=$1 L=$2 reds want miss extra x
  if [ ! -f "$L" ]; then
    echo "MUTATION-VERDICT=$M GATE-ERROR"
    echo "MUTATION-GATE-ERROR=$M reason=no gate log at $L"
    return 9
  fi
  if ! grep -qE '^PLAYBACK-ERROR-PATHS=(PASS|FAIL)$' "$L"; then
    echo "MUTATION-VERDICT=$M GATE-ERROR"
    echo "MUTATION-GATE-ERROR=$M reason=log has no final PLAYBACK-ERROR-PATHS= line (truncated run or aborted gate; do not read it as SURVIVED)"
    return 9
  fi
  reds=$(mutation_redset "$L")
  if [ "$M" = "M4" ]; then
    want=$MUT_M4_REQUIRED
    miss=; extra=
    word_in "$reds" "$want" || miss=$want
    for x in $reds; do
      [ "$x" = "$want" ] && continue
      [ "$x" = "$MUT_M4_COLLATERAL" ] && continue
      extra="$extra $x"
    done
    extra=${extra# }
    echo "MUTATION-REDSET=$M reds=\"${reds:--}\" required=\"$want\" collateral=\"$MUT_M4_COLLATERAL\" not_capable=\"$MUT_M4_NOTCAPABLE\""
    if [ -n "$miss" ]; then
      echo "MUTATION-VERDICT=$M SURVIVED missing=\"$miss\""
      return 1
    fi
    if [ -n "$extra" ]; then
      echo "MUTATION-VERDICT=$M UNEXPECTED extra=\"$extra\""
      return 2
    fi
    echo "MUTATION-CAUGHT=$M by=$want"
    echo "MUTATION-NOT-CAPABLE=$M.$MUT_M4_NOTCAPABLE reason=\"$MUT_M4_REASON\""
    echo "MUTATION-VERDICT=$M NOT-CAPABLE caught_by=$want not_capable=$MUT_M4_NOTCAPABLE"
    return 3
  fi
  case "$M" in
    M1) want=$MUT_EXPECT_M1 ;;
    M2) want=$MUT_EXPECT_M2 ;;
    M3) want=$MUT_EXPECT_M3 ;;
    *) echo "MUTATION-VERDICT=$M GATE-ERROR"
       echo "MUTATION-GATE-ERROR=$M reason=unknown mutant (want M1..M4)"
       return 9 ;;
  esac
  miss=; extra=
  for x in $want; do word_in "$reds" "$x" || miss="$miss $x"; done
  for x in $reds; do word_in "$want" "$x" || extra="$extra $x"; done
  miss=${miss# }; extra=${extra# }
  echo "MUTATION-REDSET=$M reds=\"${reds:--}\" expected=\"$want\" missing=\"$miss\" extra=\"$extra\""
  if [ -n "$extra" ]; then echo "MUTATION-VERDICT=$M UNEXPECTED extra=\"$extra\""; return 2; fi
  if [ -n "$miss" ]; then echo "MUTATION-VERDICT=$M SURVIVED missing=\"$miss\""; return 1; fi
  echo "MUTATION-VERDICT=$M KILLED"
  return 0
}

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
  # Drop this case's own artefacts first: a stale .bmp or trace from an earlier
  # run in the same --out would otherwise satisfy an assertion that this run did
  # not produce (the same class of bug as the shared iso directory in D-45).
  rm -f "$O.trace" "$O.rc" "$O.err" "$O.out"
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

# ---------------------------------------------------------------- selftest
# Synthetic artefacts: no product, no demo, no lock, no GPU. Each mutant must be
# caught by the assertion named beside it; the clean fixtures must produce no red
# at all. This is the negative control for the JUDGEMENT. The product-side half
# (M1-M4, docs/T93-PLAYBACK-FIXES.md section 5) does not exist -- D-55.
selftest() {
  local ST
  ST=$(mktemp -d) || { echo "SELFTEST-FAIL: mktemp"; return 1; }

  cat >"$ST/a1.trace" <<'EOF'
demo: open failed path=D:/nowhere/no-such.dem reason=cannot open
EOF
  printf 'demo open failed: D:/nowhere/no-such.dem: cannot open\n' >"$ST/a1.err"
  : >"$ST/empty.err"; : >"$ST/empty.trace"
  { cat "$ST/a1.trace"; echo "mode: window_created=1 title=TF2 Demo Player"; } >"$ST/a1-window.trace"
  { cat "$ST/a1.trace"; echo "map: lookup map=maps/ctf_2fort.bsp result=ok"; } >"$ST/a1-map.trace"

  cat >"$ST/a2.trace" <<'EOF'
mode: window_created=1 title=TF2 Demo Player
map: lookup map=maps/ctf_2fort.bsp result=ok
EOF
  grep -v "mode: window_created" "$ST/a2.trace" >"$ST/a2-nowin.trace"
  grep -v "map: lookup" "$ST/a2.trace" >"$ST/a2-nomap.trace"

  cat >"$ST/b.trace" <<'EOF'
camera: accept step=7 cmd=freefly result=ok detail=on mode=free-look
camera: mark tick=413
camera: mode applied=free-look tick=413 paused=1
camera: snapshot label=before valid=1 tick=413 paused=1
camera: snapshot label=after valid=1 tick=413 paused=1
camera: accept steps=16 failures=0 captures=2
camera: expect what=same-tick marked=413 actual=413 match=1
camera: summary mode=free-look modeChanges=1 modeApplies=1
EOF
  printf 'BEFORE-FRAME' >"$ST/b1.bmp"; printf 'AFTER-FRAME' >"$ST/b2.bmp"
  sed 's/paused=1/paused=0/' "$ST/b.trace" >"$ST/b-paused0.trace"
  grep -v "camera: mode applied" "$ST/b.trace" >"$ST/b-noapplied.trace"
  sed 's/mark tick=413/mark tick=-1/' "$ST/b.trace" >"$ST/b-markminus.trace"
  sed 's/mark tick=413/mark tick=400/' "$ST/b.trace" >"$ST/b-mark400.trace"
  sed 's/modeApplies=1/modeApplies=0/' "$ST/b.trace" >"$ST/b-nosummary.trace"
  sed 's/failures=0/failures=1/' "$ST/b.trace" >"$ST/b-scriptfail.trace"
  sed 's/mode applied=free-look tick=413/mode applied=free-look tick=410/' "$ST/b.trace" >"$ST/b-clock.trace"
  sed 's/match=1/match=0/' "$ST/b.trace" >"$ST/b-unfrozen.trace"
  sed 's/detail=on mode=free-look/detail=off mode=first-person/' "$ST/b.trace" >"$ST/b-nomode.trace"

  cat >"$ST/c.trace" <<'EOF'
audio: timeline scheduled=5323 firstTick=0 lastTick=5322
audio: advance tick=5 seen=750 dup=0
audio: seek reset from=5 to=3000
audio: advance tick=3000 seen=0 dup=0
audio: advance tick=3025 seen=28 dup=0
EOF
  grep -v "audio: seek reset" "$ST/c.trace" >"$ST/c-noreset.trace"
  sed 's/to=3000/to=2999/' "$ST/c.trace" >"$ST/c-target.trace"
  sed 's/seen=28/seen=900/' "$ST/c.trace" >"$ST/c-burst.trace"
  grep -v "audio: advance tick=3000" "$ST/c.trace" >"$ST/c-noland.trace"
  awk 'index($0,"audio: seek reset"){p=1;print;next} p&&index($0,"audio: advance"){next} {print}' "$ST/c.trace" >"$ST/c-emptywin.trace"

  cat >"$ST/d.trace" <<'EOF'
audio: advance tick=16 seen=750 dup=0
audio: advance tick=17 seen=5 dup=0
EOF
  sed 's/tick=16 seen=750/tick=16 seen=4/' "$ST/d.trace" >"$ST/d-starved.trace"
  sed 's/dup=0/dup=2/' "$ST/d.trace" >"$ST/d-dup.trace"

  cat >"$ST/e.trace" <<'EOF'
audio: timeline scheduled=5323 thirdParty=0
mode: summary events_seen=759 events_scheduled=5323 audio_device_opened=0 audio_device_open_attempts=0
camera: summary mode=first-person modeChanges=0 modeApplies=0
EOF
  printf 'CAPTURE-FRAME' >"$ST/e.bmp"
  sed 's/scheduled=5323/scheduled=5000/' "$ST/e.trace" >"$ST/e-sched.trace"
  sed 's/events_seen=759/events_seen=758/' "$ST/e.trace" >"$ST/e-events.trace"
  sed 's/audio_device_opened=0/audio_device_opened=1/' "$ST/e.trace" >"$ST/e-device.trace"
  sed 's/modeApplies=0/modeApplies=1/' "$ST/e.trace" >"$ST/e-modeapplies.trace"
  { cat "$ST/e.trace"; echo "audio: seek reset from=1 to=2"; } >"$ST/e-seek.trace"

  local CLEAN CLEAN_RED CLEAN_PASS ST_PASS=0 ST_FAIL=0 ST_MJ_PASS=0 ST_MJ_FAIL=0
  CLEAN=$(
    judge_A1 "$ST/a1.trace" 2 "$ST/a1.err" 0
    judge_A2 "$ST/a2.trace" 0
    judge_B  "$ST/b.trace" 0 "$ST/b1.bmp" "$ST/b2.bmp"
    judge_C  "$ST/c.trace" 0
    judge_D  "$ST/d.trace" 0
    judge_E  "$ST/e.trace" 0 "$ST/e.bmp"
  )
  CLEAN_RED=$(printf '%s\n' "$CLEAN" | grep -c "^RED   " || true)
  CLEAN_PASS=$(printf '%s\n' "$CLEAN" | grep -c "^PASS  " || true)
  if [ "$CLEAN_RED" -eq 0 ] && [ "$CLEAN_PASS" -eq 35 ]; then
    echo "SELFTEST-CLEAN-OK 35 pass / 0 red (the fixtures satisfy the shipping judgement)"
  else
    echo "SELFTEST-CLEAN-FAIL pass=$CLEAN_PASS red=$CLEAN_RED (want 35/0)"
    printf '%s\n' "$CLEAN" | grep "^RED" | sed 's/^/  /'
    ST_FAIL=$((ST_FAIL+1))
  fi

  self_red() { # <label> <expected assertion id> <command and arguments...>
    local label=$1 wid=$2; shift 2
    local o
    o=$("$@")
    if printf '%s\n' "$o" | grep -q "^RED   $wid  --"; then
      ST_PASS=$((ST_PASS+1))
    else
      ST_FAIL=$((ST_FAIL+1))
      echo "SELFTEST-MUTANT-SURVIVED $label (want RED $wid)"
      printf '%s\n' "$o" | sed 's/^/    /'
    fi
  }

  self_red "A1 rc=0"              A1-exit    judge_A1 "$ST/a1.trace" 0 "$ST/a1.err" 0
  self_red "A1 stderr silent"     A1-stderr  judge_A1 "$ST/a1.trace" 2 "$ST/empty.err" 0
  self_red "A1 trace silent"      A1-trace   judge_A1 "$ST/empty.trace" 2 "$ST/a1.err" 0
  self_red "A1 window opened"     A1-window  judge_A1 "$ST/a1-window.trace" 2 "$ST/a1.err" 0
  self_red "A1 map resolved"      A1-map     judge_A1 "$ST/a1-map.trace" 2 "$ST/a1.err" 0
  self_red "A1 refusal slow"      A1-time    judge_A1 "$ST/a1.trace" 2 "$ST/a1.err" 16

  self_red "A2 rc=1"              A2-exit    judge_A2 "$ST/a2.trace" 1
  self_red "A2 no window"         A2-window  judge_A2 "$ST/a2-nowin.trace" 0
  self_red "A2 no map"            A2-map     judge_A2 "$ST/a2-nomap.trace" 0

  self_red "B rc=1"               B-exit     judge_B "$ST/b.trace" 1 "$ST/b1.bmp" "$ST/b2.bmp"
  self_red "B step reports off"   B-step     judge_B "$ST/b-nomode.trace" 0 "$ST/b1.bmp" "$ST/b2.bmp"
  self_red "B no applied line"    B-applied  judge_B "$ST/b-noapplied.trace" 0 "$ST/b1.bmp" "$ST/b2.bmp"
  self_red "B applied unpaused"   B-paused   judge_B "$ST/b-paused0.trace" 0 "$ST/b1.bmp" "$ST/b2.bmp"
  self_red "B mark left at -1"    B-mark     judge_B "$ST/b-markminus.trace" 0 "$ST/b1.bmp" "$ST/b2.bmp"
  self_red "B mark elsewhere"     B-mark     judge_B "$ST/b-mark400.trace" 0 "$ST/b1.bmp" "$ST/b2.bmp"
  self_red "B modeApplies=0"      B-summary  judge_B "$ST/b-nosummary.trace" 0 "$ST/b1.bmp" "$ST/b2.bmp"
  self_red "B identical frames"   B-pixels   judge_B "$ST/b.trace" 0 "$ST/b1.bmp" "$ST/b1.bmp"
  self_red "B script failed"      B-script   judge_B "$ST/b-scriptfail.trace" 0 "$ST/b1.bmp" "$ST/b2.bmp"
  self_red "B mode drifted tick"  B-clock    judge_B "$ST/b-clock.trace" 0 "$ST/b1.bmp" "$ST/b2.bmp"
  self_red "B tick not frozen"    B-frozen   judge_B "$ST/b-unfrozen.trace" 0 "$ST/b1.bmp" "$ST/b2.bmp"

  self_red "C rc=1"               C-exit     judge_C "$ST/c.trace" 1
  self_red "C no reset line"      C-trace    judge_C "$ST/c-noreset.trace" 0
  self_red "C wrong target"       C-target   judge_C "$ST/c-target.trace" 0
  self_red "C empty window"       C-window   judge_C "$ST/c-emptywin.trace" 0
  self_red "C dumped burst"       C-burst    judge_C "$ST/c-burst.trace" 0
  self_red "C no landing"         C-landing  judge_C "$ST/c-noland.trace" 0

  self_red "D rc=1"               D-exit     judge_D "$ST/d.trace" 1
  self_red "D starved tick 0"     D-first    judge_D "$ST/d-starved.trace" 0
  self_red "D duplicate re-entry" D-dup      judge_D "$ST/d-dup.trace" 0

  self_red "E rc=1"               E-exit     judge_E "$ST/e.trace" 1 "$ST/e.bmp"
  self_red "E scheduled drifted"  E-timeline judge_E "$ST/e-sched.trace" 0 "$ST/e.bmp"
  self_red "E events drifted"     E-events   judge_E "$ST/e-events.trace" 0 "$ST/e.bmp"
  self_red "E device opened"      E-device   judge_E "$ST/e-device.trace" 0 "$ST/e.bmp"
  self_red "E unseeked run seeked" E-noseek  judge_E "$ST/e-seek.trace" 0 "$ST/e.bmp"
  self_red "E mode applied"       E-modeapplies judge_E "$ST/e-modeapplies.trace" 0 "$ST/e.bmp"
  self_red "E no capture"         E-capture  judge_E "$ST/e.trace" 0 "$ST/no-such.bmp"

  # D-87/D-88: synthetic GATE LOGS for the mutation verdict. These are logs, not
  # traces: judge_mutation reads only the RED lines plus the final token, so the
  # positive control can assert KILLED without a product, and the negative control can
  # assert that the MEASURED M4 shape comes back NOT-CAPABLE -- never KILLED (that would
  # hide the capability gap) and never SURVIVED (that would understate the detection).
  mk_log() { # <file> <red ids...>
    local f=$1; shift
    {
      echo "PASS  A2-exit"
      echo "PASS  E-timeline"
      local r
      for r in "$@"; do echo "RED   $r  -- synthetic red for the mutation-verdict selftest"; done
      echo "GATE tag=selftest cases=6 pass=2 red=$# seconds=0"
      echo "GATE-RESULT=FAIL"
      echo "PLAYBACK-ERROR-PATHS=FAIL"
    } >"$ST/$f"
  }
  mk_log m1.log A1-exit A1-stderr A1-trace A1-window A1-map A1-time
  mk_log m2.log B-applied B-paused B-mark B-summary B-clock
  mk_log m3.log C-trace C-target C-window C-burst
  mk_log m4.log D-first E-events
  mk_log m1-surv.log A1-exit A1-stderr A1-trace A1-window A1-map
  mk_log m4-surv.log E-events
  mk_log m1-extra.log A1-exit A1-stderr A1-trace A1-window A1-map A1-time B-applied
  mk_log m4-extra.log D-first E-events B-applied
  mk_log m4-dup.log D-first D-dup
  { echo "PASS  A1-exit"; echo "GATE tag=selftest cases=1"; } >"$ST/trunc.log"

  self_judge() { # <label> <want rc> <want line fragment> <M#> <log>
    local label=$1 wantrc=$2 tok=$3 m=$4 l=$5 o rc
    o=$(judge_mutation "$m" "$l"); rc=$?
    if [ "$rc" = "$wantrc" ] && printf '%s\n' "$o" | grep -qF -- "$tok"; then
      ST_MJ_PASS=$((ST_MJ_PASS+1))
    else
      ST_MJ_FAIL=$((ST_MJ_FAIL+1))
      echo "SELFTEST-MUTATION-VERDICT-FAIL $label (want rc=$wantrc and '$tok', got rc=$rc)"
      printf '%s\n' "$o" | sed 's/^/    /'
    fi
  }

  self_judge "M1 exact red set"            0 "MUTATION-VERDICT=M1 KILLED"   M1 "$ST/m1.log"
  self_judge "M2 exact red set"            0 "MUTATION-VERDICT=M2 KILLED"   M2 "$ST/m2.log"
  self_judge "M3 exact red set"            0 "MUTATION-VERDICT=M3 KILLED"   M3 "$ST/m3.log"
  # The negative control this debt exists for: the measured M4 shape (D-first + the
  # E-events collateral, D-dup green) must be NOT-CAPABLE, with D-dup named as the gap.
  self_judge "M4 swallow is NOT-CAPABLE"  3 "MUTATION-VERDICT=M4 NOT-CAPABLE caught_by=D-first" M4 "$ST/m4.log"
  self_judge "M4 names D-dup as the gap"  3 "MUTATION-NOT-CAPABLE=M4.D-dup reason=" M4 "$ST/m4.log"
  self_judge "M4 E-events is collateral"  3 "collateral=\"E-events\""      M4 "$ST/m4.log"
  self_judge "M1 missing one red"         1 "MUTATION-VERDICT=M1 SURVIVED" M1 "$ST/m1-surv.log"
  self_judge "M4 without D-first"         1 "MUTATION-VERDICT=M4 SURVIVED" M4 "$ST/m4-surv.log"
  self_judge "M1 extra red"               2 "MUTATION-VERDICT=M1 UNEXPECTED" M1 "$ST/m1-extra.log"
  self_judge "M4 extra non-collateral"    2 "MUTATION-VERDICT=M4 UNEXPECTED" M4 "$ST/m4-extra.log"
  self_judge "M4 D-dup red = other mutant" 2 "MUTATION-VERDICT=M4 UNEXPECTED" M4 "$ST/m4-dup.log"
  self_judge "truncated log"              9 "MUTATION-VERDICT=M1 GATE-ERROR" M1 "$ST/trunc.log"

  echo "PLAYBACK-GATE-SELFTEST: mutants $ST_PASS killed / $ST_FAIL survived; clean fixtures $CLEAN_PASS pass / $CLEAN_RED red"
  echo "SELFTEST-MUTATION-VERDICT: $ST_MJ_PASS pass / $ST_MJ_FAIL fail (M4 must be NOT-CAPABLE, never KILLED and never SURVIVED)"
  rm -rf "$ST"
  if [ "$ST_FAIL" -eq 0 ] && [ "$ST_MJ_FAIL" -eq 0 ] && [ "$CLEAN_RED" -eq 0 ] && [ "$CLEAN_PASS" -eq 35 ]; then
    echo "PLAYBACK-GATE-SELFTEST=PASS"
    return 0
  fi
  echo "PLAYBACK-GATE-SELFTEST=FAIL"
  return 1
}

# --mutation-judge: judge a gate log and exit. Nothing is launched; no product, no lock.
if [ -n "$MJ_MUT" ] || [ -n "$MJ_LOG" ]; then
  if [ -z "$MJ_MUT" ] || [ -z "$MJ_LOG" ]; then
    echo "usage: $0 --mutation-judge M1..M4 GATE-LOG"
    exit 64
  fi
  judge_mutation "$MJ_MUT" "$MJ_LOG"
  exit $?
fi

if [ "$SELFTEST" = 1 ]; then
  selftest
  exit $?
fi

START=$SECONDS
mkdir -p "$OUT" || exit 1
OUTW=$(cygpath -m "$OUT")
# The two ini files the product writes on exit, in a throwaway directory. Nothing
# this gate starts may touch the real per-user settings (D-22).
ISO_DIR=$(mktemp -d) || exit 1
trap 'rm -rf "$ISO_DIR"' EXIT
ISOW=$(cygpath -m "$ISO_DIR")

echo "gate  exe=$EXE"
echo "gate  tf=$TF"
echo "gate  out=$OUT tag=$TAG only=${ONLY:-all}"

# ---------------------------------------------------------------- A: missing demo
if want A1; then
  CASES=$((CASES+1))
  run A1-missing-demo "$EXE" --tf-root "$TF" --demo "$OUTW/no-such.dem" --hidden-window --audio-disabled
  T=$OUT/$TAG-A1-missing-demo.trace
  R=$(cat "$OUT/$TAG-A1-missing-demo.rc" 2>/dev/null || echo -)
  judge_A1 "$T" "$R" "$OUT/$TAG-A1-missing-demo.err" "$TOOK"
fi

# ------------------------------------------------- A2: no --demo still starts
if want A2; then
  CASES=$((CASES+1))
  { echo "frames 3"; echo "quit"; } >"$OUT/$TAG-a2.script"
  run A2-default-map "$EXE" --tf-root "$TF" --hidden-window --audio-disabled --camera-script "$OUTW/$TAG-a2.script"
  T=$OUT/$TAG-A2-default-map.trace
  R=$(cat "$OUT/$TAG-A2-default-map.rc" 2>/dev/null || echo -)
  judge_A2 "$T" "$R"
fi

# --------------------------------------------------- B: camera mode while paused
if want B; then
  CASES=$((CASES+1))
  {
    echo "resume"
    echo "tick 400"
    echo "pause"
    echo "frames 4"
    # Without this step expect-same-tick compares against g_scriptTickMark's -1
    # initial value (main.cpp:765) and fails by construction -- that was the
    # 2026-11-07 red: the script was wrong, not the product's freeze.
    echo "mark-tick"
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
  rm -f "$OUT/$TAG-b-before.bmp" "$OUT/$TAG-b-after.bmp"
  run B-freefly-paused "$EXE" --tf-root "$TF" --demo "$CAM_DEMO" --hidden-window --audio-disabled --camera-script "$OUTW/$TAG-b.script"
  T=$OUT/$TAG-B-freefly-paused.trace
  R=$(cat "$OUT/$TAG-B-freefly-paused.rc" 2>/dev/null || echo -)
  judge_B "$T" "$R" "$OUT/$TAG-b-before.bmp" "$OUT/$TAG-b-after.bmp"
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
  R=$(cat "$OUT/$TAG-C-forward-seek.rc" 2>/dev/null || echo -)
  judge_C "$T" "$R"
fi

# ------------------------------------------- D: --start-paused resume keeps tick 0
if want D; then
  CASES=$((CASES+1))
  { echo "resume"; echo "frames 30"; echo "pause"; echo "frames 4"; echo "quit"; } >"$OUT/$TAG-d.script"
  run D-start-paused "$EXE" --tf-root "$TF" --demo "$AUDIO_DEMO" --start-paused --hidden-window --audio-disabled --camera-script "$OUTW/$TAG-d.script"
  T=$OUT/$TAG-D-start-paused.trace
  R=$(cat "$OUT/$TAG-D-start-paused.rc" 2>/dev/null || echo -)
  judge_D "$T" "$R"
fi

# -------------------------------------------------------------- E: unchanged runs
if want E; then
  CASES=$((CASES+1))
  rm -f "$OUT/$TAG-e.bmp"
  run E-baseline "$EXE" --tf-root "$TF" --demo "$AUDIO_DEMO" --hidden-window --audio-disabled --capture-tick 600 --capture-frame "$OUTW/$TAG-e.bmp"
  T=$OUT/$TAG-E-baseline.trace
  R=$(cat "$OUT/$TAG-E-baseline.rc" 2>/dev/null || echo -)
  judge_E "$T" "$R" "$OUT/$TAG-e.bmp"
fi

# A --only that names no case must not pass: zero assertions is not a clean run.
if [ "$CASES" = 0 ]; then red GATE-CASES "--only '$ONLY' selected no case (zero assertions is not a pass)"; fi

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
