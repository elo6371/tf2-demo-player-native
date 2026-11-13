#!/usr/bin/env bash
# verify-iso.sh -- the EXIT-trap chain shared by the gates that run the product
# with an isolated settings directory.
#
# The gates used to install this inline:
#   ISO_PREV_TRAP=$(trap -p EXIT); ISO_PREV_TRAP=${ISO_PREV_TRAP#trap -- }; ISO_PREV_TRAP=${ISO_PREV_TRAP% EXIT}
#   trap "$ISO_PREV_TRAP; rm -rf \"$ISO_DIR\"" EXIT
# 'trap -p' prints the action as a quoted shell word (trap -- 'cmd' EXIT), so the
# leftover quotes made the whole action a single command name at exit time
# ("line 1: rm -rf ...: command not found"): the previous action never ran, the
# lock and the isolated directory leaked, and a pending mutation restore could be
# skipped. A failure that reads like success, so the chaining lives here and has
# a test (iso-trap-selftest.sh).
#
# Usage, after ISO_DIR exists:
#   . "$(dirname "$0")/verify-iso.sh"
#   iso_chain_exit "$ISO_DIR"

iso_chain_exit() { # iso_chain_exit <directory to remove on exit>
  local dir="${1:?iso_chain_exit needs a directory}"
  local prev tail
  prev=$(trap -p EXIT)
  tail="rm -rf \"$dir\""
  if [ -n "$prev" ]; then
    prev=${prev#trap -- }
    prev=${prev% EXIT}
    # Undo only the outer quoting 'trap -p' added. The shell re-parses the
    # action when the trap runs, so keeping the quotes is what broke it.
    case "$prev" in
      \'*\') prev=${prev#\'}; prev=${prev%\'} ;;
    esac
    trap "$prev; $tail" EXIT
  else
    trap "$tail" EXIT
  fi
}

# The renderer configuration a gate runs with is a file in this repository, not
# whatever the last person to open the player happened to save under
# %LOCALAPPDATA%. An EMPTY settings.ini is not "the defaults": loadSettings()
# reads `preset` with a 0 fallback (native/src/settings_store.cpp:56-58), 0 means
# Performance, and Performance is the preset that switches the sky pass off
# (native/src/settings_store.cpp:82, native/src/main.cpp:6866-6868). A gate that
# measured against an empty directory therefore rendered a different world than
# the frames it pins -- measured on the T26 pair in verify-all RUN4 (2026-11-08):
# 113,159 pixels of clear colour, trace reason "skybox draw disabled by the
# caller" against the pinned frame's "world sky state=active". The fixture is
# written out in full on purpose: a key that falls back to the compiled default
# moves when the code moves, and the pin would move without recording why.
iso_fixture_settings() {
  local here
  here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
  printf '%s/native/tests/fixtures/gate-settings.ini' "$here"
}

# The copy the product will actually read must be the fixture the identity
# records. The capture identity hashes the FIXTURE, not this copy, so a stale
# or hand-edited copy would make the identity claim configuration X while the
# renderer runs configuration Y -- the identity lying, which this suite forbids
# everywhere else. Fail closed instead of silently rendering the wrong world.
iso_assert_gate_settings() { # iso_assert_gate_settings <directory>
  local dir="${1:?iso_assert_gate_settings needs a directory}"
  local fixture want got
  fixture=$(iso_fixture_settings)
  if [ ! -f "$fixture" ]; then
    echo "FATAL: the gate settings fixture is missing: $fixture" >&2
    return 1
  fi
  if [ ! -f "$dir/settings.ini" ]; then
    echo "FATAL: $dir/settings.ini is missing; the product would run with no renderer configuration" >&2
    return 1
  fi
  want=$(sha256sum "$fixture" | cut -d' ' -f1)
  got=$(sha256sum "$dir/settings.ini" | cut -d' ' -f1)
  if [ "$got" != "$want" ]; then
    echo "FATAL: $dir/settings.ini is not the gate fixture ($got != $want)" >&2
    echo "       the identity would record settings=$want while the product renders $got" >&2
    return 1
  fi
  return 0
}

# Seed a gate's throwaway settings directory with that fixture. Callers that run
# the product immediately must treat a failure as fatal: running with no
# settings file at all is exactly the bug this exists to prevent. An existing
# copy is not "already done": it is compared, and a mismatch is fatal.
iso_seed_gate_settings() { # iso_seed_gate_settings <directory>
  local dir="${1:?iso_seed_gate_settings needs a directory}"
  local fixture
  mkdir -p "$dir" || return 1
  if [ -f "$dir/settings.ini" ]; then
    # The caller seeded it (capture --validate) or an earlier run left it; the
    # bytes must match the fixture either way.
    iso_assert_gate_settings "$dir" || return 1
    return 0
  fi
  fixture=$(iso_fixture_settings)
  if [ ! -f "$fixture" ]; then
    echo "FATAL: the gate settings fixture is missing: $fixture" >&2
    return 1
  fi
  cp "$fixture" "$dir/settings.ini" || return 1
  iso_assert_gate_settings "$dir"
}
