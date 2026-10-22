#!/usr/bin/env bash
# T54 locked run: build both targets, run the probe, run the gate, and -- only if
# the forward gate is green -- the mutation suite. Everything here is a host
# single-instance operation and is invoked from inside hostlock.sh run, so no lock
# logic lives in this file.
set -uo pipefail
cd /d/TF2_Native_Worktrees/t54-logstf || exit 90

# Two tickets are queued for this run (owner t54-logstf and t54-logstf-backup) so a
# stuck queue cannot cost the whole task. Whichever is granted first must end the
# other, or the same twenty-minute run executes twice and the second rebuilds the
# binaries the first just verified. The waiter touches its own ticket every two
# seconds, so deleting the ticket alone resurrects it: kill the waiter's pid, which
# the ticket filename carries as its last field.
for ticket in /d/TF2_Native_Worktrees/_shared/queue/*t54-logstf*; do
  [ -e "$ticket" ] || continue
  pid=${ticket##*-}
  echo "DUPLICATE-TICKET removed $(basename "$ticket") waiter_pid=$pid"
  kill "$pid" 2>/dev/null || echo "  (waiter pid $pid already gone)"
  rm -f "$ticket"
done
# The kill is best-effort across shells; the marker is not. A ticket that was
# granted while the other was still waking up finds this file and stands down
# instead of re-running the gate.
# The marker names the process that wrote it. A marker whose process is still
# alive means another ticket is running THIS gate right now; a marker whose
# process is gone is just a note from the previous run. (A file's mere existence
# is not "a run is in progress" -- that reading would block every later run, which
# is the same mistake as reading "no writes here" as "no work happening".)
MARKER=.scratch/t54-run.started
if [ -f "$MARKER" ]; then
  running_pid=$(cut -d' ' -f1 "$MARKER" 2>/dev/null)
  if [ -n "$running_pid" ] && kill -0 "$running_pid" 2>/dev/null && [ -n "$(find "$MARKER" -mmin -180 2>/dev/null)" ]; then
    echo "T54-RUN=SKIP (a ticket is running this gate as pid $running_pid: $(cat "$MARKER"))"
    exit 0
  fi
  echo "stale run marker (pid $running_pid is gone): $(cat "$MARKER")"
fi
echo "$$ $(date '+%Y-%m-%d %H:%M:%S')" > "$MARKER"

STAMP=$(date +%Y%m%d-%H%M%S)
# ABSOLUTE: the install-preview step cds into the staged tree before writing its
# file list, and a relative $LOGDIR resolved inside the preview directory. "tee:
# ... No such file or directory" was then reported as
# "INSTALL-PREVIEW=NOT-ATTEMPTED-COMPLETELY" while the install had in fact printed
# all eleven files, licences included. That reading was about the log's path, not
# about the install.
LOGDIR="$PWD/.scratch/t54-locked-$STAMP"
mkdir -p "$LOGDIR" evidence/logstf
echo "LOGDIR=$LOGDIR"
echo "PWD=$PWD"
git log --oneline -1
git status --porcelain | head -20
echo

SRCS="native/src/main.cpp native/src/logstf_viewer.cpp native/src/logstf_webview.cpp native/src/logstf_panel.cpp native/tools/logstf_viewer_probe.cpp native/CMakeLists.txt"
sha256sum $SRCS > "$LOGDIR/sources-before.sha256"
cat "$LOGDIR/sources-before.sha256"

echo "=== SYNTAX /Zs (read-only, runs before the build it protects) $(date '+%H:%M:%S') ==="
bash logstf-syntax-check.sh 2>&1 | tee "$LOGDIR/syntax-check.log"
echo "SYNTAX_RC=${PIPESTATUS[0]}"
# A syntax failure does not stop the build: the build is the authority on whether
# the tree compiles, and its error output is better. The check is here so that a
# red build is not the FIRST time anyone learns about a type error.

echo "=== BUILD $(date '+%H:%M:%S') ==="
# native_install_smoke_probe is not needed by the gate; it is needed by the
# INSTALL. The generated install script copies every installed executable, so a
# build of only two targets made "cmake --install" stop at line 40 -- four lines
# before the licence files -- and the licence landing could not be seen at all.
# Building the third target is ~30 s and turns "the rule exists" into "the files
# are in the tree".
# vpk_integrity_probe is the readiness gate's third binary (install-readiness-check.sh:221).
bash build-target.sh tf2_demo_native logstf_viewer_probe native_install_smoke_probe vpk_integrity_probe 2>&1 | tee "$LOGDIR/build.log" | tail -25
BUILD_RC=${PIPESTATUS[0]}
echo "BUILD_RC=$BUILD_RC"
if [ "$BUILD_RC" -ne 0 ]; then
  echo "=== BUILD ERRORS (first 40) ==="
  grep -nE " error |error C|error LNK|fatal error" "$LOGDIR/build.log" | head -40 || true
  echo "=== BUILD LOG TAIL ==="
  tail -40 "$LOGDIR/build.log"
  echo "T54-RUN-DONE build=$BUILD_RC forward=skipped mutation=skipped logdir=$LOGDIR $(date '+%H:%M:%S')"
  exit 10
fi
ls -l native/build-nmake/tf2_demo_native.exe native/build-nmake/logstf_viewer_probe.exe

echo
echo "=== INSTALL PREVIEW: where the WebView2 licence actually lands $(date '+%H:%M:%S') ==="
# The BSD-3 clause 2 obligation is about the bytes that ship, not about a rule in
# CMakeLists: this stages a real install and lists what appeared, so the report
# can name a path found in a tree instead of a DESTINATION string. If the tree
# has install rules for targets this run did not build, the preview says so and
# the generated install script is printed instead -- it is the same rule, one
# step earlier.
PREVIEW="$PWD/.scratch/t54-install-preview"
rm -rf "$PREVIEW"
rm -rf "$PREVIEW"
if cmake --install native/build-nmake --prefix "$(cygpath -w "$PREVIEW")" > "$LOGDIR/install-preview.log" 2>&1; then
  echo "INSTALL-PREVIEW=PASS"
  ( cd "$PREVIEW" && find . -type f -printf '%P\n' | LC_ALL=C sort | tee "$LOGDIR/install-preview-files.txt" | sed 's/^/    /' )
else
  echo "INSTALL-PREVIEW=NOT-ATTEMPTED-COMPLETELY (see $LOGDIR/install-preview.log)"
  tail -6 "$LOGDIR/install-preview.log"
  echo "--- the same rule, from the generated install script ---"
  grep -nE 'webview2/(LICENSE|NOTICE)' native/build-nmake/cmake_install.cmake | head -4 || echo "(no licence rule in the generated script)"
fi

echo
echo "=== PROBE $(date '+%H:%M:%S') ==="
./native/build-nmake/logstf_viewer_probe.exe > "$LOGDIR/probe.txt" 2>&1
echo "PROBE_RC=$?"
tail -4 "$LOGDIR/probe.txt"

echo
echo "=== GATE SELF-CHECK $(date '+%H:%M:%S') ==="
PYTHON="C:/Users/Administrator/.workbuddy-ai/binaries/python/versions/3.13.12/python.exe"
"$PYTHON" gate-self-check.py --gate logstf-check.sh \
  --readings native/tools/logstf_viewer_probe.cpp --sources native/src 2>&1 | tee "$LOGDIR/self-check.log"
echo "SELFCHECK_RC=${PIPESTATUS[0]} (0 = every asserted key has a writer; this is a"
echo "  static check of the gate, not of the product -- it runs before the gate)"

echo
echo "=== FORWARD GATE $(date '+%H:%M:%S') ==="
bash logstf-check.sh 2>&1 | tee "$LOGDIR/forward.log"
FWD_RC=${PIPESTATUS[0]}
echo "FORWARD_RC=$FWD_RC"

# Diagnostics: whatever the verdict, print what a human needs to see why. A gate
# run costs a host-lock slot, so the log has to answer "what did the panel
# actually do" without a second acquisition.
echo
echo "=== FORWARD FAIL LINES ==="
grep -E "^  FAIL" "$LOGDIR/forward.log" | head -40 || echo "(none)"
echo "=== FORWARD SECTIONS ==="
grep -E "^=== " "$LOGDIR/forward.log" || true
echo "=== LOGSTF TRACE LINES (per run) ==="
for t in .scratch/logstf/*.trace; do
  [ -f "$t" ] || continue
  echo "-- $t"
  grep -E "logstf: " "$t" | head -8
done
echo "=== PANEL STATE / RUNTIME LINES ==="
grep -hE "logstf: (runtime|panel|summary|nav)" .scratch/logstf/*.trace 2>/dev/null | sort -u | head -20 || true

# T54 changed the package list, so the T32/T37 readiness gate owns constants that
# were also made stale by this branch. Its total (7 files -> 11) is knowledge a
# different task spent a day pinning, and knowledge that is executable must be
# EXECUTED, not recomputed: this runs it on the pristine tree, before the
# mutation suite (which temporarily edits sources). Its verdict is reported on its
# own line -- it is T32/T37's gate, not T54's, and "T54 green" must not be read as
# "readiness green".
READY_RC=99
if [ "$FWD_RC" -eq 0 ]; then
  echo
  echo "=== READINESS GATE (install-readiness-check.sh, T32/T37) $(date '+%H:%M:%S') ==="
  rm -rf "$PWD/.scratch/t54-readiness"
  bash install-readiness-check.sh --forward "$PWD/.scratch/t54-readiness" 2>&1 | tee "$LOGDIR/readiness.log" | tail -40
  READY_RC=${PIPESTATUS[0]}
  echo "READINESS_RC=$READY_RC"
else
  echo "READINESS=SKIPPED (the forward gate is red; the package list is not what this run measures)"
fi

if [ "$FWD_RC" -eq 0 ]; then
  echo
  echo "=== MUTATION SUITE $(date '+%H:%M:%S') ==="
  bash logstf-check.sh --mutation 2>&1 | tee "$LOGDIR/mutation.log"
  MUT_RC=${PIPESTATUS[0]}
  echo "MUTATION_RC=$MUT_RC"
else
  MUT_RC=99
  echo "MUTATION skipped: the forward gate is red, and a mutation scored against a"
  echo "red baseline proves nothing (the suite itself refuses in this case)."
fi

echo
sha256sum $SRCS > "$LOGDIR/sources-after.sha256"
if cmp -s "$LOGDIR/sources-before.sha256" "$LOGDIR/sources-after.sha256"; then
  echo "SOURCES-RESTORED=PASS (every mutated source is byte-identical to before the run)"
else
  echo "SOURCES-RESTORED=FAIL"
  diff "$LOGDIR/sources-before.sha256" "$LOGDIR/sources-after.sha256"
fi
git status --porcelain | head -20
mkdir -p evidence/logstf/traces
cp .scratch/logstf/*.trace evidence/logstf/traces/ 2>/dev/null || true
cp "$LOGDIR"/*.log "$LOGDIR"/*.sha256 "$LOGDIR"/probe.txt "$LOGDIR"/install-preview-files.txt evidence/logstf/ 2>/dev/null
( cd evidence/logstf && sha256sum *.trace > traces.sha256 2>/dev/null ) || true
echo "T54-RUN-DONE build=$BUILD_RC forward=$FWD_RC mutation=$MUT_RC readiness=$READY_RC logdir=$LOGDIR $(date '+%H:%M:%S')"
