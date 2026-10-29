set -u
RUN="$1"
cd /d/TF2_Native_Worktrees/candidate-final || exit 9
echo "== build under the lock: the forward phase does not build, so this is the only"
echo "   thing that makes the exe describe the sources below ($(date -Is))"
bash build-target.sh tf2_demo_native entity_overlay_probe > "$RUN/build.log" 2>&1
BRC=$?
echo "BUILD_RC=$BRC"
if [ "$BRC" != "0" ]; then
  tail -12 "$RUN/build.log"
  echo "RUN_ABORT=build-failed"
  exit 90
fi
{
  echo "== post-build ($(date -Is))"
  echo "exe_sha256:"
  sha256sum native/build-nmake/tf2_demo_native.exe native/build-nmake/entity_overlay_probe.exe 2>/dev/null
  echo "exe_mtime:"
  ls -l --time-style=full-iso native/build-nmake/tf2_demo_native.exe native/build-nmake/entity_overlay_probe.exe 2>/dev/null
  echo "sources_newer_than_exe (must be empty):"
  find native/src native/include native/tools -type f \( -name '*.cpp' -o -name '*.h' \) -newer native/build-nmake/tf2_demo_native.exe 2>/dev/null
} > "$RUN/post-build.txt"
cat "$RUN/post-build.txt"
echo "== gate ($(date -Is))"
bash outline-check.sh --mutation 2>&1 | tee "$RUN/outline-mutation.log"
exit ${PIPESTATUS[0]}
