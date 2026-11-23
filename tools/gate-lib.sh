#!/usr/bin/env bash
# tools/gate-lib.sh -- the helpers every gate keeps re-writing, written once.
#
# Source it from a gate:   . "$(dirname "$0")/tools/gate-lib.sh"
# It defines functions only: no `set`, no `cd`, no traps, no output on load.
#
# Why it exists (docs/GATE-FIXES-2026-11-22.md): the same three defects were
# found in 16 gates on 2026-11-22, each one fixed in one gate and never in its
# siblings. tools/gate-lint.sh now refuses the defective shapes; this file is the
# correct shape to use instead.
#
#   1. gl_restore <path> <backup>    content restore that also restores BUILD
#                                    state (D-101): fresh mtime when the bytes
#                                    changed, untouched when they did not.
#   2. gl_has_line <file> <ere>      whole-line match that tolerates a trailing
#                                    CR (Windows probes write CRLF).
#   3. gl_same_lines <a> <b> [out]   compare two files after CR stripping, via
#                                    real files only -- never <(...), because
#                                    diff/comm/paste here are the WinuxCmd shim,
#                                    which cannot open /dev/fd/N.
#   4. gl_mutant_cond <expr>         print an always-false C++ condition for a
#                                    mutation that /W4 /WX accepts (not `false &&`,
#                                    which is C4127 -> C2220).
#
# Locks are NOT here: verify-lock.sh (vlock_acquire / vlock_release) already does
# owner, pid, heartbeat and stale reclaim, and tools/lockrun.sh wraps it.

# gl_restore <path> <backup> -> 0 restored (or already identical), 1 failed.
# Prints RESTORE-OK / RESTORE-FAIL lines in the format the gates already grep.
gl_restore() {
  local path="$1" backup="$2"
  if [ ! -f "$backup" ]; then
    echo "RESTORE-FAIL path=$path (no backup at $backup)"
    return 1
  fi
  if cmp -s "$backup" "$path"; then
    echo "RESTORE-OK path=$path (already identical)"
    return 0
  fi
  # No -p: the pre-run mtime is older than the mutated .obj and NMake would skip
  # the recompile. touch makes it newer even when copy and build share a second.
  if cp "$backup" "$path" && touch "$path" && cmp -s "$backup" "$path"; then
    echo "RESTORE-OK path=$path (content restored, mtime refreshed)"
    return 0
  fi
  echo "RESTORE-FAIL path=$path"
  return 1
}

# gl_backup <path> <backup> -> 0 ok. `cp -p` is right HERE: the backup is a
# content snapshot for cmp, and gl_restore never copies its timestamp back.
gl_backup() {
  cp -p "$1" "$2" && cmp -s "$1" "$2"
}

# gl_has_line <file> <ere-without-anchors> -> 0 when some line is exactly the
# pattern, optionally followed by one CR.
gl_has_line() {
  grep -qE "^($2)"$'\r'"?\$" "$1"
}

# gl_strip_cr <in> <out>
gl_strip_cr() {
  tr -d '\r' < "$1" > "$2"
}

# gl_same_lines <a> <b> [diff-out] -> 0 identical after CR stripping, 1 differ,
# 2 an input is missing. With diff-out, the diff (CR-stripped) is written there.
gl_same_lines() {
  local a="$1" b="$2" out="${3:-}" tmp rc
  [ -f "$a" ] && [ -f "$b" ] || return 2
  tmp=$(mktemp -d) || return 2
  gl_strip_cr "$a" "$tmp/a"
  gl_strip_cr "$b" "$tmp/b"
  if cmp -s "$tmp/a" "$tmp/b"; then
    rc=0
  else
    rc=1
    [ -n "$out" ] && diff "$tmp/a" "$tmp/b" > "$out" 2>&1
  fi
  rm -rf "$tmp"
  return "$rc"
}

# gl_mutant_cond <expr> -> prints  !(expr) && (expr)
# Always false at run time, not a constant expression, so no C4127.
gl_mutant_cond() {
  printf '!(%s) && (%s)' "$1" "$1"
}
