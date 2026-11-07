#!/usr/bin/env bash
# release-rebuild.sh -- rebuild every binary the capture steps trust, then prove
# the tree and those binaries agree.
#
# WHY THIS STEP EXISTS (2026-11-07 chain, round 2)
#   Step 4 (outline) failed after two seconds with
#     "native/build-nmake/tf2_demo_native.exe cannot be trusted ...
#      sources are newer than the binary: native/src/entity_model.cpp"
#   Nothing had edited that file. The mutation suite restores every case with
#   "git checkout -- native/", and a restore writes a NEW MTIME with unchanged
#   content. outline-check.sh trusts a prebuilt binary only while no source is
#   newer than it, so its stale guard fired on a tree whose bytes were correct,
#   and every capture step after it (third-person-weapon, body-appearance,
#   class-pixels, world-geometry-budget) runs that same exe without ever
#   invalidating it. One rebuild makes freshness a measured fact.
#
# WHAT IT PROVES
#   1. all four targets link from the tree as it stands (build rc=0);
#   2. no input is newer than the binary it feeds -- the exact reading that
#      failed at step 4;
#   3. a second build changes nothing, i.e. nothing in the tree still declares
#      itself newer than what was built. Point 2 alone can pass on an empty
#      source list; the no-op build is what makes it mean "settled".
#
# VERDICT: REBUILD=PASS targets=4 rebuilt=N fresh=4 noop=4 exe_sha256=<sha>
#   rebuilt counts the binaries the first build relinked. rebuilt=0 on an
#   already-current tree is a pass: the claim is about agreement, not churn.
# Exit: 0 pass, 1 fail, 2 refused (no build tree / no such target / usage).
set -uo pipefail
cd "$(dirname "$0")" || exit 2

BUILD="${TF2_BUILD_DIR:-native/build-nmake}"
TARGETS=(tf2_demo_native entity_overlay_probe entity_model_probe entity_protocol_probe)
N=${#TARGETS[@]}

if [ ! -f "$BUILD/Makefile" ]; then
  echo "REBUILD=FAIL reason=no-build-tree build=$BUILD (build or configure it first)"
  exit 2
fi

exe_of() { printf '%s/%s.exe\n' "$BUILD" "$1"; }

# The inputs each binary is allowed to depend on. Restricted per target on
# purpose: native/tools holds ~40 probe sources that no shipped binary links, so
# a blanket "native/ is newer" check would fail on files that cannot stale it.
sources_of() {
  case "$1" in
    tf2_demo_native)
      printf '%s\n' native/src native/include native/generated ;;
    *)
      printf '%s\n' "native/tools/$1.cpp" native/src native/include ;;
  esac
}

sha_of() { if [ -f "$1" ]; then sha256sum "$1" | cut -d' ' -f1; else printf 'missing\n'; fi; }

before=()
for t in "${TARGETS[@]}"; do before+=("$(sha_of "$(exe_of "$t")")"); done

echo "=== build (first pass) ==="
bash build-target.sh "${TARGETS[@]}"
rc=$?
if [ "$rc" -ne 0 ]; then
  echo "REBUILD=FAIL reason=build-failed rc=$rc targets=${TARGETS[*]} log=.scratch/build-target.log"
  exit 1
fi

after=()
rebuilt=0
i=0
for t in "${TARGETS[@]}"; do
  e=$(exe_of "$t")
  s=$(sha_of "$e")
  if [ "$s" = "missing" ]; then
    echo "REBUILD=FAIL reason=exe-not-built target=$t exe=$e"
    exit 1
  fi
  [ "$s" != "${before[$i]}" ] && rebuilt=$((rebuilt+1))
  after+=("$s")
  i=$((i+1))
done

# Freshness: the reading step 4 died on.
stale=0
i=0
for t in "${TARGETS[@]}"; do
  e=$(exe_of "$t")
  newer=""
  while IFS= read -r p; do
    [ -z "$p" ] && continue
    hit=$(find "$p" -type f -newer "$e" -print 2>/dev/null | head -5)
    [ -n "$hit" ] && newer="$newer$hit\n"
  done <<< "$(sources_of "$t")"
  if [ -n "$newer" ]; then
    echo "REBUILD-FRESH=FAIL exe=$e is older than:"
    printf "%b" "$newer" | sed 's/^/  /'
    stale=1
  fi
  echo "rebuild: target=$t exe=$e sha256=${after[$i]} bytes=$(stat -c %s "$e")"
  i=$((i+1))
done

echo "=== build (second pass, must be a no-op) ==="
bash build-target.sh "${TARGETS[@]}"
rc=$?
if [ "$rc" -ne 0 ]; then
  echo "REBUILD=FAIL reason=second-build-failed rc=$rc (the tree is not settled)"
  exit 1
fi
noop=0
i=0
for t in "${TARGETS[@]}"; do
  e=$(exe_of "$t")
  s=$(sha_of "$e")
  if [ "$s" = "${after[$i]}" ]; then
    noop=$((noop+1))
  else
    echo "REBUILD-NOOP=FAIL exe=$e changed on a second build after=${after[$i]:0:12} now=${s:0:12} -- something still declares itself out of date"
  fi
  i=$((i+1))
done

if [ "$stale" -ne 0 ]; then
  echo "REBUILD=FAIL reason=source-newer-than-binary targets=$N (see REBUILD-FRESH=FAIL above)"
  exit 1
fi
if [ "$noop" -ne "$N" ]; then
  echo "REBUILD=FAIL reason=not-idempotent noop=$noop/$N"
  exit 1
fi
echo "REBUILD=PASS targets=$N rebuilt=$rebuilt fresh=$N noop=$noop exe_sha256=$(sha_of "$(exe_of tf2_demo_native)")"