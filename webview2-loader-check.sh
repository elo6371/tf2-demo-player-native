#!/usr/bin/env bash
# webview2-loader-check.sh -- WEB-3: the WebView2 loader is loaded from the
# executable's directory, never by bare name and never from the CWD.
#
# WHY THIS EXISTS
#   logstf_webview.cpp used to call LoadLibraryW(L"WebView2Loader.dll"). A bare
#   name lets the Windows loader accept a copy planted in the working directory,
#   so "the panel works" could be a statement about a DLL the product did not
#   ship. The fix resolves the loader path from GetModuleFileNameW and calls
#   LoadLibraryExW with LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|SYSTEM32. This gate
#   proves both halves with one positive and one negative layout:
#
#     positive: exe + real loader in the exe directory, CWD has neither
#               -> logstf: runtime ... loader=present
#     negative: exe alone in the exe directory, real loader in the CWD
#               -> logstf: runtime ... loader=missing (...exe-dir path...)
#
#   The negative run is the one that distinguishes this fix from the old bare
#   name: the same layout makes the pre-fix executable report loader=present.
#   That old executable is an OPTIONAL control (--old-exe); its answer is printed
#   as OLD-WEBVIEW-LOADER=present|missing|unavailable and is deliberately NOT one
#   of the 22 assertions. The gate passes when the old executable cannot be
#   found, because the old binary is not a usability prerequisite.
#
# WHAT THIS GATE DOES NOT CLAIM
#   It does not claim a page renders, that the Evergreen runtime exists on every
#   host, or that the loader exports every interface. It asserts the load path
#   and the product's own runtime line only.
#
# INDEPENDENT BY DESIGN
#   It does not read another gate's output tree; it stages its own layouts under
#   .scratch/webview2-loader. It does not acquire hostlock (hostlock is not
#   reentrant); it requires the caller to hold it and refuses with exit 9
#   otherwise, the same convention as ci-fast.
#
# Usage:
#   bash /d/TF2_Native_Worktrees/_shared/hostlock.sh run <owner> \
#     env WEBVIEW2_LOADER_OWNER=<owner> bash webview2-loader-check.sh [options]
#
# Options:
#   --exe PATH        product exe (default native/build-nmake/tf2_demo_native.exe)
#   --loader PATH     WebView2Loader.dll (default beside the exe)
#   --demo PATH       demo used by the product run
#   --tf-root PATH    TF2 tf directory
#   --old-exe PATH    optional pre-fix exe for the control line
#   --out DIR         scratch directory (default .scratch/webview2-loader)
#   --capture-tick N  product capture tick (default 600)
#   --keep            accepted for compatibility; the scratch tree is always retained
#
# Exit: 0 = 22/22 assertions held; 1 = an assertion failed; 9 = lock not held.

set -uo pipefail
cd "$(dirname "$0")" || exit 9
export LC_ALL=C

EXE="native/build-nmake/tf2_demo_native.exe"
LOADER=""
TF="D:/SteamLibrary/steamapps/common/Team Fortress 2/tf"
DEMO="D:/TF2_Demo_Player/testdata/demos/4a9bfb9276509d0ec5f5fdc722a95b17_match-20260927-0239-koth_bagel_rc13.dem"
OUT=".scratch/webview2-loader"
OLD_EXE="/d/_shared/EXEC-2026-11-12/R0/before-clean/tf2_demo_native-incr.exe"
CAPTURE_TICK=600
RUN_TIMEOUT=180
KEEP=0
HOSTLOCK="${HOSTLOCK:-/d/TF2_Native_Worktrees/_shared/hostlock.sh}"
OWNER="${WEBVIEW2_LOADER_OWNER:-}"

while [ "$#" -gt 0 ]; do
  case "$1" in
    --exe) EXE="$2"; shift 2 ;;
    --loader) LOADER="$2"; shift 2 ;;
    --demo) DEMO="$2"; shift 2 ;;
    --tf-root) TF="$2"; shift 2 ;;
    --old-exe) OLD_EXE="$2"; shift 2 ;;
    --out) OUT="$2"; shift 2 ;;
    --capture-tick) CAPTURE_TICK="$2"; shift 2 ;;
    --keep) KEEP=1; shift ;;
    *) echo "webview2-loader-check: unknown argument: $1" >&2; exit 2 ;;
  esac
done

[ -n "$LOADER" ] || LOADER="$(dirname "$EXE")/WebView2Loader.dll"

# ---------------------------------------------------------------- lock guard
# The gate launches the product twice, so it is only meaningful inside a lock.
# This is deliberately checked before anything is created or run.
if [ -z "$OWNER" ]; then
  echo "WEBVIEW-LOADER-CHECK=FAIL lock=not-held reason=WEBVIEW2_LOADER_OWNER-unset"
  echo "  run it as: bash $HOSTLOCK run <owner> env WEBVIEW2_LOADER_OWNER=<owner> bash $0" >&2
  exit 9
fi
if [ ! -f "$HOSTLOCK" ]; then
  echo "WEBVIEW-LOADER-CHECK=FAIL lock=not-held reason=hostlock-script-missing path=$HOSTLOCK"
  exit 9
fi
lock_status="$(bash "$HOSTLOCK" status 2>&1)"
held="$(printf '%s\n' "$lock_status" | sed -n 's/^LOCKED owner=\([^ ]*\).*/\1/p' | head -1)"
if [ -z "$held" ]; then
  echo "WEBVIEW-LOADER-CHECK=FAIL lock=not-held reason=hostlock-not-held"
  echo "$lock_status" | sed 's/^/  lock: /' >&2
  exit 9
fi
if [ "$held" != "$OWNER" ]; then
  echo "WEBVIEW-LOADER-CHECK=FAIL lock=owner-mismatch held=$held want=$OWNER"
  exit 9
fi

# ---------------------------------------------------------------- assertions
ok=0
bad=0
assert_eq() {
  if [ "${2:-<none>}" = "$3" ]; then
    echo "  OK   $1 = $3"; ok=$((ok + 1))
  else
    echo "  FAIL $1 = ${2:-<none>}, expected $3"; bad=$((bad + 1))
  fi
}
assert_contains() {
  case "${2:-}" in
    *"$3"*) echo "  OK   $1"; ok=$((ok + 1)) ;;
    *) echo "  FAIL $1 (wanted: $3)"; bad=$((bad + 1)) ;;
  esac
}
assert_not_contains() {
  case "${2:-}" in
    *"$3"*) echo "  FAIL $1 (forbidden: $3)"; bad=$((bad + 1)) ;;
    *) echo "  OK   $1"; ok=$((ok + 1)) ;;
  esac
}

finish() {
  local total=$((ok + bad))
  if [ "$bad" -eq 0 ] && [ "$total" -eq 22 ]; then
    echo "WEBVIEW-LOADER-CHECK=PASS ($total assertions)"
    echo "out=$OUT (retained after PASS)"
    exit 0
  fi
  echo "WEBVIEW-LOADER-CHECK=FAIL ($total assertions, $bad failed)"
  exit 1
}

# ---------------------------------------------------------------- preflight (4)
echo "=== preflight ==="
echo "exe=$EXE"
echo "exe_sha256=$(sha256sum "$EXE" 2>/dev/null | cut -d' ' -f1)"
echo "exe_bytes=$(wc -c < "$EXE" 2>/dev/null | tr -d ' ')"
echo "loader=$LOADER"
echo "loader_sha256=$(sha256sum "$LOADER" 2>/dev/null | cut -d' ' -f1)"
echo "demo=$DEMO"
echo "tf_root=$TF"
echo "head=$(git rev-parse HEAD 2>/dev/null || echo unknown)"

assert_eq "preflight product exe exists" "$([ -f "$EXE" ] && [ -x "$EXE" ] && echo yes || echo no)" "yes"
assert_eq "preflight loader beside exe exists" "$([ -f "$LOADER" ] && echo yes || echo no)" "yes"
assert_eq "preflight demo exists" "$([ -f "$DEMO" ] && echo yes || echo no)" "yes"
assert_eq "preflight tf root exists" "$([ -d "$TF" ] && echo yes || echo no)" "yes"
if [ "$bad" -ne 0 ]; then
  echo "  preflight failed; the two runs were not started"
  finish
fi

# --------------------------------------------------- source load-site (4)
# The two runs prove the executable-directory mechanism through the probe line
# (loaderStatus). They would stay green if the panel's loadLibrary() reverted to
# a bare name, because that is a different call site (audit D-2). Pin both the
# helper call and the absence of a bare LoadLibraryW in the source.
WEBVIEW_SRC="native/src/logstf_webview.cpp"
assert_eq "source exists" "$([ -f "$WEBVIEW_SRC" ] && echo yes || echo no)" "yes"
assert_eq "source LoadLibraryExW call count" "$(grep -c 'LoadLibraryExW(' "$WEBVIEW_SRC" 2>/dev/null || true)" "1"
assert_eq "source bare LoadLibraryW call count" "$(grep -c 'LoadLibraryW(' "$WEBVIEW_SRC" 2>/dev/null || true)" "0"
assert_eq "source loadLibrary() calls the exe-dir helper" "$(sed -n '/void loadLibrary()/,/^  }$/p' "$WEBVIEW_SRC" 2>/dev/null | grep -c 'loadWebView2Loader(' || true)" "1"

command -v cygpath >/dev/null 2>&1 || { echo "FATAL: cygpath missing"; exit 1; }
command -v timeout >/dev/null 2>&1 || { echo "FATAL: timeout missing"; exit 1; }
mkdir -p "$OUT" || { echo "FATAL: cannot create $OUT"; exit 1; }
EXE_SHA="$(sha256sum "$EXE" | cut -d' ' -f1)"
LOADER_SHA="$(sha256sum "$LOADER" | cut -d' ' -f1)"

# run_case <label> <exe> <cwd> <dir>
# The argv is assembled once and the safety flags are checked on that argv, not
# on a comment: a run without --hidden-window would steal focus, and one without
# --audio-disabled would play the demo into the room.
run_case() {
  local label="$1" exe="$2" cwd="$3" dir="$4"
  mkdir -p "$cwd" "$dir"
  # Resolve both paths BEFORE the subshell changes directory. Keeping them
  # relative is how the first version lost its stdout redirect: the shell tried
  # to write .scratch/... from inside .scratch/.../work and the product never ran.
  local absdir abscwd absexe
  absdir="$(cd "$dir" && pwd)" || return 1
  abscwd="$(cd "$cwd" && pwd)" || return 1
  absexe="$(cd "$(dirname "$exe")" && pwd)/$(basename "$exe")" || return 1
  local -a argv=(--tf-root "$TF" --demo "$DEMO" --hidden-window --audio-disabled
                 --capture-frame "$(cygpath -m "$absdir/$label.bmp")"
                 --capture-tick "$CAPTURE_TICK"
                 --settings-file "$(cygpath -m "$absdir/$label.settings")"
                 --ui-settings-file "$(cygpath -m "$absdir/$label.ui.ini")"
                 --logstf-script empty)
  case " ${argv[*]} " in *" --hidden-window "*) : ;; *) echo "REFUSED: no --hidden-window" >&2; return 99 ;; esac
  case " ${argv[*]} " in *" --audio-disabled "*) : ;; *) echo "REFUSED: no --audio-disabled" >&2; return 98 ;; esac
  rm -f "$absdir/$label.trace" "$absdir/$label.rc" "$absdir/$label.stdout"
  ( cd "$abscwd" && TF2_NATIVE_TRACE="$(cygpath -m "$absdir/$label.trace")" \
      timeout "$RUN_TIMEOUT" "$absexe" "${argv[@]}" > "$absdir/$label.stdout" 2>&1 )
  echo "$?" > "$absdir/$label.rc"
  echo "  $label rc=$(cat "$absdir/$label.rc") trace_lines=$(wc -l < "$absdir/$label.trace" 2>/dev/null | tr -d ' ')"
}

# ---------------------------------------------------------------- positive (6)
echo
echo "=== positive: exe + loader in the exe directory, CWD empty ==="
POS_APP="$OUT/pos/app"
POS_WORK="$OUT/pos/work"
mkdir -p "$POS_APP" "$POS_WORK"
rm -f "$POS_APP/tf2_demo_native.exe" "$POS_APP/WebView2Loader.dll"
cp -p "$EXE" "$POS_APP/tf2_demo_native.exe"
cp -p "$LOADER" "$POS_APP/WebView2Loader.dll"
assert_eq "positive layout has exe" "$([ -f "$POS_APP/tf2_demo_native.exe" ] && echo yes || echo no)" "yes"
assert_eq "positive layout has loader beside exe" "$([ -f "$POS_APP/WebView2Loader.dll" ] && echo yes || echo no)" "yes"
run_case positive "$POS_APP/tf2_demo_native.exe" "$POS_WORK" "$OUT/pos"
POS_RC="$(cat "$OUT/pos/positive.rc" 2>/dev/null || echo missing)"
POS_TRACE="$OUT/pos/positive.trace"
POS_RUNTIME="$(grep -F 'logstf: runtime' "$POS_TRACE" 2>/dev/null | tail -1)"
echo "  positive.runtime: ${POS_RUNTIME:-<none>}"
assert_eq "positive run rc" "$POS_RC" "0"
assert_eq "positive runtime line present" "$([ -n "$POS_RUNTIME" ] && echo yes || echo no)" "yes"
assert_contains "positive loader is present" "$POS_RUNTIME" "loader=present"
assert_not_contains "positive does not report missing" "$POS_RUNTIME" "loader=missing"

# ---------------------------------------------------------------- negative (8)
echo
echo "=== negative: exe alone beside CWD, loader only in CWD ==="
NEG_APP="$OUT/neg/app"
NEG_WORK="$OUT/neg/work"
mkdir -p "$NEG_APP" "$NEG_WORK"
rm -f "$NEG_APP/tf2_demo_native.exe" "$NEG_APP/WebView2Loader.dll" "$NEG_WORK/WebView2Loader.dll"
cp -p "$EXE" "$NEG_APP/tf2_demo_native.exe"
cp -p "$LOADER" "$NEG_WORK/WebView2Loader.dll"
assert_eq "negative layout has exe" "$([ -f "$NEG_APP/tf2_demo_native.exe" ] && echo yes || echo no)" "yes"
assert_eq "negative exe directory has no loader" "$([ ! -e "$NEG_APP/WebView2Loader.dll" ] && echo yes || echo no)" "yes"
assert_eq "negative CWD loader sha256" "$(sha256sum "$NEG_WORK/WebView2Loader.dll" | cut -d' ' -f1)" "$LOADER_SHA"
run_case negative "$NEG_APP/tf2_demo_native.exe" "$NEG_WORK" "$OUT/neg"
NEG_RC="$(cat "$OUT/neg/negative.rc" 2>/dev/null || echo missing)"
NEG_TRACE="$OUT/neg/negative.trace"
NEG_RUNTIME="$(grep -F 'logstf: runtime' "$NEG_TRACE" 2>/dev/null | tail -1)"
echo "  negative.runtime: ${NEG_RUNTIME:-<none>}"
assert_eq "negative run rc" "$NEG_RC" "0"
assert_eq "negative runtime line present" "$([ -n "$NEG_RUNTIME" ] && echo yes || echo no)" "yes"
assert_contains "negative loader is missing" "$NEG_RUNTIME" "loader=missing"
assert_not_contains "negative does not report present" "$NEG_RUNTIME" "loader=present"
NEG_APP_WIN="$(cygpath -w "$NEG_APP")"
NEG_WORK_WIN="$(cygpath -w "$NEG_WORK")"
NEG_MISSING_PATH="$NEG_APP_WIN\\WebView2Loader.dll"
neg_path_ok=no
case "$NEG_RUNTIME" in
  *"$NEG_MISSING_PATH"*) case "$NEG_RUNTIME" in *"$NEG_WORK_WIN"*) ;; *) neg_path_ok=yes ;; esac ;;
esac
assert_eq "negative missing path names the exe directory, not the CWD" "$neg_path_ok" "yes"

# ---------------------------------------------------------------- old control
# Not an assertion: the old executable is optional. Print one line with one of
# three answers. If it is present and runs, the negative layout should make the
# pre-fix bare-name load find the CWD copy, which is the point of the control.
echo
echo "=== old-exe control (not one of the 22 assertions) ==="
if [ ! -f "$OLD_EXE" ]; then
  echo "OLD-WEBVIEW-LOADER=unavailable (old-exe-missing: $OLD_EXE)"
else
  OLD_APP="$OUT/old/app"
  OLD_WORK="$OUT/old/work"
  mkdir -p "$OLD_APP" "$OLD_WORK"
  rm -f "$OLD_APP/tf2_demo_native_old.exe" "$OLD_APP/WebView2Loader.dll" "$OLD_WORK/WebView2Loader.dll"
  cp -p "$OLD_EXE" "$OLD_APP/tf2_demo_native_old.exe"
  cp -p "$LOADER" "$OLD_WORK/WebView2Loader.dll"
  run_case old "$OLD_APP/tf2_demo_native_old.exe" "$OLD_WORK" "$OUT/old" >/dev/null 2>&1 || true
  old_rc="$(cat "$OUT/old/old.rc" 2>/dev/null || echo missing)"
  old_runtime="$(grep -F 'logstf: runtime' "$OUT/old/old.trace" 2>/dev/null | tail -1)"
  echo "  old.rc: $old_rc"
  echo "  old.runtime: ${old_runtime:-<none>}"
  if [ "$old_rc" != "0" ] || [ -z "$old_runtime" ]; then
    echo "OLD-WEBVIEW-LOADER=unavailable (old-exe-run-failed rc=$old_rc)"
  elif printf '%s' "$old_runtime" | grep -qF 'loader=present'; then
    echo "OLD-WEBVIEW-LOADER=present"
  elif printf '%s' "$old_runtime" | grep -qF 'loader=missing'; then
    echo "OLD-WEBVIEW-LOADER=missing"
  else
    echo "OLD-WEBVIEW-LOADER=unavailable (old-exe-runtime-unreadable)"
  fi
fi

echo
finish
