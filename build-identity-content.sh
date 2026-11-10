#!/usr/bin/env bash
# build-identity-content.sh -- the content hash of the compiled inputs, computed
# independently of the builder.
#
# Why a second implementation exists (D-54, 2026-11-10)
# -----------------------------------------------------
# native/cmake/gen-build-stamp.cmake writes one hash of the files the two
# executables compile and configure from, and the executables carry it as a
# compiled-in constant. An assertion that compares the builder's hash with
# itself proves only that the builder is deterministic. This script recomputes
# the same digest from a different source of truth -- the git index plus git's
# own blob normalisation, or the plain bytes when git is unavailable -- so that
# the gate can compare TWO measurements instead of one.
#
#   bash build-identity-content.sh hash     # 64 lowercase hex, or "unknown"
#   bash build-identity-content.sh method   # git-clean-v1 | raw-v1 | unavailable
#
# git-clean-v1 (what the builder reports when git is present):
#   * inputs: native/CMakeLists.txt, native/src, native/include,
#     native/third_party, native/cmake -- every tracked file the product and the
#     probe compile or configure from. native/generated is a build product and
#     native/tools belongs to other targets; neither is an input here.
#   * one line per file, "<mode> <blob> <path>", path relative to native/, mode
#     and blob from the git index, blob recomputed from the working-copy content
#     the way "git add" would (a modified file is hashed as it lies on disk).
#   * a tracked file that is missing from the working copy is skipped: it is not
#     part of the bytes that will be built.
#   * lines sorted with LC_ALL=C, joined with newlines, one trailing newline, and
#     the sha256 of that text is the content hash.
# raw-v1: the same roots, hashed as raw bytes ("<sha256> <path>" lines), for a
#   tree without git. The two methods are NOT comparable, and contentMethod is the
#   only thing in the output that says which one you are looking at.
#
# Exit: 0 = a hash was produced, 3 = no hash possible (prints "unknown"), 2 = usage.
set -uo pipefail

REPO="$(cd "$(dirname "$0")" && pwd)"
NATIVE="$REPO/native"
ROOTS=(CMakeLists.txt src include third_party cmake)

usage() { echo "usage: bash build-identity-content.sh hash|method" >&2; exit 2; }
WANT="${1-}"
case "$WANT" in hash|method) ;; *) usage ;; esac

method="unavailable"
hash="unknown"

if command -v git >/dev/null 2>&1 && git -C "$NATIVE" rev-parse --git-dir >/dev/null 2>&1; then
  listing="$(git -C "$NATIVE" ls-files -s -- "${ROOTS[@]}" 2>/dev/null)"
  if [ -n "$listing" ]; then
    modes=(); paths=()
    while IFS= read -r record; do
      [ -n "$record" ] || continue
      meta="${record%%	*}"
      path="${record#*	}"
      [ "$meta" = "$record" ] && continue
      [ -f "$NATIVE/$path" ] || continue
      modes+=("${meta%% *}")
      paths+=("$path")
    done <<< "$listing"
    if [ "${#paths[@]}" -gt 0 ]; then
      if blobs="$(git -C "$NATIVE" hash-object -- "${paths[@]}" 2>/dev/null)"; then
        mapfile -t blob_list <<< "$blobs"
        if [ "${#blob_list[@]}" -eq "${#paths[@]}" ]; then
          lines=()
          i=0
          while [ "$i" -lt "${#paths[@]}" ]; do
            lines+=("${modes[$i]} ${blob_list[$i]} ${paths[$i]}")
            i=$((i + 1))
          done
          text="$(printf '%s\n' "${lines[@]}" | LC_ALL=C sort)"
          hash="$(printf '%s\n' "$text" | sha256sum | cut -d' ' -f1)"
          method="git-clean-v1"
        fi
      fi
    fi
  fi
fi

if [ "$method" = "unavailable" ]; then
  # raw-v1: no git, no index. Byte-for-byte the same roots, no normalisation.
  files=()
  for root in "${ROOTS[@]}"; do
    if [ -d "$NATIVE/$root" ]; then
      while IFS= read -r f; do
        files+=("${f#"$NATIVE/"}")
      done < <(find "$NATIVE/$root" -type f -print | LC_ALL=C sort)
    elif [ -f "$NATIVE/$root" ]; then
      files+=("$root")
    fi
  done
  if [ "${#files[@]}" -gt 0 ]; then
    lines=()
    for f in "${files[@]}"; do
      lines+=("$(sha256sum "$NATIVE/$f" | cut -d' ' -f1) $f")
    done
    text="$(printf '%s\n' "${lines[@]}" | LC_ALL=C sort)"
    hash="$(printf '%s\n' "$text" | sha256sum | cut -d' ' -f1)"
    method="raw-v1"
  fi
fi

if [ "$WANT" = "method" ]; then
  echo "$method"
else
  echo "$hash"
fi
[ "$method" = "unavailable" ] && exit 3
exit 0
