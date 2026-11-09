#!/usr/bin/env bash
# logstf-check.sh -- the T54 gate: the embedded logs.tf viewer.
#
# WHY THIS EXISTS
#   T54 asked for a logs panel that is empty until a URL is imported, that
#   validates the host before navigating, that resizes/scrolls/navigates like a
#   browser, that keeps the demo playing while it is open, and that says so out
#   loud when the host has no web-view runtime. A panel is easy to demo and hard
#   to pin, so every one of those is a reading here:
#
#     * logstf_viewer_probe -- the product's own URL policy, geometry, state
#       machine, runtime probe and frame-time histogram, driven through a
#       recording fake host. "The empty panel navigated nowhere" is a counter on
#       the controller the product runs, not a claim about a screenshot.
#     * the product, run with --hidden-window --audio-disabled, driving the same
#       controller through --logstf-script. The trace's logstf: summary line is
#       the product's own answer; the runtime line names the web-view runtime it
#       found (or why it found none) and the package's loader status.
#     * a frame-time comparison between a run with the panel closed and a run
#       with it open, from the product's own measured intervals, plus the demo's
#       tick advance in the panel-open run. That is "playback stays usable" as a
#       number instead of an assurance.
#
# WHAT THIS GATE DOES NOT CLAIM
#   It does not claim that a page's pixels appear in the pane. Nothing here
#   reads a pixel of the panel; the evidence is the controller state, the host's
#   own answers and the runtime's own version string. That is stated in the
#   report as NOT VERIFIED with the cheapest next step, not folded into a PASS.
#
# MUTATIONS (--mutation)
#   Four source mutations, each reintroducing one defect the gate exists to
#   catch, each rebuilt for real and each restored by byte copy with cmp:
#     m1 wrong-host accepted   -- the allow-list returns true for any host
#     m2 empty-state prefetch  -- open() navigates a default page
#     m3 silent runtime failure-- the unavailable state loses its reason/fallback
#     m4 resize ignored        -- setWidth stops clamping (the drag leaves the client)
#     m6 script never runs     -- the scroll call is counted, the page never moves
#     m7 history flag only     -- CanGoBack=1, the address never changes
#     m8 JSON not decoded      -- the browser's quoted result parses as 0
#     m9 script truncated      -- the scroll script is built into char[160] and cut
#   Plus capture mutations over the probe's own reading lines, so each needle is
#   shown to discriminate.
#
# Usage: bash logstf-check.sh [--mutation]
# Exit:  0 = every assertion held (or every mutation went red); 1 = otherwise.

set -uo pipefail
cd "$(dirname "$0")"
export LC_ALL=C

EXE=native/build-nmake/tf2_demo_native.exe
PROBE=native/build-nmake/logstf_viewer_probe.exe
TF="D:/SteamLibrary/steamapps/common/Team Fortress 2/tf"
BAGEL="D:/TF2_Demo_Player/testdata/demos/4a9bfb9276509d0ec5f5fdc722a95b17_match-20260927-0239-koth_bagel_rc13.dem"
OUT="${LOGSTF_OUT:-.scratch/logstf}"
RUN_TIMEOUT=180
CAPTURE_TICK=600

fail=0
for path in "$EXE" "$PROBE" "$TF" "$BAGEL"; do
  [ -e "$path" ] || { echo "FATAL: missing input: $path"; exit 1; }
done

PY="C:/Users/Administrator/.workbuddy-ai/binaries/python/versions/3.13.12/python.exe"
[ -x "$PY" ] || PY="C:/Users/Administrator/AppData/Local/Programs/Python/Python310/python.exe"
[ -x "$PY" ] || { echo "FATAL: no python interpreter found"; exit 1; }
for tool in cygpath timeout cmp sed; do
  command -v "$tool" >/dev/null 2>&1 || { echo "FATAL: missing tool: $tool"; exit 1; }
done
mkdir -p "$OUT"

MUTATION=0
for arg in "$@"; do
  case "$arg" in
    --mutation) MUTATION=1 ;;
    *) echo "unknown argument: $arg"; exit 2 ;;
  esac
done

assert_eq() {
  if [ "${2:-<none>}" = "$3" ]; then echo "  OK   $1 = $3"
  else echo "  FAIL $1 = ${2:-<none>}, expected $3"; fail=1; fi
}
assert_ge() {
  if [ -n "${2:-}" ] && [ "${2:-x}" -ge "$3" ] 2>/dev/null; then echo "  OK   $1 = $2 (>= $3)"
  else echo "  FAIL $1 = ${2:-<none>}, expected >= $3"; fail=1; fi
}
assert_nonempty() {
  if [ -n "${2:-}" ] && [ "${2:-}" != "-" ]; then echo "  OK   $1 is named"
  else echo "  FAIL $1 is empty (must never be silent)"; fail=1; fi
}
assert_contains() {
  case "$2" in *"$3"*) echo "  OK   $1" ;; *) echo "  FAIL $1 (wanted: $3)"; fail=1 ;; esac
}
# The scroll verdicts live in ONE place, because the negative controls below must
# measure what the forward run measures -- a control that checks something else
# proves nothing about this gate.
#
# The distinction the Lead required, made into separate assertions:
#   "the offset was READ"     -- something came back from the page at all;
#   "the answer was a NUMBER" -- it decoded to digits, not null and not a quoted
#                                JSON envelope;
#   "the page actually MOVED" -- the offset is greater than zero.
# A single assertion of the form "offset == expected" collapses all three and is
# satisfied by 0 == 0 when nothing was read; that is exactly how the truncation
# defect of 2026-10-23 stayed green on a page 1729px tall in a 608px viewport.
SCROLL_FIRST_BAD=none
_scroll_check() { # _scroll_check <name> <1|0>
  if [ "$2" = "1" ]; then echo "  OK   $1"; return 0; fi
  echo "  FAIL $1"
  if [ "$SCROLL_FIRST_BAD" = "none" ]; then SCROLL_FIRST_BAD="$1"; fi
  fail=1
  return 0
}
scroll_verdicts() { # scroll_verdicts <summary-line>; sets SCROLL_FIRST_BAD
  local summary="$1"
  local page viewport offset raw failures
  page="$(pvalue "$summary" scrollPage)"
  viewport="$(pvalue "$summary" scrollViewport)"
  offset="$(pvalue "$summary" scrollOffset)"
  raw="$(pvalue "$summary" scrollRaw)"
  failures="$(pvalue "$summary" scriptFailures)"
  SCROLL_FIRST_BAD=none
  # 1. Something came back. A script that was never issued leaves this at the
  #    "no answer" sentinel, which is the only reading a never-run script cannot
  #    counterfeit: every counter around it (scrollCalls, scriptFailures) stays
  #    green, as m6 demonstrates.
  _scroll_check "scroll_offset_was_read"     "$([ -n "$raw" ] && [ "$raw" != "none" ] && echo 1 || echo 0)"
  # 2. The answer decoded to digits. An undecoded JSON envelope starts with a
  #    quote; a page-side SyntaxError arrives as the literal null. Both parse as
  #    0 through atoi(), so without this assertion they cannot be told apart from
  #    a page that did not move (m8 and m9).
  # A page answers a float (the 2026-10-23 run read "480.0000305175781"), so the
  # numeric test allows one decimal point and requires at least one digit: a
  # digit-only test would redden the healthy case, and a permissive one would
  # accept the quoted envelope m8 produces.
  case "$raw" in
    ""|none|null|*[!0-9.]*) scroll_raw_numeric=0 ;;
    *) case "$raw" in *[0-9]*) scroll_raw_numeric=1 ;; *) scroll_raw_numeric=0 ;; esac ;;
  esac
  _scroll_check "scroll_raw_is_number"       "$scroll_raw_numeric"
  # 3. The page moved -- asserted only when the page COULD move. A viewport
  #    taller than the page cannot scroll, and "offset 0" there would be a
  #    vacuous pass.
  if [ -n "$page" ] && [ -n "$viewport" ] && [ "$page" -gt "$viewport" ] 2>/dev/null; then
    _scroll_check "scroll_page_moved"        "$([ -n "$offset" ] && [ "$offset" -gt 0 ] 2>/dev/null && echo 1 || echo 0)"
  else
    echo "  NOT-VERIFIED scroll_page_moved: the imported page is not taller than"
    echo "               the pane (page=$page viewport=$viewport), so a scroll cannot"
    echo "               move it and offset 0 proves nothing."
    echo "               (owner: t54-logstf; cheapest next step: import a log page"
    echo "               with more than one screen of text, re-run --logstf-script scroll)"
  fi
  _scroll_check "scroll_script_did_not_fail"  "$([ "$failures" = "0" ] && echo 1 || echo 0)"
}

pvalue() { # pvalue <line> <field>
  printf '%s\n' "$1" | grep -oE "$2=[^ ]*" | head -1 | cut -d= -f2-
}

# run_product <label> <extra args...> -- one quiet run, trace captured.
#
# THE SAFETY FLAGS ARE CHECKED ON THE ARGV THAT WILL ACTUALLY RUN.
#   The first version of this guard inspected a constant string and sat at file
#   scope, where "return 99" only prints "can only return from a function" and
#   the script carries on to launch the product: a guard that shouts and then
#   launches is worse than no guard, because it looks like one. It also checked
#   the wrong object -- a guard over the helper's own flags protects nothing if
#   the argv built below is what loses one (outline-check.sh:321-325).
#   So: build the argv first, check THAT, and return non-zero without launching.
#   --audio-disabled exists because a capture run on 2026-10-22 played twenty
#   seconds of demo audio into a room while every reading was green; the only
#   detector was a person in the next room. --hidden-window keeps a card-sized
#   capture from stealing focus.
# One predicate, used by the launch helper and tested by the gate itself.
# The two flags are matched SEPARATELY on purpose: one pattern that requires
# " --hidden-window " followed immediately by " --audio-disabled " only matches
# when no other argument sits between them, and the gate's own self-test caught it
# rejecting a good argv. A predicate that refuses everything is as useless as one
# that refuses nothing -- and it fails in the direction that looks safe.
safety_check() { # safety_check <argv...>
  case " $* " in *" --hidden-window "*) : ;; *) return 99 ;; esac
  case " $* " in *" --audio-disabled "*) : ;; *) return 98 ;; esac
  return 0
}

run_product() {
  local label="$1"; shift
  local trace
  trace="$(cygpath -m "$PWD/$OUT/$label.trace")"
  local -a argv=(--tf-root "$TF" --demo "$BAGEL" --hidden-window --audio-disabled
                 --capture-frame "$OUT/$label.bmp" --capture-tick "$CAPTURE_TICK"
                 --settings-file "$(cygpath -m "$PWD/$OUT/$label.settings")"
                 --ui-settings-file "$(cygpath -m "$PWD/$OUT/$label.ui.ini")" "$@")
  if ! safety_check "${argv[@]}"; then
    echo "REFUSED: run_product builds an argv without the safety flags" >&2
       echo "99" > "$OUT/$label.rc"
    echo "  $label REFUSED (argv lacks the safety flags; the product was not launched)"
    return 0
  fi
  rm -f "$OUT/$label.trace" "$OUT/$label.rc"
  TF2_NATIVE_TRACE="$trace" timeout "$RUN_TIMEOUT" "$EXE" "${argv[@]}" \
    > "$OUT/$label.stdout" 2>&1
  local rc=$?
  echo "$rc" > "$OUT/$label.rc"
  # A run that prints no summary line is ambiguous: it may have failed, or it may
  # never have reached the summary. The exit code separates the two, and without
  # it the scroll case below read as "the panel reported nothing" when the process
  # had actually died at 20 s with rc=127 (see GATE-DESIGN-TRAPS 10).
  echo "  $label rc=$rc trace_lines=$(wc -l < "$OUT/$label.trace" 2>/dev/null | tr -d ' ')"
  if [ "$rc" -ne 0 ]; then
    echo "  $label trace tail:"; tail -3 "$OUT/$label.trace" 2>/dev/null | sed 's/^/    /'
  fi
  return 0
}

# --- 0/3 the guard's own test: both checks can go red ------------------------
# A guard that has never refused anything is a comment.
#   static  exactly one line in this file launches the product; a second launch
#           point would bypass the check entirely.
#   dynamic the predicate itself rejects an argv that lost a flag -- and the
#           helper uses the SAME predicate, so this tests what actually runs.
# Matched on the launch LINE's shape (`  TF2_NATIVE_TRACE=... timeout`), not on
# the bare command: the first version of this check counted its own grep line and
# reported two launch sites where there is one.
LAUNCH_SITES=$(grep -cE '^[[:space:]]+TF2_NATIVE_TRACE=.*timeout ' "$0")
if [ "$LAUNCH_SITES" -ne 1 ]; then
  echo "FATAL: $LAUNCH_SITES product launch sites in this gate (expected 1)."
  echo "       A launch the guard cannot see is not guarded; add it to run_product."
  exit 3
fi
safety_check --hidden-window --audio-disabled || {
  echo "FATAL: the safety predicate rejects an argv that has both flags"; exit 3; }
safety_check --hidden-window --capture-frame x >/dev/null 2>&1
missing_audio_rc=$?
[ "$missing_audio_rc" -eq 98 ] || {
  echo "FATAL: an argv missing --audio-disabled gave rc=$missing_audio_rc (want 98)"
  exit 3; }
safety_check --audio-disabled --capture-frame x >/dev/null 2>&1
missing_window_rc=$?
[ "$missing_window_rc" -eq 99 ] || {
  echo "FATAL: an argv missing --hidden-window gave rc=$missing_window_rc (want 99)"
  exit 3; }
echo "LAUNCH-SAFETY=OK (1 launch site; missing --audio-disabled -> 98, missing --hidden-window -> 99)"
echo "  (a truly flagless argv is refused by whichever case runs first, which is 99;
  the test above is 'one flag missing', not 'flagless' -- the difference is what a
  report would otherwise get to claim without the code backing it)"

# --- 1/3 the probe ------------------------------------------------------------
echo "=== 1/3 logstf_viewer_probe (URL policy, empty state, geometry, failure text) ==="
ok=0; bad=0
"$PROBE" > "$OUT/probe.txt" 2>&1
probe_rc=$?
cat "$OUT/probe.txt"
echo "probe_exit=$probe_rc"
assert_eq "probe exit" "$probe_rc" "0"

P_ASSERTIONS=(
  'urlAccepted=14|url: every accepted shape is accepted verbatim'
  'urlEmpty=3|url: an empty address is Empty/Missing, not a refusal'
  'urlRefused=26|url: every refusal names its own reason'
  'homographRefused=1|url: a non-ASCII homograph never reaches the host rule'
  'injectionRefused=1|url: a header-injection attempt is refused'
  'emptyState=empty|empty: an open panel with no URL is Empty'
  'emptyHostCalls=0|empty: zero host calls'
  'emptyNavigations=0|empty: zero navigations'
  'reopenState=empty|empty: a reopen is Empty again'
  'reopenLoadedState=content|content: a reopen of a loaded panel lands on Content'
  'reopenHostCalls=0|empty: a reopen still makes no host call'
  'refusalKeepsEmpty=1|empty: a refused import leaves the panel Empty'
  'refusalHasReason=1|empty: a refused import names its reason'
  'contentState=content|content: a valid URL with a live host lands on Content'
  'contentNavigations=1|content: exactly one navigation'
  'backCalls=1|content: back reached the host once'
  'forwardCalls=1|content: forward reached the host once'
  'reloadCalls=1|content: reload reached the host once'
  'reopenNavigations=2|content: a reopen restored exactly one navigation'
  'pendingKeepsLoading=1|content: a pending navigation keeps the panel in Loading'
  'pendingLandsOnContent=1|content: the panel reaches Content when the navigation completes'
  'unavailableState=unavailable|failure: no runtime reports Unavailable'
  'unavailableHasReason=1|failure: no runtime names the reason'
  'unavailableHasFallback=1|failure: no runtime names the fallback'
  'geometryOutside=0|geometry: nothing leaves the client at any size/dpi'
  'geometryOverlaps=0|geometry: no two toolbar controls overlap'
  'geometryNoContent=0|geometry: every panel size leaves a content rect'
  'geometryDemoSqueezed=0|geometry: the demo keeps its minimum width'
  'geometryUnreachable=0|geometry: every toolbar control stays reachable'
  'scrollOffset=480|scroll: the offset the page took is what is reported'
  'scrollScriptComplete=1|script: the scroll script is a complete expression, not truncated'
  'scrollScriptChars=174|script: the scroll script is the whole expression (174 chars)'
  'scriptValueQuoted=1|script: the JSON string a real browser returns decodes to its text'
  'scriptValueEmpty=1|script: an empty JSON string decodes to an empty answer'
  'scrollRawRecorded=1|scroll: an unparsable page answer is recorded, not folded into 0'
  'scrollFailuresWhenRefused=1|scroll: a refused script is distinguishable from a page that stayed put'
  'scrollOffsetWhenRefused=0|scroll: a refused script reports no offset'
  'historyUrlBefore=https://logs.tf/1|history: the address before the move is recorded'
  'historyUrlAfter=https://logs.tf/2|history: the address after the move is recorded'
  'scrollClamped=0|scroll: a page that cannot scroll reports 0, not the request'
  'scrollHeight=4200|scroll: the page height is read back'
  'viewportHeight=720|scroll: the viewport height is read back'
  'histogramSamples=8192|frames: the ring is bounded'
)

run_probe_expectations() { # run_probe_expectations <file> <needle|what>...
  local file="$1"; shift
  local e
  for e in "$@"; do
    local needle="${e%%|*}" what="${e#*|}"
    if [ -s "$file" ] && grep -qF -- "$needle" "$file"; then
      ok=$((ok + 1)); printf '  OK   %s\n' "$what"
    else
      bad=$((bad + 1)); printf '  FAIL %s (wanted: %s)\n' "$what" "$needle"
    fi
  done
}

run_probe_expectations "$OUT/probe.txt" "${P_ASSERTIONS[@]}"
echo "assertions_ok=$ok assertions_bad=$bad"
[ "$bad" -eq 0 ] || fail=1

# The runtime this host actually has, printed so a run on another machine is
# read rather than guessed. An unavailable runtime is not a probe failure: the
# probe's job is to report the two states, and the product half of this gate
# decides what an available one must then do.
probe_runtime="$(grep -oE 'runtimeKind=[^ ]* runtimeAvailable=[0-9]' "$OUT/probe.txt" | head -1)"
echo "host_runtime=$probe_runtime"
RUNTIME_AVAILABLE=0
case "$probe_runtime" in *"runtimeAvailable=1"*) RUNTIME_AVAILABLE=1 ;; esac
# The runtime is read, not pinned. Pinning runtimeKind=webview2 here would make
# this gate red on a host without the Evergreen runtime -- the very host whose
# "unavailable" answer the product half treats as a legitimate, explained
# outcome (2c). What IS asserted is that the answer is one of the two named
# states, so a blank or malformed runtime line cannot pass as either.
case "$probe_runtime" in
  "runtimeKind=webview2 runtimeAvailable=1"|"runtimeKind=none runtimeAvailable=0") : ;;
  *) echo "  FAIL the runtime line is neither available-webview2 nor absent: [$probe_runtime]"; fail=1 ;;
esac

# --- 2/3 the product ----------------------------------------------------------
echo
echo "=== 2/3 the product (hidden window, audio disabled, --logstf-script) ==="

# 2a. The empty panel. The product's own counters must show nothing fetched and
# nothing navigated, and its runtime line must name what it found.
run_product empty --logstf-script empty
assert_eq "empty run rc" "$(cat "$OUT/empty.rc")" "0"
empty_summary="$(grep -F 'logstf: summary' "$OUT/empty.trace" | tail -1)"
empty_runtime="$(grep -F 'logstf: runtime' "$OUT/empty.trace" | tail -1)"
echo "  empty.summary: $empty_summary"
echo "  empty.runtime: $empty_runtime"
[ -n "$empty_summary" ] || { echo "  FAIL the product emitted no logstf: summary line"; fail=1; }
assert_contains "product empty state" "$empty_summary" "state=empty"
assert_contains "product empty navigations" "$empty_summary" " navigations=0"
assert_contains "product empty host" "$empty_summary" "host=none"
assert_contains "product empty accepted" "$empty_summary" "accepted=0"
assert_contains "product empty refused" "$empty_summary" "refused=0"
assert_contains "product runtime line present" "$empty_runtime" "logstf: runtime kind="
# Format, not a pin: on a host without the Evergreen runtime this line says
# kind=none, and that is the state section 2c below handles as a legitimate
# outcome. Pinning webview2 here would make the gate red on exactly the host
# whose missing-runtime explanation the task asks the product to produce.
assert_contains "product runtime line names a kind" "$empty_runtime" "kind="
assert_contains "product loader status" "$empty_runtime" "loader="
#[no-lower]

# 2b. A malformed and a wrong-host URL. Both refused, both with a reason, and
# the panel stays empty -- a refusal is not a navigation to a search page.
run_product refuse --logstf-script import --logstf-url "https://evil.example/log/1"
refuse_summary="$(grep -F 'logstf: summary' "$OUT/refuse.trace" | tail -1)"
echo "  refuse.summary: $refuse_summary"
assert_contains "wrong host refused" "$refuse_summary" "refused=1"
assert_contains "wrong host never navigated" "$refuse_summary" " navigations=0"
# The token, not the sentence: refusal= is the text shown to the user, and asking
# prose to contain a token is how this assertion became unsatisfiable.
assert_contains "wrong host names its reason" "$refuse_summary" "refusalReason=host-not-allowed"

# 2b-2. A non-https address is refused with its own reason. The acceptance text
# asks for an explicit refusal here; silently upgrading http to https would be a
# decision the user did not make, and would leave "did you open what I typed?"
# unanswerable.
run_product nothttps --logstf-script import --logstf-url "http://logs.tf/3"
nothttps_summary="$(grep -F 'logstf: summary' "$OUT/nothttps.trace" | tail -1)"
echo "  nothttps.summary: $nothttps_summary"
assert_contains "http URL refused" "$nothttps_summary" "refused=1"
assert_contains "http URL names its reason" "$nothttps_summary" "refusalReason=not-https"
assert_contains "http URL never navigated" "$nothttps_summary" " navigations=0"

run_product malformed --logstf-script import --logstf-url "javascript:alert(1)"
malformed_summary="$(grep -F 'logstf: summary' "$OUT/malformed.trace" | tail -1)"
echo "  malformed.summary: $malformed_summary"
assert_contains "malformed URL refused" "$malformed_summary" "refused=1"
# Same trap as 2b: refusal= is the sentence, refusalReason= is the token (GATE-DESIGN-TRAPS 9).
assert_contains "malformed URL names its reason" "$malformed_summary" "refusalReason=unsupported-scheme"
assert_contains "malformed URL never navigated" "$malformed_summary" " navigations=0"

# 2c. A valid URL. Two honest outcomes: content when a runtime exists, or
# Unavailable with a reason AND a fallback when it does not. The second is a
# PASS for the panel and a NOT VERIFIED for "a page is displayed", which this
# section prints as its own line rather than hiding.
run_product import --logstf-script import --logstf-url "https://logs.tf/1"
assert_eq "import run rc" "$(cat "$OUT/import.rc")" "0"
import_summary="$(grep -F 'logstf: summary' "$OUT/import.trace" | tail -1)"
import_runtime="$(grep -F 'logstf: runtime' "$OUT/import.trace" | tail -1)"
echo "  import.summary: $import_summary"
echo "  import.runtime: $import_runtime"
import_state="$(pvalue "$import_summary" state)"
assert_contains "valid URL accepted" "$import_summary" "accepted=1"
assert_contains "valid URL address kept" "$import_summary" "url=https://logs.tf/1"
PANEL_CONTENT=0
case "$import_state" in
  content)
    PANEL_CONTENT=1
    assert_contains "valid URL navigated once" "$import_summary" " navigations=1"
    assert_contains "valid URL host alive" "$import_summary" "host=alive"
    assert_contains "valid URL panel has width" "$import_summary" "panel="
    ;;
  unavailable)
    assert_nonempty "unavailable reason named" "$(pvalue "$import_summary" unavailable)"
    assert_contains "unavailable keeps the address" "$import_summary" "url=https://logs.tf/1"
    assert_contains "unavailable never navigated" "$import_summary" " navigations=0"
    echo "  NOT-VERIFIED page-displayed: the host has no usable web-view runtime"
    echo "               (owner: t54-logstf; cheapest next step: run this gate on a host with"
    echo "               the Evergreen runtime, or install it and re-run --logstf-script import)"
    ;;
  *)
    echo "  FAIL the panel reached an unexpected state: $import_state"; fail=1 ;;
esac
echo "panel_content=$PANEL_CONTENT"

if [ "$PANEL_CONTENT" -eq 1 ]; then
  # 2d. Scroll, resize and history -- the browser-like behaviours, driven
  # through the same entry points the controls use.
  run_product scroll --logstf-script scroll --logstf-url "https://logs.tf/1"
  scroll_summary="$(grep -F 'logstf: summary' "$OUT/scroll.trace" | tail -1)"
  echo "  scroll.summary: $scroll_summary"
  # ONE scroll, not two: the scroll script calls scrollContent once and then
  # pageExtent, which counts scriptCalls rather than scrollCalls. The first version
  # of this line asked for 2, so a working scroll step still read as a failure --
  # an expectation about the script that the script never promised. The offset the
  # page took is asserted by the probe (scrollOffset=480), where the host is a
  # recording fake; what the product run proves is that the real runtime accepted
  # the script and reported no failure.
  assert_contains "scroll reached the page" "$scroll_summary" "scrollCalls=1"
  # The four verdicts below are the ones the negative controls re-use. See
  # scroll_verdicts: "the offset was read", "the answer was a number", "the page
  # moved" and "the script did not fail" are four different claims, and only the
  # first one can go red when the script never runs.
  scroll_verdicts "$scroll_summary"
  echo "  scroll.verdicts: first_bad=$SCROLL_FIRST_BAD"
  run_product resize --logstf-script resize --logstf-url "https://logs.tf/1"
  resize_summary="$(grep -F 'logstf: summary' "$OUT/resize.trace" | tail -1)"
  echo "  resize.summary: $resize_summary"
  resize_width="$(pvalue "$resize_summary" panel)"
  resize_client="$(pvalue "$resize_summary" client)"      # "W,H"
  resize_scale="$(pvalue "$resize_summary" dpiScale)"
  # The scripted run asks for 1 then 100000; the panel must report the clamped
  # maximum, which is the client width minus the demo's minimum -- not the
  # request and not the minimum. Derived from the run's own client rect and dpi
  # so the assertion is arithmetic on the reading, not a pin on this host's
  # window frame.
  resize_expected="$(awk -F, -v c="${resize_client%%,*}" -v s="${resize_scale:-1}" 'BEGIN{ printf "%d", c - int(320*s + 0.5) }')"
  assert_eq "resize clamps the request to the edge" "$resize_width" "$resize_expected"
  assert_contains "resize stays in content" "$resize_summary" "state=content"

  run_product history --logstf-script history --logstf-url "https://logs.tf/1" --logstf-url2 "https://logs.tf/2"
  history_summary="$(grep -F 'logstf: summary' "$OUT/history.trace" | tail -1)"
  echo "  history.summary: $history_summary"
  assert_contains "history navigated twice" "$history_summary" " navigations=2"
  # canBack at the END of the script is the meaningful direction: the panel has
  # gone back, forward and reloaded, so it ends at the newest entry and canForward
  # is legitimately 0 there. Demanding canForward=1 at that moment asked the panel
  # to have a future entry after returning to the present -- an assertion about a
  # state the script never visits. Forward's success is proven by
  # scriptFailures=0 (the script fails the step on a refused back/forward/reload)
  # and by navigations=2.
  assert_contains "history has an entry behind it" "$history_summary" "canBack=1"
  assert_contains "history back/forward/reload all succeeded" "$history_summary" "scriptFailures=0"
  assert_contains "history ended on the second URL" "$history_summary" "url=https://logs.tf/2"
  # canBack is a FLAG, and it was already true as soon as the second page
  # loaded. What proves the browser moved is the address on both sides of the
  # settled move: historyMoved=1 means the two differ. Both URLs are printed so
  # a reader can see which pages they were.
  assert_contains "history moved the page (address changed across the move)" \
    "$history_summary" "historyMoved=1"
  echo "  history: $(pvalue "$history_summary" urlBeforeHistory) -> $(pvalue "$history_summary" urlAfterHistory)"

  run_product reopen --logstf-script reopen --logstf-url "https://logs.tf/1"
  reopen_summary="$(grep -F 'logstf: summary' "$OUT/reopen.trace" | tail -1)"
  echo "  reopen.summary: $reopen_summary"
  assert_contains "reopen returns to content" "$reopen_summary" "state=content"
  assert_contains "reopen navigated once more" "$reopen_summary" " navigations=2"
  assert_contains "reopen counted" "$reopen_summary" "closes=1"

  run_product close --logstf-script close --logstf-url "https://logs.tf/1"
  close_summary="$(grep -F 'logstf: summary' "$OUT/close.trace" | tail -1)"
  echo "  close.summary: $close_summary"
  assert_contains "close returns to Closed" "$close_summary" "state=closed"
  assert_contains "close drops the host" "$close_summary" "host=none"
  assert_contains "close has no geometry" "$close_summary" "panel=0"
else
  echo "  SKIP scroll/resize/history/reopen/close: no usable runtime on this host"
  echo "       (reported as NOT VERIFIED above, not as a pass)"
fi

# 2e. Playback responsiveness. Two runs of the same demo and the same tick:
# one with the panel closed (the baseline) and one with it open. Both runs must
# advance the demo, and the open run's p95 frame interval is compared against
# the closed run's. The comparison is printed either way, and the tolerance is
# a named number rather than "looks fine".
run_product frames_closed --logstf-measure
run_product frames_open --logstf-measure --logstf-script import --logstf-url "https://logs.tf/1"
closed_line="$(grep -F 'logstf: frames' "$OUT/frames_closed.trace" | tail -1)"
open_line="$(grep -F 'logstf: frames' "$OUT/frames_open.trace" | tail -1)"
echo "  frames.closed: $closed_line"
echo "  frames.open:   $open_line"
p95_of() { printf '%s\n' "$1" | grep -oE "window=$2 [^|]*" | grep -oE 'p95=[0-9.]+' | head -1 | cut -d= -f2; }
frames_of() { printf '%s\n' "$1" | grep -oE "window=$2 [^|]*" | grep -oE 'frames=[0-9]+' | head -1 | cut -d= -f2; }
closed_p95="$(p95_of "$closed_line" closed)"
open_p95="$(p95_of "$open_line" open)"
closed_n="$(frames_of "$closed_line" closed)"
open_n="$(frames_of "$open_line" open)"
echo "  p95_closed=$closed_p95 p95_open=$open_p95 frames_closed=$closed_n frames_open=$open_n"
assert_ge "baseline frame samples" "$closed_n" 30
assert_ge "panel-open frame samples" "$open_n" 30

# The demo must have advanced in the panel-open run: responsiveness means the
# player still plays, which is a tick reading and not a frame count.
# Anchored on the line the product writes WHEN IT ARMS THE CAPTURE, because that
# line names the tick the frame belongs to. The old form took the last tick= in
# the whole trace, which is whichever line happens to be last -- a reading whose
# object was not the capture at all.
capture_line="$(grep -F 'capture: armed' "$OUT/frames_open.trace" | tail -1)"
echo "  capture.open: $capture_line"
open_ticks="$(printf '%s\n' "$capture_line" | grep -oE 'frame_tick=[0-9]+' | head -1 | cut -d= -f2)"
assert_ge "panel-open run reached the capture tick" "$open_ticks" "$CAPTURE_TICK"

if [ -n "$closed_p95" ] && [ -n "$open_p95" ]; then
  # 2.0x is deliberately loose: this is a shared CI host, and the claim being
  # tested is "opening the panel does not collapse the frame rate", not "the
  # cost is below 3 ms". A ratio beyond 2x on a browser-backed panel is a real
  # regression; a 20% difference on this host is noise.
  ratio_ok="$(awk -v a="$closed_p95" -v b="$open_p95" 'BEGIN{ if (a <= 0) { print "0"; exit } printf "%d", (b <= a * 2.0) ? 1 : 0 }')"
  assert_eq "panel-open p95 stays within 2x of the closed baseline" "$ratio_ok" "1"
  echo "  p95_ratio=$(awk -v a="$closed_p95" -v b="$open_p95" 'BEGIN{ if (a<=0) print "n/a"; else printf "%.2f", b/a }')"
else
  echo "  FAIL the frame windows were not both reported"; fail=1
fi

# --- 3/3 mutation -------------------------------------------------------------
if [ "$MUTATION" -eq 1 ]; then
  echo
  echo "=== 3/3 mutation: perturbed captures, then four real product defects ==="
  if [ "$fail" -ne 0 ]; then
    # A mutation scored against a baseline that is already red proves nothing: the
    # "caught" verdict can come from the failure that was there before the
    # mutation. Refuse, and say which state the gate is in.
    echo "MUTATION-SUITE=REFUSED (the forward gate is red; a mutation scored"
    echo "  against a red baseline proves nothing -- fix the forward run first)"
    exit 1
  fi
  mrc=0

  mutate_capture() { # mutate_capture <name> <file> <sed>
    local name="$1" file="$2" expr="$3"
    local copy="$OUT/mutated-$name.txt"
    cp "$file" "$copy"
    sed -i "$expr" "$copy"
    if cmp -s "$file" "$copy"; then
      echo "  MUTATION-SETUP=FAIL ($name: the perturbation changed nothing)"; mrc=1; return
    fi
    local saved_ok=$ok saved_bad=$bad
    ok=0; bad=0
    run_probe_expectations "$copy" "${P_ASSERTIONS[@]}" > "$OUT/mutated-$name.log" 2>&1
    local mok=$ok mbad=$bad
    ok=$saved_ok; bad=$saved_bad
    if [ "$mbad" -ge 1 ]; then
      printf '  MUTATION-CAUGHT=PASS (%s: %s of %s assertions went red)\n' "$name" "$mbad" "$((mok + mbad))"
    else
      printf '  MUTATION-CAUGHT=FAIL (%s: no assertion moved)\n' "$name"; mrc=1
    fi
  }

  # The mutation sets a value that cannot be right rather than "the number it
  # should have moved to": anchoring on 25 meant this mutation silently stopped
  # mutating the day the refusal table grew, and the suite then reported a
  # MUTATION-CAUGHT=FAIL that was about a stale literal, not about the gate.
  mutate_capture url_host       "$OUT/probe.txt" 's/urlRefused=[0-9]*/urlRefused=999/'
  mutate_capture empty_calls    "$OUT/probe.txt" 's/emptyHostCalls=0/emptyHostCalls=1/'
  mutate_capture empty_navigate "$OUT/probe.txt" 's/emptyNavigations=0/emptyNavigations=1/'
  mutate_capture unavailable    "$OUT/probe.txt" 's/unavailableHasReason=1/unavailableHasReason=0/'
  mutate_capture geometry       "$OUT/probe.txt" 's/geometryOverlaps=0/geometryOverlaps=3/'
  mutate_capture scroll         "$OUT/probe.txt" 's/scrollOffset=480/scrollOffset=0/'
  # The two readings the lead's question produced. A probe that reports the
  # scroll as a call with no offset, and one whose script seam refused, must not
  # read the same.
  mutate_capture scroll_raw     "$OUT/probe.txt" 's/scrollRawRecorded=1/scrollRawRecorded=0/'
  # The history reading is the address itself; collapsing it to the "no address"
  # sentinel is what a silently-skipped history step would look like.
  mutate_capture history_url    "$OUT/probe.txt" 's/historyUrlAfter=https:\/\/logs.tf\/2/historyUrlAfter=-/'

  # Product mutation 1: the host allow-list accepts anything. The probe (which
  # links the same policy) must go red on the accepted/refused cases, and the
  # product's own wrong-host run must start navigating.
  backup="$OUT/logstf_viewer.cpp.backup"
  backup_webview="$OUT/logstf_webview.cpp.backup"
  cp native/src/logstf_webview.cpp "$backup_webview"
  cp native/src/logstf_viewer.cpp "$backup"
  # '#' as the delimiter, because in a GNU BRE \| is the ALTERNATION operator, not
  # a literal "||": the '|'-delimited form matched one half of the condition and
  # produced "return true;|| lowered == ...", which is a syntax error, so the
  # mutation would have been scored by the BUILD rather than by the assertion.
  sed -i 's#return lowered == "logs.tf" || lowered == "www.logs.tf";#return true;#' native/src/logstf_viewer.cpp
  if cmp -s native/src/logstf_viewer.cpp "$backup"; then
    echo "  MUTATION-SETUP=FAIL (m1_wrong_host: the sed changed nothing)"; mrc=1
  else
    if bash build-target.sh tf2_demo_native logstf_viewer_probe > "$OUT/m1.build" 2>&1; then
      echo "  mutated build rc=0 (m1)"
    else
      echo "  MUTATION-BUILD=FAIL (m1): the mutation did not compile, so it scores"
      echo "  nothing -- a build failure is an unverified mutation, not a caught one."; mrc=1
    fi
    "$PROBE" > "$OUT/m1_probe.txt" 2>&1
    m1_probe_rc=$?
    run_product m1 --logstf-script import --logstf-url "https://evil.example/log/1"
    m1_summary="$(grep -F 'logstf: summary' "$OUT/m1.trace" | tail -1)"
    if ! grep -qE 'urlRefused=[0-9]+' "$OUT/m1_probe.txt"; then
      echo "  MUTATION-UNREADABLE=FAIL (m1: the mutated probe printed no urlRefused reading)"; mrc=1
    elif [ "$m1_probe_rc" -ne 0 ] && ! grep -qE 'urlRefused=26\b' "$OUT/m1_probe.txt" \
        && case "$m1_summary" in *"refused=1"*) false ;; *) true ;; esac; then
      echo "  MUTATION-CAUGHT=PASS (m1_wrong_host: probe and product both went red)"
    else
      echo "  MUTATION-CAUGHT=FAIL (m1_wrong_host: the gate stayed green)"; mrc=1
    fi
  fi
  cp "$backup" native/src/logstf_viewer.cpp
  cmp -s native/src/logstf_viewer.cpp "$backup" || { echo "  RESTORE=FAIL (m1)"; mrc=1; }

  # Product mutation 2: the empty panel prefetches a default page.
  cp native/src/logstf_viewer.cpp "$backup"
  python_patch_empty() {
    "$PY" - "$1" <<'PYEOF'
import sys
path = sys.argv[1]
# UNIVERSAL NEWLINES, and a fixed newline on write. Every needle below is written
# with LF because that is what this repository's source looks like in git. The
# CHECKOUT is another matter: core.autocrlf=true on this host, so the integration
# tree's working files are CRLF while a fresh branch's are LF. Reading with
# newline="" preserved the CRLF, so every multi-line needle failed to match -- and
# on 2026-10-23 that silenced four mutations in the integration tree (m2, m5, m6,
# m9 all printed "needle not found"), which the suite reported as SETUP=FAIL. A
# mutation that cannot be applied is not a caught mutation, but a suite that
# cannot be applied on one of the two trees it runs in is a suite with a hole.
#
# The write below uses newline="\n", so a patched CRLF file comes back as LF for
# its whole length. That is deliberate and harmless HERE: every mutation is a
# throwaway edit that is restored by byte copy and verified with cmp two lines
# later, and the compiler does not care. Preserving the original EOL would make
# the mutation diff smaller, but it would also mean the patch code had to carry
# the file's line-ending style -- one more thing to get wrong in the one part of
# this script whose job is to be obviously correct.
text = open(path, "r", encoding="utf-8").read()  # universal newlines: CRLF -> LF
needle = """  if (url_.empty()) {
    state_ = LogsPanelState::Empty;
    counters_.state = state_;
    return true;
  }"""
replacement = """  if (url_.empty()) {
    url_ = "https://logs.tf/";
    counters_.url = url_;
    if (runtime_.available && host_ != nullptr) {
      pendingNavigate_ = true;
      state_ = LogsPanelState::Loading;
      counters_.state = state_;
      return true;
    }
    state_ = LogsPanelState::Empty;
    counters_.state = state_;
    return true;
  }"""
if needle not in text:
    sys.stderr.write("needle not found\n")
    sys.exit(3)
open(path, "w", encoding="utf-8", newline="\n").write(text.replace(needle, replacement, 1))
PYEOF
  }
  if python_patch_empty native/src/logstf_viewer.cpp && ! cmp -s native/src/logstf_viewer.cpp "$backup"; then
    if bash build-target.sh tf2_demo_native logstf_viewer_probe > "$OUT/m2.build" 2>&1; then
      echo "  mutated build rc=0 (m2)"
    else
      echo "  MUTATION-BUILD=FAIL (m2): the mutation did not compile, so it scores"
      echo "  nothing -- a build failure is an unverified mutation, not a caught one."; mrc=1
    fi
    "$PROBE" > "$OUT/m2_probe.txt" 2>&1
    m2_probe_rc=$?
    run_product m2 --logstf-script empty
    m2_summary="$(grep -F 'logstf: summary' "$OUT/m2.trace" | tail -1)"
    if [ "$m2_probe_rc" -ne 0 ] && ! grep -qF 'emptyHostCalls=0' "$OUT/m2_probe.txt" \
        && case "$m2_summary" in *"state=empty"*" navigations=0"*) false ;; *) true ;; esac; then
      echo "  MUTATION-CAUGHT=PASS (m2_empty_prefetch: probe and product both went red)"
    else
      echo "  MUTATION-CAUGHT=FAIL (m2_empty_prefetch: the gate stayed green)"; mrc=1
    fi
  else
    echo "  MUTATION-SETUP=FAIL (m2_empty_prefetch: the patch changed nothing)"; mrc=1
  fi
  cp "$backup" native/src/logstf_viewer.cpp
  cmp -s native/src/logstf_viewer.cpp "$backup" || { echo "  RESTORE=FAIL (m2)"; mrc=1; }

  # Product mutation 3: the unavailable state goes silent -- exactly the
  # "blank panel, no explanation" the task forbids. Applied at BOTH entry points
  # (open() and importUrl()), because one defect that is reachable two ways is
  # still one defect; the patch prints how many sites it changed so the reader
  # knows this mutation is two edits. The sed it replaces substituted a
  # conditional whose branches were std::string and bool, which cannot compile:
  # the mutation would have been scored by the compiler, not by the assertion.
  cp native/src/logstf_viewer.cpp "$backup"
  python_patch_silent() {
    "$PY" - "$1" <<'PYEOF'
import sys
path = sys.argv[1]
text = open(path, "r", encoding="utf-8").read()  # universal newlines: CRLF -> LF
needle = """    unavailable_ = runtime_.reason.empty()
        ? "no web-view runtime is available to this build"
        : runtime_.reason;"""
replacement = "    unavailable_ = std::string();  // MUTATION: the reason is dropped"
count = text.count(needle)
if count != 2:
    sys.stderr.write("needle count %d, expected 2\n" % count)
    sys.exit(3)
open(path, "w", encoding="utf-8", newline="\n").write(text.replace(needle, replacement))
print("patched %d sites" % count)
PYEOF
  }
  if python_patch_silent native/src/logstf_viewer.cpp && ! cmp -s native/src/logstf_viewer.cpp "$backup"; then
    if bash build-target.sh logstf_viewer_probe > "$OUT/m3.build" 2>&1; then
      echo "  mutated build rc=0 (m3)"
    else
      echo "  MUTATION-BUILD=FAIL (m3): the mutation did not compile, so it scores"
      echo "  nothing -- a build failure is an unverified mutation, not a caught one."; mrc=1
    fi
    "$PROBE" > "$OUT/m3_probe.txt" 2>&1
    m3_probe_rc=$?
    if [ "$m3_probe_rc" -ne 0 ] && ! grep -qF 'unavailableHasReason=1' "$OUT/m3_probe.txt"; then
      echo "  MUTATION-CAUGHT=PASS (m3_silent_runtime: the reason assertion went red)"
    else
      echo "  MUTATION-CAUGHT=FAIL (m3_silent_runtime: the gate stayed green)"; mrc=1
    fi
  else
    # WITHOUT THIS, a failed setup for m3 printed NOTHING: the block simply fell
    # through to the restore, and the run's only trace of the mutation was its
    # absence from the verdict list. On 2026-10-23 that is exactly what happened
    # in the integration tree -- m3 has no CAPTURED and no SETUP line at all,
    # which a reader scanning for "FAIL" would never see.
    echo "  MUTATION-SETUP=FAIL (m3_silent_runtime: the patch changed nothing)"; mrc=1
  fi
  cp "$backup" native/src/logstf_viewer.cpp
  cmp -s native/src/logstf_viewer.cpp "$backup" || { echo "  RESTORE=FAIL (m3)"; mrc=1; }

  # Product mutation 4: the resize stops clamping, so a drag can push the panel
  # off the client. The geometry matrix must go red.
  cp native/src/logstf_viewer.cpp "$backup"
  sed -i 's|  if (requested > maxWidth) return maxWidth;|  if (requested > maxWidth) return requested;|' native/src/logstf_viewer.cpp
  if cmp -s native/src/logstf_viewer.cpp "$backup"; then
    echo "  MUTATION-SETUP=FAIL (m4_resize_unclamped: the sed changed nothing)"; mrc=1
  else
    if bash build-target.sh logstf_viewer_probe > "$OUT/m4.build" 2>&1; then
      echo "  mutated build rc=0 (m4)"
    else
      echo "  MUTATION-BUILD=FAIL (m4): the mutation did not compile, so it scores"
      echo "  nothing -- a build failure is an unverified mutation, not a caught one."; mrc=1
    fi
    "$PROBE" > "$OUT/m4_probe.txt" 2>&1
    m4_probe_rc=$?
    # "the reading changed" and "the reading is not there" are different findings;
    # only the first one scores a mutation. (This is GATE-DESIGN-TRAPS 1/6.)
    if ! grep -qE 'geometryOutside=[0-9]+' "$OUT/m4_probe.txt"; then
      echo "  MUTATION-UNREADABLE=FAIL (m4: the mutated probe printed no geometryOutside reading)"; mrc=1
    elif [ "$m4_probe_rc" -ne 0 ] && grep -qE 'geometryOutside=[1-9]' "$OUT/m4_probe.txt"; then
      echo "  MUTATION-CAUGHT=PASS (m4_resize_unclamped: the geometry matrix went red)"
    else
      echo "  MUTATION-CAUGHT=FAIL (m4_resize_unclamped: the gate stayed green)"; mrc=1
    fi
  fi
  cp "$backup" native/src/logstf_viewer.cpp
  cmp -s native/src/logstf_viewer.cpp "$backup" || { echo "  RESTORE=FAIL (m4)"; mrc=1; }

  # Probe mutation 5: the asynchronous-host seam is removed. The controller stops
  # waiting for a pending navigation and enters Content the moment Navigate returns
  # S_OK -- exactly the defect the first real history run exposed (CanGoBack=0 and
  # "back was refused"). Only the probe's controllable pendingNavigation flag can
  # see it, which is why that case exists: without this mutation the branch would
  # be a line no test can turn red, and the Lead's rule is that such a branch is
  # indistinguishable from an absent one.
  cp native/src/logstf_viewer.cpp "$backup"
  python_patch_pending() {
    "$PY" - "$1" <<'PYEOF'
import sys
path = sys.argv[1]
text = open(path, "r", encoding="utf-8").read()  # universal newlines: CRLF -> LF
needle = "  if (host_->navigationPending()) return;\n"
if needle not in text:
    sys.stderr.write("needle not found\n")
    sys.exit(3)
open(path, "w", encoding="utf-8", newline="\n").write(text.replace(needle, "", 1))
PYEOF
  }
  if python_patch_pending native/src/logstf_viewer.cpp && ! cmp -s native/src/logstf_viewer.cpp "$backup"; then
    if bash build-target.sh logstf_viewer_probe > "$OUT/m5.build" 2>&1; then
      echo "  mutated build rc=0 (m5)"
    else
      echo "  MUTATION-BUILD=FAIL (m5): the mutation did not compile, so it scores"
      echo "  nothing -- a build failure is an unverified mutation, not a caught one."; mrc=1
    fi
    "$PROBE" > "$OUT/m5_probe.txt" 2>&1
    if ! grep -qE 'pendingKeepsLoading=[0-9]+' "$OUT/m5_probe.txt"; then
      echo "  MUTATION-UNREADABLE=FAIL (m5: the mutated probe printed no pendingKeepsLoading reading)"; mrc=1
    elif grep -qF 'pendingKeepsLoading=0' "$OUT/m5_probe.txt"; then
      echo "  MUTATION-CAUGHT=PASS (m5_pending_navigation: the async-host case went red)"
    else
      echo "  MUTATION-CAUGHT=FAIL (m5_pending_navigation: the probe stayed green)"; mrc=1
    fi
  else
    echo "  MUTATION-SETUP=FAIL (m5_pending_navigation: the patch changed nothing)"; mrc=1
  fi
  cp "$backup" native/src/logstf_viewer.cpp
  cmp -s native/src/logstf_viewer.cpp "$backup" || { echo "  RESTORE=FAIL (m5)"; mrc=1; }

  # Product mutation 6: THE VACUITY MUTATION. The script call is counted but
  # never issued -- scrollCalls=1 and scriptFailures=0 both stay green, exactly
  # as they did before this round, while the page never moves. If the gate cannot
  # turn this red then its scroll assertions measure a call site, not a scroll.
  cp native/src/logstf_viewer.cpp "$backup"
  m6_patch() {
    "$PY" - "$1" <<'PYEOF'
import sys
path = sys.argv[1]
text = open(path, "r", encoding="utf-8").read()  # universal newlines: CRLF -> LF
# The scrollContent call site, identified by BOTH its neighbours: the bare
# "runScript" line also appears in pageExtent, and replace(count=1) would then
# silently mutate whichever function happens to come first in the file.
needle = """  if (!host_->runScript(script, &result, &error)) {
    ++counters_.scriptFailures;
    return false;
  }
  const int parsed = std::atoi(result.c_str());"""
replacement = """  if (false) {
    ++counters_.scriptFailures;
    return false;
  }
  const int parsed = std::atoi(result.c_str());"""
if needle not in text:
    sys.stderr.write("needle not found\n")
    sys.exit(3)
open(path, "w", encoding="utf-8", newline="\n").write(text.replace(needle, replacement, 1))
PYEOF
  }
  if m6_patch native/src/logstf_viewer.cpp && ! cmp -s native/src/logstf_viewer.cpp "$backup"; then
    if bash build-target.sh tf2_demo_native logstf_viewer_probe > "$OUT/m6.build" 2>&1; then
      echo "  mutated build rc=0 (m6)"
    else
      echo "  MUTATION-BUILD=FAIL (m6): the mutation did not compile, so it scores"
      echo "  nothing -- a build failure is an unverified mutation, not a caught one."; mrc=1
    fi
    run_product m6 --logstf-script scroll --logstf-url "https://logs.tf/1"
    m6_summary="$(grep -F 'logstf: summary' "$OUT/m6.trace" | tail -1)"
    echo "  m6 summary: $m6_summary"
    # The SAME verdicts the forward run uses, so the control measures this gate
    # and not a parallel opinion of it. mrc=1 here is the SUCCESS condition of a
    # negative control, so `fail` is restored afterwards.
    saved_fail=$fail
    scroll_verdicts "$m6_summary" > "$OUT/m6.verdicts" 2>&1
    m6_first_bad="$SCROLL_FIRST_BAD"
    fail=$saved_fail
    sed 's/^/    /' "$OUT/m6.verdicts"
    m6_calls="$(pvalue "$m6_summary" scrollCalls)"
    m6_fail="$(pvalue "$m6_summary" scriptFailures)"
    echo "  m6 readings: scrollCalls=$m6_calls scriptFailures=$m6_fail first_bad=$m6_first_bad"
    # The point of this control is that the OLD readings do not move. If they
    # moved too, the control would not show which reading did the work.
    if [ "$m6_calls" != "1" ] || [ "$m6_fail" != "0" ]; then
      echo "  MUTATION-UNREADABLE=FAIL (m6: the call counters moved too, so this"
      echo "    control does not isolate the page reading: calls=$m6_calls fail=$m6_fail)"; mrc=1
    elif [ "$m6_first_bad" = "scroll_offset_was_read" ]; then
      echo "  MUTATION-CAUGHT=PASS (m6_script_never_runs: scrollCalls=1 and"
      echo "    scriptFailures=0 stayed green; the first red assertion is"
      echo "    scroll_offset_was_read -- exactly the one that names it)"
    else
      echo "  MUTATION-CAUGHT=FAIL (m6_script_never_runs: first red was"
      echo "    ${m6_first_bad:-nothing}, wanted scroll_offset_was_read)"; mrc=1
    fi
  else
    echo "  MUTATION-SETUP=FAIL (m6_script_never_runs: the patch changed nothing)"; mrc=1
  fi
  cp "$backup" native/src/logstf_viewer.cpp
  cmp -s native/src/logstf_viewer.cpp "$backup" || { echo "  RESTORE=FAIL (m6)"; mrc=1; }

  # Product mutation 7: history is a FLAG, not a move. recordHistoryPair is fed
  # the same address on both sides, which is exactly what a browser that reports
  # CanGoBack=1 without committing a navigation looks like. canBack=1 stays
  # green; only historyMoved / the address pair go red.
  cp native/src/logstf_viewer.cpp "$backup"
  m7_patch() {
    "$PY" - "$1" <<'PYEOF'
import sys
path = sys.argv[1]
text = open(path, "r", encoding="utf-8").read()  # universal newlines: CRLF -> LF
needle = """  counters_.urlAfterHistory = after;"""
replacement = """  counters_.urlAfterHistory = before;"""
if needle not in text:
    sys.stderr.write("needle not found\n")
    sys.exit(3)
open(path, "w", encoding="utf-8", newline="\n").write(text.replace(needle, replacement, 1))
PYEOF
  }
  if m7_patch native/src/logstf_viewer.cpp && ! cmp -s native/src/logstf_viewer.cpp "$backup"; then
    if bash build-target.sh tf2_demo_native logstf_viewer_probe > "$OUT/m7.build" 2>&1; then
      echo "  mutated build rc=0 (m7)"
    else
      echo "  MUTATION-BUILD=FAIL (m7): the mutation did not compile, so it scores"
      echo "  nothing -- a build failure is an unverified mutation, not a caught one."; mrc=1
    fi
    run_product m7 --logstf-script history --logstf-url "https://logs.tf/1" --logstf-url2 "https://logs.tf/2"
    m7_summary="$(grep -F 'logstf: summary' "$OUT/m7.trace" | tail -1)"
    m7_moved="$(pvalue "$m7_summary" historyMoved)"
    m7_can="$(pvalue "$m7_summary" canBack)"
    echo "  m7 readings: canBack=$m7_can historyMoved=$m7_moved"
    if [ "$m7_can" != "1" ]; then
      echo "  MUTATION-UNREADABLE=FAIL (m7: canBack moved too, so this mutation"
      echo "    does not isolate the address reading: canBack=$m7_can)"; mrc=1
    elif [ "$m7_moved" = "0" ]; then
      echo "  MUTATION-CAUGHT=PASS (m7_history_flag_only: canBack=1 stayed green, the"
      echo "    address pair went red -- historyMoved=0)"
    else
      echo "  MUTATION-CAUGHT=FAIL (m7_history_flag_only: the address reading stayed green)"; mrc=1
    fi
  else
    echo "  MUTATION-SETUP=FAIL (m7_history_flag_only: the patch changed nothing)"; mrc=1
  fi
  cp "$backup" native/src/logstf_viewer.cpp
  cmp -s native/src/logstf_viewer.cpp "$backup" || { echo "  RESTORE=FAIL (m7)"; mrc=1; }

  # Product mutation 8: THE DEFECT THIS ROUND FOUND. Stop decoding the browser
  # JSON, so String(480) parses as 0. Every call counter stays green: the script
  # runs, succeeds, and reports an offset of 0 on a page that does scroll. This is
  # the failure mode that made the original scroll assertion decorative -- and it
  # is invisible to any test whose fake host answers with an unquoted string,
  # which is every test in this tree before the decoder existed.
  cp native/src/logstf_webview.cpp "$backup_webview"
  m8_patch() {
    "$PY" - "$1" <<'PYEOF'
import sys
path = sys.argv[1]
text = open(path, "r", encoding="utf-8").read()  # universal newlines: CRLF -> LF
needle = "else value = logsScriptValue(narrow(jsonResult));"
replacement = "else value = narrow(jsonResult);"
if needle not in text:
    sys.stderr.write("needle not found\n")
    sys.exit(3)
open(path, "w", encoding="utf-8", newline="\n").write(text.replace(needle, replacement, 1))
PYEOF
  }
  if m8_patch native/src/logstf_webview.cpp && ! cmp -s native/src/logstf_webview.cpp "$backup_webview"; then
    if bash build-target.sh tf2_demo_native > "$OUT/m8.build" 2>&1; then
      echo "  mutated build rc=0 (m8)"
    else
      echo "  MUTATION-BUILD=FAIL (m8): the mutation did not compile, so it scores"
      echo "  nothing -- a build failure is an unverified mutation, not a caught one."; mrc=1
    fi
    run_product m8 --logstf-script scroll --logstf-url "https://logs.tf/1"
    m8_summary="$(grep -F 'logstf: summary' "$OUT/m8.trace" | tail -1)"
    m8_calls="$(pvalue "$m8_summary" scrollCalls)"
    m8_fail="$(pvalue "$m8_summary" scriptFailures)"
    m8_raw="$(pvalue "$m8_summary" scrollRaw)"
    m8_off="$(pvalue "$m8_summary" scrollOffset)"
    echo "  m8 summary: $m8_summary"
    saved_fail=$fail
    scroll_verdicts "$m8_summary" > "$OUT/m8.verdicts" 2>&1
    m8_first_bad="$SCROLL_FIRST_BAD"
    fail=$saved_fail
    sed 's/^/    /' "$OUT/m8.verdicts"
    echo "  m8 readings: scrollCalls=$m8_calls scriptFailures=$m8_fail scrollRaw=$m8_raw scrollOffset=$m8_off first_bad=$m8_first_bad"
    if [ "$m8_calls" != "1" ] || [ "$m8_fail" != "0" ]; then
      echo "  MUTATION-UNREADABLE=FAIL (m8: the call counters moved too, so this"
      echo "    mutation does not isolate the decode: calls=$m8_calls fail=$m8_fail)"; mrc=1
    elif [ "$m8_first_bad" = "scroll_raw_is_number" ]; then
      echo "  MUTATION-CAUGHT=PASS (m8_json_undecoded: scrollCalls=1 and scriptFailures=0"
      echo "    stayed green; the first red assertion is scroll_raw_is_number -- the"
      echo "    raw answer $m8_raw was a quoted envelope, not a number)"
    else
      echo "  MUTATION-CAUGHT=FAIL (m8_json_undecoded: first red was"
      echo "    ${m8_first_bad:-nothing}, wanted scroll_raw_is_number)"; mrc=1
    fi
  else
    echo "  MUTATION-SETUP=FAIL (m8_json_undecoded: the patch changed nothing)"; mrc=1
  fi
  cp "$backup_webview" native/src/logstf_webview.cpp
  cmp -s native/src/logstf_webview.cpp "$backup_webview" || { echo "  RESTORE=FAIL (m8)"; mrc=1; }

  # Product mutation 9: THE TRUNCATION. Rebuild the scroll script with snprintf
  # into char[160], which cuts it at 159 characters: the browser answers a
  # SyntaxError as the JSON literal null, atoi reads 0, and the panel reports
  # scrollCalls=1 scriptFailures=0 on a page that did not move. This is the
  # defect the 2026-10-23 run produced BEFORE the fix, so the mutation restores
  # the exact code that was shipped and requires the offset assertion to notice.
  cp native/src/logstf_viewer.cpp "$backup"
  m9_patch() {
    "$PY" - "$1" <<'PYEOF'
import sys
path = sys.argv[1]
text = open(path, "r", encoding="utf-8").read()  # universal newlines: CRLF -> LF
needle = """  return
      "(function(){var y=(window.scrollY||document.documentElement.scrollTop||0)+("
      + std::to_string(deltaY)
      + ");window.scrollTo(0,y);return String(window.scrollY||document.documentElement.scrollTop||0);})()";"""
replacement = """  char buf[160];
  std::snprintf(buf, sizeof(buf),
      "(function(){var y=(window.scrollY||document.documentElement.scrollTop||0)+(%d);"
      "window.scrollTo(0,y);return String(window.scrollY||document.documentElement.scrollTop||0);})()",
      deltaY);
  return std::string(buf);"""
if needle not in text:
    sys.stderr.write("needle not found\n")
    sys.exit(3)
open(path, "w", encoding="utf-8", newline="\n").write(text.replace(needle, replacement, 1))
PYEOF
  }
  if m9_patch native/src/logstf_viewer.cpp && ! cmp -s native/src/logstf_viewer.cpp "$backup"; then
    if bash build-target.sh tf2_demo_native logstf_viewer_probe > "$OUT/m9.build" 2>&1; then
      echo "  mutated build rc=0 (m9)"
    else
      echo "  MUTATION-BUILD=FAIL (m9): the mutation did not compile, so it scores"
      echo "  nothing -- a build failure is an unverified mutation, not a caught one."; mrc=1
    fi
    # The probe holds the script as a string, so it must go red first.
    "$PROBE" > "$OUT/m9_probe.txt" 2>&1
    m9_probe_rc=$?
    run_product m9 --logstf-script scroll --logstf-url "https://logs.tf/1"
    m9_summary="$(grep -F 'logstf: summary' "$OUT/m9.trace" | tail -1)"
    m9_calls="$(pvalue "$m9_summary" scrollCalls)"
    m9_fail="$(pvalue "$m9_summary" scriptFailures)"
    m9_off="$(pvalue "$m9_summary" scrollOffset)"
    echo "  m9 summary: $m9_summary"
    saved_fail=$fail
    scroll_verdicts "$m9_summary" > "$OUT/m9.verdicts" 2>&1
    m9_first_bad="$SCROLL_FIRST_BAD"
    fail=$saved_fail
    sed 's/^/    /' "$OUT/m9.verdicts"
    echo "  m9 readings: scrollCalls=$m9_calls scriptFailures=$m9_fail scrollOffset=$m9_off first_bad=$m9_first_bad"
    # TWO independent reds are required, because the two halves of this tree
    # learn about truncation differently: the probe holds the script STRING and
    # can refuse it without a browser, while the product can only see the answer
    # the browser gives (null). Either alone would leave one half of the defect
    # unwatched -- a probe-only check says nothing about what a page receives, and
    # a product-only check needs a browser to notice a typo.
    if [ "$m9_calls" != "1" ] || [ "$m9_fail" != "0" ]; then
      echo "  MUTATION-UNREADABLE=FAIL (m9: the call counters moved too, so this"
      echo "    mutation does not isolate the truncation: calls=$m9_calls fail=$m9_fail)"; mrc=1
    elif [ "$m9_probe_rc" -eq 0 ]; then
      echo "  MUTATION-CAUGHT=FAIL (m9_script_truncated: the probe accepted a"
      echo "    truncated script -- scrollScriptComplete=1 stayed green)"; mrc=1
    elif [ "$m9_first_bad" != "scroll_raw_is_number" ]; then
      echo "  MUTATION-CAUGHT=FAIL (m9_script_truncated: the probe went red but the"
      echo "    product first red was ${m9_first_bad:-nothing}, wanted scroll_raw_is_number)"; mrc=1
    else
      echo "  MUTATION-CAUGHT=PASS (m9_script_truncated: the probe refused the string"
      echo "    and the product first red was scroll_raw_is_number -- scrollCalls=1 and"
      echo "    scriptFailures=0 stayed green while the browser answered null)"
    fi
  else
    echo "  MUTATION-SETUP=FAIL (m9_script_truncated: the patch changed nothing)"; mrc=1
  fi
  cp "$backup" native/src/logstf_viewer.cpp
  cmp -s native/src/logstf_viewer.cpp "$backup" || { echo "  RESTORE=FAIL (m9)"; mrc=1; }

  # Restore the real binaries after the last mutation.
  bash build-target.sh tf2_demo_native logstf_viewer_probe > "$OUT/restore.build" 2>&1 \
    || { echo "  RESTORE-BUILD=FAIL"; mrc=1; }
  "$PROBE" > "$OUT/restore_probe.txt" 2>&1 \
    || { echo "  RESTORE-PROBE=FAIL (the restored probe is not green)"; mrc=1; }

  [ "$mrc" -eq 0 ] || { echo "LOGSTF-MUTATION=FAIL"; exit 1; }
  echo "LOGSTF-MUTATION=PASS (eight capture mutations, eight product mutations and one seam mutation all went red)"
fi

echo
if [ "$fail" -eq 0 ]; then
  echo "LOGSTF=PASS panel_content=$PANEL_CONTENT runtime_available=$RUNTIME_AVAILABLE"
  exit 0
fi
echo "LOGSTF=FAIL"
exit 1
