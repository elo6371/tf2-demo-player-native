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
#   bash build-identity-content.sh method   # git-clean-v1 | git-untracked-v1 | raw-v1 | unavailable
#   bash build-identity-content.sh stats    # the D-90 input census, one line, for gates/selftests
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
# WHAT COUNTS AS AN INPUT (D-90)
#   "native/cmake, native/src, ..." is a set of ROOTS, not a manifest: the
#   compiler reads whatever unignored file is there, while the git index only
#   knows about tracked ones. This script therefore enumerates the files git does
#   not track in the same roots and refuses (exit 4) when one of them is named
#   like a compile input, because a digest that cannot name an input must not be
#   produced at all. TF2_IDENTITY_ALLOW_UNTRACKED=1 turns the refusal into a
#   warning and reports method=git-untracked-v1, which no gate accepts as
#   pinnable. native/generated is a build output and is counted, never hashed.
#
# Exit: 0 = a hash was produced, 3 = no hash possible (prints "unknown"),
#       4 = refused: untracked or ignored compile inputs are outside the digest,
#       2 = usage.
set -uo pipefail

REPO="$(cd "$(dirname "$0")" && pwd)"
NATIVE="$REPO/native"
ROOTS=(CMakeLists.txt src include third_party cmake)

usage() { echo "usage: bash build-identity-content.sh hash|method|stats" >&2; exit 2; }
WANT="${1-}"
case "$WANT" in hash|method|stats) ;; *) usage ;; esac

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

# ---------------------------------------------------------------------------
# D-90: the worktree is not the index.
#
# The git-clean-v1 digest above is built from INDEX entries. A file that is
# present but untracked has no entry, so it cannot appear in the digest while the
# compiler still reads it -- the first D-54 build hashed 53f14a19 with two
# untracked cmake files driving it. Enumerate what git does not track, classify by
# name, and refuse when a compile input is outside the digest.
#
#   untracked, not ignored  -> git would track it as soon as someone adds it and
#       the compiler reads it now: FATAL by default, degraded method under
#       TF2_IDENTITY_ALLOW_UNTRACKED=1.
#   ignored, under a root   -> .gitignore and the extension disagree: same rule.
#   untracked, not an input -> counted, not refused: it cannot change the bytes.
#   generated/              -> this generator's own output; counted, never hashed.
# ---------------------------------------------------------------------------
INPUT_EXTS=" c cc cpp cxx h hh hpp hxx inl i ii ipp tpp inc s asm glsl hlsl vert frag comp rc def lib a "

is_compile_input() { # <path relative to native/>
  local p="${1-}" name ext
  [ -n "$p" ] || return 1
  name="${p##*/}"
  [ "$name" = "CMakeLists.txt" ] && return 0
  case "$p" in *cmake/*) return 0 ;; esac
  ext="${p##*.}"
  [ "$ext" = "$p" ] && return 1
  ext="$(printf "%s" "$ext" | tr "A-Z" "a-z")"
  case "$INPUT_EXTS" in *" $ext "*) return 0 ;; esac
  return 1
}

untracked_inputs=()
untracked_other=0
ignored_inputs=()
ignored_other=0
generated_count=0
if [ -d "$NATIVE/generated" ]; then
  generated_count="$(find "$NATIVE/generated" -type f -print | wc -l | tr -d " ")"
fi

if [ "$method" = "git-clean-v1" ]; then
  if ! listing="$(git -C "$NATIVE" ls-files --others --exclude-standard -- "${ROOTS[@]}" 2>/dev/null)"; then
    echo "build-identity-content.sh: D-90 cannot enumerate untracked files; the digest cannot be shown to cover every input" >&2
    exit 4
  fi
  while IFS= read -r p; do
    [ -n "$p" ] || continue
    if is_compile_input "$p"; then untracked_inputs+=("$p"); else untracked_other=$((untracked_other + 1)); fi
  done <<< "$listing"

  if ! listing="$(git -C "$NATIVE" ls-files --others --ignored --exclude-standard -- "${ROOTS[@]}" 2>/dev/null)"; then
    echo "build-identity-content.sh: D-90 cannot enumerate ignored files; the digest cannot be shown to cover every input" >&2
    exit 4
  fi
  while IFS= read -r p; do
    [ -n "$p" ] || continue
    if is_compile_input "$p"; then ignored_inputs+=("$p"); else ignored_other=$((ignored_other + 1)); fi
  done <<< "$listing"

  allow="${TF2_IDENTITY_ALLOW_UNTRACKED-}"
  # stats is a diagnostic query: it reports the census even when the answer is
  # "this digest would be incomplete", so a gate can print WHICH files are outside.
  if [ "$WANT" != "stats" ] && [ "${#untracked_inputs[@]}" -gt 0 ]; then
    if [ "$allow" = "1" ]; then
      {
        echo "build-identity-content.sh: D-90 OVERRIDDEN by TF2_IDENTITY_ALLOW_UNTRACKED=1."
        echo "  These compile inputs are untracked and are NOT in the digest:"
        printf "    %s\n" "${untracked_inputs[@]}"
        echo "  method is reported as git-untracked-v1; no gate accepts it as pinnable."
      } >&2
      method="git-untracked-v1"
    else
      {
        echo "build-identity-content.sh: D-90 REFUSED -- untracked compile inputs are not in the content hash:"
        printf "    %s\n" "${untracked_inputs[@]}"
        echo "  Add them to the index (git add), remove them, or build deliberately incomplete with"
        echo "  TF2_IDENTITY_ALLOW_UNTRACKED=1 (method becomes git-untracked-v1, not pinnable)."
      } >&2
      exit 4
    fi
  fi

  if [ "$WANT" != "stats" ] && [ "${#ignored_inputs[@]}" -gt 0 ]; then
    if [ "$allow" = "1" ]; then
      {
        echo "build-identity-content.sh: D-90 OVERRIDDEN by TF2_IDENTITY_ALLOW_UNTRACKED=1."
        echo "  These files are ignored yet named like compile inputs, and are NOT in the digest:"
        printf "    %s\n" "${ignored_inputs[@]}"
      } >&2
      method="git-untracked-v1"
    else
      {
        echo "build-identity-content.sh: D-90 REFUSED -- ignored compile inputs are not in the content hash:"
        printf "    %s\n" "${ignored_inputs[@]}"
        echo "  .gitignore and the extension disagree about whether these are source; refusing to guess."
      } >&2
      exit 4
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

if [ "$WANT" = "stats" ]; then
  echo "method=$method untrackedInputs=${#untracked_inputs[@]} untrackedOther=$untracked_other ignoredInputs=${#ignored_inputs[@]} ignoredOther=$ignored_other generated=$generated_count"
elif [ "$WANT" = "method" ]; then
  echo "$method"
else
  echo "$hash"
fi
[ "$method" = "unavailable" ] && exit 3
exit 0
