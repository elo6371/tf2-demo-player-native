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
