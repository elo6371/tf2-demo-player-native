#!/usr/bin/env bash
# verify-lock.sh -- the self-healing mkdir lock verify-all.sh runs under.
#
# WHY (2026-11-08): the old lock was "mkdir $LOCK_DIR" plus an EXIT trap. A run
# killed with SIGKILL (or a crashed terminal) left the directory behind with no
# way to tell a live owner from a corpse, and every later run refused with rc=2
# forever. 22:56 today: an empty .scratch/verify-all.lock with no owner file made
# mutate.sh exit 2 while product_processes was 0.
#
# WHAT IT DOES
#   - the lock directory carries owner, pid, started and heartbeat (epoch seconds);
#   - a heartbeat child refreshes heartbeat every 30 s and exits when the owner
#     pid disappears (a SIGKILLed owner therefore stops refreshing, instead of
#     leaving a lock that looks alive forever);
#   - a lock whose pid is gone, or whose heartbeat is older than the limit
#     (default 900 s), is reclaimed:
#       STALE-LOCK=RECLAIMED owner=<old> pid=<old> age=<n>s reason=<...>
#   - a lock with a live pid and a fresh heartbeat is refused:
#       VERIFY-LOCK=FAIL owner=<old> pid=<old> age=<n>s heartbeat_limit=<n>s  (rc=2)
#   - release only removes the directory when this process still owns it.
#
# SIGKILL CANNOT BE TRAPPED. That is the point of the pid+heartbeat check: the
# next run does the cleanup the dead process could not do. If a pid was reused by
# an unrelated live process, the heartbeat limit still expires (max 900 s).
#
# Usage (sourced):  . ./verify-lock.sh; vlock_acquire DIR OWNER [STALE_AFTER]
#                   vlock_release DIR
VLOCK_STALE_DEFAULT=900
VLOCK_HEARTBEAT_SECONDS="${VLOCK_HEARTBEAT_SECONDS:-30}"

vlock_now() { date +%s; }

vlock_pid_alive() { # vlock_pid_alive <pid>
  local p="$1"
  [ -n "$p" ] || return 1
  case "$p" in ''|*[!0-9]*) return 1;; esac
  kill -0 "$p" 2>/dev/null
}

vlock_age() { # vlock_age <dir> -- seconds since heartbeat, else started, else dir mtime
  local d="$1" f t
  for f in "$d/heartbeat" "$d/started"; do
    [ -f "$f" ] || continue
    t=$(cat "$f" 2>/dev/null || true)
    case "$t" in ''|*[!0-9]*) continue;; esac
    printf '%s' "$(( $(vlock_now) - t ))"
    return 0
  done
  t=$(stat -c %Y "$d" 2>/dev/null || printf '0')
  printf '%s' "$(( $(vlock_now) - t ))"
}

vlock_heartbeat_start() { # vlock_heartbeat_start <dir> <owner-pid>
  local d="$1" owner="$2"
  (
    exec </dev/null >/dev/null 2>&1
    while vlock_pid_alive "$owner"; do
      date +%s > "$d/heartbeat" 2>/dev/null || exit 0
      sleep "$VLOCK_HEARTBEAT_SECONDS"
    done
    exit 0
  ) &
  printf '%s\n' "$!" > "$d/heartbeat.pid"
}

vlock_init() { # vlock_init <dir> <owner>
  local d="$1" owner="$2"
  printf '%s\n' "$owner" > "$d/owner"
  printf '%s\n' "$$" > "$d/pid"
  date +%s > "$d/started"
  date +%s > "$d/heartbeat"
  vlock_heartbeat_start "$d" "$$"
}

vlock_acquire() { # vlock_acquire <dir> <owner> [stale-after-seconds]
  local d="$1" owner="$2" limit="${3:-$VLOCK_STALE_DEFAULT}"
  mkdir -p "$(dirname "$d")" 2>/dev/null || true
  if mkdir "$d" 2>/dev/null; then
    vlock_init "$d" "$owner"
    echo "VERIFY-LOCK=ACQUIRED owner=$owner pid=$$ dir=$d stale_after=${limit}s"
    return 0
  fi
  local old_owner old_pid age alive reason
  old_owner=$(cat "$d/owner" 2>/dev/null || printf 'unknown')
  old_pid=$(cat "$d/pid" 2>/dev/null || printf '')
  age=$(vlock_age "$d")
  alive=no
  vlock_pid_alive "$old_pid" && alive=yes
  if [ "$alive" = yes ] && [ "$age" -le "$limit" ] && [ -f "$d/pid" ]; then
    echo "VERIFY-LOCK=FAIL (another verify-all run owns $d: owner=$old_owner pid=$old_pid age=${age}s heartbeat_limit=${limit}s)"
    return 2
  fi
  if [ "$alive" = yes ]; then reason=heartbeat-timeout
  elif [ -f "$d/pid" ]; then reason=owner-dead
  else reason=no-owner-file
  fi
  echo "STALE-LOCK=RECLAIMED owner=$old_owner pid=${old_pid:-none} age=${age}s reason=$reason dir=$d"
  rm -rf "$d"
  if ! mkdir "$d" 2>/dev/null; then
    echo "VERIFY-LOCK=FAIL (stale lock was reclaimed but $d came back; another run won the race)"
    return 2
  fi
  vlock_init "$d" "$owner"
  echo "VERIFY-LOCK=ACQUIRED owner=$owner pid=$$ dir=$d stale_after=${limit}s"
  return 0
}

vlock_release() { # vlock_release <dir>
  local d="$1" p hb
  [ -d "$d" ] || return 0
  p=$(cat "$d/pid" 2>/dev/null || printf '')
  if [ "$p" != "$$" ]; then
    echo "VERIFY-LOCK=RELEASE-SKIPPED dir=$d (owned by pid ${p:-unknown}, not $$)"
    return 0
  fi
  hb=$(cat "$d/heartbeat.pid" 2>/dev/null || printf '')
  vlock_pid_alive "$hb" && kill "$hb" 2>/dev/null
  rm -rf "$d"
  echo "VERIFY-LOCK=RELEASED dir=$d"
}