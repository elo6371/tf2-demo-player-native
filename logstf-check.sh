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
OUT="\${LOGSTF_OUT:-.scratch/logstf}"
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
  if [ "\${2:-<none>}" = "$3" ]; then echo "  OK   $1 = $3"
  else echo "  FAIL $1 = \${2:-<none>}, expected $3"; fail=1; fi
}
assert_ge() {
  if [ -n "\${2:-}" ] && [ "\${2:-x}" -ge "$3" ] 2>/dev/null; then echo "  OK   $1 = $2 (>= $3)"
  else echo "  FAIL $1 = \${2:-<none>}, expected >= $3"; fail=1; fi
}
assert_nonempty() {
  if [ -n "\${2:-}" ] && [ "\${2:-}" != "-" ]; then echo "  OK   $1 is named"
  else echo "  FAIL $1 is empty (must never be silent)"; fail=1; fi
}
assert_contains() {
  case "$2" in *"$3"*) echo "  OK   $1" ;; *) echo "  FAIL $1 (wanted: $3)"; fail=1 ;; esac
}
pvalue() { # pvalue <line> <field>
  printf '%s\n' "$1" | grep -oE "$2=[^ ]*" | head -1 | cut -d= -f2-
}

# run_product <label> <extra args...> -- one quiet run, trace captured.
run_product() {
  local label="$1"; shift
  local trace
  trace="$(cygpath -m "$PWD/$OUT/$label.trace")"
  rm -f "$OUT/$label.trace" "$OUT/$label.rc"
  TF2_NATIVE_TRACE="$trace" timeout "$RUN_TIMEOUT" \
    "$EXE" --tf-root "$TF" --demo "$BAGEL" --hidden-window --audio-disabled \
    --capture-frame "$OUT/$label.bmp" --capture-tick "$CAPTURE_TICK" \
    --settings-file "$(cygpath -m "$PWD/$OUT/$label.settings")" "$@" \
    > "$OUT/$label.stdout" 2>&1
  echo "$?" > "$OUT/$label.rc"
}

# --- 1/3 the probe ------------------------------------------------------------
echo "=== 1/3 logstf_viewer_probe (URL policy, empty state, geometry, failure text) ==="
ok=0; bad=0
"$PROBE" > "$OUT/probe.txt" 2>&1
probe_rc=$?
cat "$OUT/probe.txt"
echo "probe_exit=$probe_rc"
assert_eq "probe exit" "$probe_rc" "0"

P_ASSERTIONS=(
  'urlAccepted=15|url: every accepted shape is accepted verbatim'
  'urlRefused=26|url: every refusal names its own reason'
  'homographRefused=1|url: a non-ASCII homograph never reaches the host rule'
  'injectionRefused=1|url: a header-injection attempt is refused'
  'emptyState=empty|empty: an open panel with no URL is Empty'
  'emptyHostCalls=0|empty: zero host calls'
  'emptyNavigations=0|empty: zero navigations'
  'reopenState=empty|empty: a reopen is Empty again'
  'reopenHostCalls=0|empty: a reopen still makes no host call'
  'refusalKeepsEmpty=1|empty: a refused import leaves the panel Empty'
  'refusalHasReason=1|empty: a refused import names its reason'
  'contentState=content|content: a valid URL with a live host lands on Content'
  'contentNavigations=1|content: exactly one navigation'
  'backCalls=1|content: back reached the host once'
  'forwardCalls=1|content: forward reached the host once'
  'reloadCalls=1|content: reload reached the host once'
  'reopenNavigations=2|content: a reopen restored exactly one navigation'
  'reopenState=content|content: a reopen lands on Content'
  'unavailableState=unavailable|failure: no runtime reports Unavailable'
  'unavailableHasReason=1|failure: no runtime names the reason'
  'unavailableHasFallback=1|failure: no runtime names the fallback'
  'geometryOutside=0|geometry: nothing leaves the client at any size/dpi'
  'geometryOverlaps=0|geometry: no two toolbar controls overlap'
  'geometryNoContent=0|geometry: every panel size leaves a content rect'
  'geometryDemoSqueezed=0|geometry: the demo keeps its minimum width'
  'geometryUnreachable=0|geometry: every toolbar control stays reachable'
  'scrollOffset=480|scroll: the offset the page took is what is reported'
  'scrollClamped=0|scroll: a page that cannot scroll reports 0, not the request'
  'scrollHeight=4200|scroll: the page height is read back'
  'viewportHeight=720|scroll: the viewport height is read back'
  'histogramSamples=8192|frames: the ring is bounded'
  'runtimeKind=webview2|runtime: this host has a web-view2 runtime'
  'runtimeAvailable=1|runtime: the runtime is usable'
)

run_probe_expectations() { # run_probe_expectations <file> <needle|what>...
  local file="$1"; shift
  local e
  for e in "$@"; do
    local needle="\${e%%|*}" what="\${e#*|}"
    if [ -s "$file" ] && grep -qF -- "$needle" "$file"; then
      ok=$((ok + 1)); printf '  OK   %s\n' "$what"
    else
      bad=$((bad + 1)); printf '  FAIL %s (wanted: %s)\n' "$what" "$needle"
    fi
  done
}

run_probe_expectations "$OUT/probe.txt" "\${P_ASSERTIONS[@]}"
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
assert_contains "product runtime kind" "$empty_runtime" "kind=webview2"
assert_contains "product loader status" "$empty_runtime" "loader="
#[no-lower]

# 2b. A malformed and a wrong-host URL. Both refused, both with a reason, and
# the panel stays empty -- a refusal is not a navigation to a search page.
run_product refuse --logstf-script import --logstf-url "https://evil.example/log/1"
refuse_summary="$(grep -F 'logstf: summary' "$OUT/refuse.trace" | tail -1)"
echo "  refuse.summary: $refuse_summary"
assert_contains "wrong host refused" "$refuse_summary" "refused=1"
assert_contains "wrong host never navigated" "$refuse_summary" " navigations=0"
assert_contains "wrong host names its reason" "$refuse_summary" "refusal=host-not-allowed"

run_product malformed --logstf-script import --logstf-url "javascript:alert(1)"
malformed_summary="$(grep -F 'logstf: summary' "$OUT/malformed.trace" | tail -1)"
echo "  malformed.summary: $malformed_summary"
assert_contains "malformed URL refused" "$malformed_summary" "refused=1"
assert_contains "malformed URL names its reason" "$malformed_summary" "refusal=missing-scheme"
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
  assert_contains "scroll reached the page" "$scroll_summary" "scrollCalls=2"
  assert_contains "scroll script succeeded" "$scroll_summary" "scriptFailures=0"

  run_product resize --logstf-script resize --logstf-url "https://logs.tf/1"
  resize_summary="$(grep -F 'logstf: summary' "$OUT/resize.trace" | tail -1)"
  echo "  resize.summary: $resize_summary"
  resize_width="$(pvalue "$resize_summary" panel)"
  # The scripted run asks for 1 then 100000; the panel must report the clamped
  # maximum, which is the client width minus the demo's minimum -- not the
  # request and not the minimum.
  assert_eq "resize clamps the request to the edge" "$resize_width" "1600"
  assert_contains "resize stays in content" "$resize_summary" "state=content"

  run_product history --logstf-script history --logstf-url "https://logs.tf/1" --logstf-url2 "https://logs.tf/2"
  history_summary="$(grep -F 'logstf: summary' "$OUT/history.trace" | tail -1)"
  echo "  history.summary: $history_summary"
  assert_contains "history navigated twice" "$history_summary" " navigations=2"
  assert_contains "history kept both directions" "$history_summary" "canBack=1 canForward=1"
  assert_contains "history ended on the second URL" "$history_summary" "url=https://logs.tf/2"

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
open_ticks="$(grep -oE 'tick=[0-9]+' "$OUT/frames_open.trace" | tail -1 | cut -d= -f2)"
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
    run_probe_expectations "$copy" "\${P_ASSERTIONS[@]}" > "$OUT/mutated-$name.log" 2>&1
    local mok=$ok mbad=$bad
    ok=$saved_ok; bad=$saved_bad
    if [ "$mbad" -ge 1 ]; then
      printf '  MUTATION-CAUGHT=PASS (%s: %s of %s assertions went red)\n' "$name" "$mbad" "$((mok + mbad))"
    else
      printf '  MUTATION-CAUGHT=FAIL (%s: no assertion moved)\n' "$name"; mrc=1
    fi
  }

  mutate_capture url_host       "$OUT/probe.txt" 's/urlRefused=26/urlRefused=25/'
  mutate_capture empty_calls    "$OUT/probe.txt" 's/emptyHostCalls=0/emptyHostCalls=1/'
  mutate_capture empty_navigate "$OUT/probe.txt" 's/emptyNavigations=0/emptyNavigations=1/'
  mutate_capture unavailable    "$OUT/probe.txt" 's/unavailableHasReason=1/unavailableHasReason=0/'
  mutate_capture geometry       "$OUT/probe.txt" 's/geometryOverlaps=0/geometryOverlaps=3/'
  mutate_capture scroll         "$OUT/probe.txt" 's/scrollOffset=480/scrollOffset=0/'

  # Product mutation 1: the host allow-list accepts anything. The probe (which
  # links the same policy) must go red on the accepted/refused cases, and the
  # product's own wrong-host run must start navigating.
  backup="$OUT/logstf_viewer.cpp.backup"
  cp native/src/logstf_viewer.cpp "$backup"
  sed -i 's|return lowered == "logs.tf" \|\| lowered == "www.logs.tf";|return true;|' native/src/logstf_viewer.cpp
  if cmp -s native/src/logstf_viewer.cpp "$backup"; then
    echo "  MUTATION-SETUP=FAIL (m1_wrong_host: the sed changed nothing)"; mrc=1
  else
    bash build-target.sh tf2_demo_native logstf_viewer_probe > "$OUT/m1.build" 2>&1 \
      || { echo "  MUTATION-BUILD=FAIL (m1)"; mrc=1; }
    "$PROBE" > "$OUT/m1_probe.txt" 2>&1
    m1_probe_rc=$?
    run_product m1 --logstf-script import --logstf-url "https://evil.example/log/1"
    m1_summary="$(grep -F 'logstf: summary' "$OUT/m1.trace" | tail -1)"
    if [ "$m1_probe_rc" -ne 0 ] && ! grep -qF 'urlRefused=26' "$OUT/m1_probe.txt" \
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
text = open(path, "r", encoding="utf-8", newline="").read()
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
open(path, "w", encoding="utf-8", newline="").write(text.replace(needle, replacement, 1))
PYEOF
  }
  if python_patch_empty native/src/logstf_viewer.cpp && ! cmp -s native/src/logstf_viewer.cpp "$backup"; then
    bash build-target.sh tf2_demo_native logstf_viewer_probe > "$OUT/m2.build" 2>&1 \
      || { echo "  MUTATION-BUILD=FAIL (m2)"; mrc=1; }
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
  # "blank panel, no explanation" the task forbids.
  cp native/src/logstf_viewer.cpp "$backup"
  sed -i 's|unavailable_ = runtime_.reason.empty()|unavailable_ = true ? std::string() : runtime_.reason.empty()|' native/src/logstf_viewer.cpp
  if cmp -s native/src/logstf_viewer.cpp "$backup"; then
    echo "  MUTATION-SETUP=FAIL (m3_silent_runtime: the sed changed nothing)"; mrc=1
  else
    bash build-target.sh logstf_viewer_probe > "$OUT/m3.build" 2>&1 \
      || { echo "  MUTATION-BUILD=FAIL (m3)"; mrc=1; }
    "$PROBE" > "$OUT/m3_probe.txt" 2>&1
    m3_probe_rc=$?
    if [ "$m3_probe_rc" -ne 0 ] && ! grep -qF 'unavailableHasReason=1' "$OUT/m3_probe.txt"; then
      echo "  MUTATION-CAUGHT=PASS (m3_silent_runtime: the reason assertion went red)"
    else
      echo "  MUTATION-CAUGHT=FAIL (m3_silent_runtime: the gate stayed green)"; mrc=1
    fi
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
    bash build-target.sh logstf_viewer_probe > "$OUT/m4.build" 2>&1 \
      || { echo "  MUTATION-BUILD=FAIL (m4)"; mrc=1; }
    "$PROBE" > "$OUT/m4_probe.txt" 2>&1
    m4_probe_rc=$?
    if [ "$m4_probe_rc" -ne 0 ] && grep -qE 'geometryOutside=[1-9]' "$OUT/m4_probe.txt"; then
      echo "  MUTATION-CAUGHT=PASS (m4_resize_unclamped: the geometry matrix went red)"
    else
      echo "  MUTATION-CAUGHT=FAIL (m4_resize_unclamped: the gate stayed green)"; mrc=1
    fi
  fi
  cp "$backup" native/src/logstf_viewer.cpp
  cmp -s native/src/logstf_viewer.cpp "$backup" || { echo "  RESTORE=FAIL (m4)"; mrc=1; }

  # Restore the real binaries after the last mutation.
  bash build-target.sh tf2_demo_native logstf_viewer_probe > "$OUT/restore.build" 2>&1 \
    || { echo "  RESTORE-BUILD=FAIL"; mrc=1; }
  "$PROBE" > "$OUT/restore_probe.txt" 2>&1 \
    || { echo "  RESTORE-PROBE=FAIL (the restored probe is not green)"; mrc=1; }

  [ "$mrc" -eq 0 ] || { echo "LOGSTF-MUTATION=FAIL"; exit 1; }
  echo "LOGSTF-MUTATION=PASS (six capture mutations and four product mutations all went red)"
fi

echo
if [ "$fail" -eq 0 ]; then
  echo "LOGSTF=PASS panel_content=$PANEL_CONTENT runtime_available=$RUNTIME_AVAILABLE"
  exit 0
fi
echo "LOGSTF=FAIL"
exit 1
