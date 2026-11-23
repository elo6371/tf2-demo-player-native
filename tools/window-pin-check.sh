#!/usr/bin/env bash
# window-pin-check.sh -- every script that launches the product exports the window
# size its pixel pins were measured at.
#
# Why (W1, 2026-11-23)
# --------------------
# The product's default window moves from 1280x720 to 1920x1080 (fitted to the work
# area). Every pixel pin, panel rectangle and frame hash in the gates was measured at
# 1280x720 -- client 1264x681 -- and only five gates ever said so with --window-size;
# the rest inherited it from the default. So each launching script now carries
#
#   export TF2_NATIVE_WINDOW_SIZE="${TF2_NATIVE_WINDOW_SIZE:-1280x720}"
#
# which the product reads when --window-size is absent. This check is what keeps a
# NEW gate from silently inheriting 1920x1080 instead.
#
# Rule: a *.sh at the repo root, under tools/ or under native/tools/ that names the
# product and passes a product-only switch (--capture-frame, --hidden-window,
# --ui-settings-file, --capture-script, --metrics-file) must contain that export at
# the start of a line. evidence/ is history and is not scanned.
#
# Written without mapfile/while-read over file text (tools/gate-lint.sh, 2026-11-22:
# that shape executed file contents on the user's host). Only file NAMES go through
# the shell; contents are read by grep.
#
# Usage: bash tools/window-pin-check.sh
# Token: WINDOW-PIN=PASS checked=N allowed=N | WINDOW-PIN=FAIL missing=N checked=N
set -uo pipefail
cd "$(dirname "$0")/.." || exit 2

# Static scanners whose launch lines are fixture TEXT, not launches. Each entry is a
# file name relative to the repo root.
ALLOW="gate-hygiene-selftest.sh gate-launch-hygiene-check.sh"
# Measured 2026-11-23 after W1's gate edit: 80 launching scripts matched the rule. A
# glob change that stopped finding them would otherwise read exactly like a pass.
FLOOR=80

LAUNCH_FLAGS='--capture-frame|--hidden-window|--ui-settings-file|--capture-script|--metrics-file'
# This check names the product and the product-only switches in its own text, so it
# matches its own rule; it cannot be its own subject, and is not a launcher.
SELF=tools/window-pin-check.sh
cands=$(grep -l -e 'tf2_demo_native' -- *.sh tools/*.sh tools/*/*.sh tools/*/*/*.sh native/tools/*.sh 2>/dev/null | sort -u | grep -v -x -e "$SELF")

checked=0
missing=0
allowed=0
for f in $cands; do
  grep -qE -e "$LAUNCH_FLAGS" -- "$f" || continue
  case " $ALLOW " in
    *" $f "*)
      allowed=$((allowed + 1))
      echo "  ALLOW   $f (static scanner: its launch lines are fixture text)"
      continue
      ;;
  esac
  checked=$((checked + 1))
  if grep -q -e '^export TF2_NATIVE_WINDOW_SIZE=' -- "$f"; then
    :
  else
    missing=$((missing + 1))
    echo "  MISSING $f (launches the product without pinning the window size)"
  fi
done

# A stale allow entry is a finding too: it would quietly exempt a future launcher.
for a in $ALLOW; do
  if [ ! -f "$a" ]; then
    echo "  STALE   allow entry $a no longer exists"
    missing=$((missing + 1))
  fi
done

if [ "$missing" -ne 0 ]; then
  echo "WINDOW-PIN=FAIL missing=$missing checked=$checked allowed=$allowed"
  exit 1
fi
if [ "$checked" -lt "$FLOOR" ]; then
  echo "WINDOW-PIN=FAIL checked=$checked below the floor $FLOOR -- the scan stopped finding launchers"
  exit 1
fi
echo "WINDOW-PIN=PASS checked=$checked allowed=$allowed"
exit 0
