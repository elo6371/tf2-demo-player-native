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
# CROSS-RUNTIME PIDS (D72, 2026-11-16). The pid a lock records is $$, and $$ only
# means something inside the MSYS runtime that wrote it. A Git Bash holder writes its
# own pid (e.g. 339); a niubash checker runs kill -0 339, gets "no such process", and
# reclaimed a lock whose owner was alive and working -- three times in one night, at
# ages of 3 s, 18 s and 22 s. Two defences, in this order:
#   1. the lock also records winpid (the Windows pid) and liveness is checked in that
#      pid space too. tasklist exits 0 for a pid that is not running (it prints an INFO
#      line instead), so the decision is the CSV PID column, never the exit code;
#   2. a heartbeat younger than the grace window proves the owner heartbeat child is
#      still running, which proves the owner is alive, with no pid resolution at all.
#      That child refreshes only while it still owns this directory (nonce match), and
#      when it exits because its owner died it stamps $d/gone -- so a dead owner is
#      still reclaimed immediately instead of waiting out the grace window.
#
#
# Usage (sourced):  . ./verify-lock.sh; vlock_acquire DIR OWNER [STALE_AFTER]
#                   vlock_release DIR
VLOCK_STALE_DEFAULT=900
VLOCK_HEARTBEAT_SECONDS="${VLOCK_HEARTBEAT_SECONDS:-30}"
# D72: a heartbeat younger than this proves the owner heartbeat child is still running,
# and that child stops the moment its owner dies. Three intervals, with a 60 s floor.
VLOCK_HEARTBEAT_GRACE="${VLOCK_HEARTBEAT_GRACE:-$(( VLOCK_HEARTBEAT_SECONDS * 3 ))}"
[ "$VLOCK_HEARTBEAT_GRACE" -ge 60 ] 2>/dev/null || VLOCK_HEARTBEAT_GRACE=60

vlock_now() { date +%s; }

vlock_pid_alive() { # vlock_pid_alive <pid>
  local p="$1"
  [ -n "$p" ] || return 1
  case "$p" in ''|*[!0-9]*) return 1;; esac
  kill -0 "$p" 2>/dev/null
}

vlock_winpid() { # vlock_winpid -- the Windows pid of THIS shell; /proc/$$/winpid where the runtime has it
  local w
  w=$(cat /proc/$$/winpid 2>/dev/null || true)
  case "$w" in ''|*[!0-9]*) printf '%s' "$$"; return 0;; esac
  printf '%s' "$w"
}

vlock_win_pid_alive() { # vlock_win_pid_alive <windows-pid> -- the Windows pid space, whichever shell asks
  local p="$1" out
  case "$p" in ''|*[!0-9]*) return 1;; esac
  command -v tasklist >/dev/null 2>&1 || return 1
  out=$(MSYS_NO_PATHCONV=1 tasklist /FI "PID eq $p" /FO CSV /NH 2>/dev/null) || return 1
  printf '%s' "$out" | awk -F, -v p="$p" '{ v=$2; gsub(/"/,"",v); if (v==p) f=1 } END { exit !f }'
}

vlock_alive_pid() { # vlock_alive_pid <pid> -- kill -0 where the pid space matches, tasklist where it does not
  local p="$1"
  vlock_pid_alive "$p" && return 0
  vlock_win_pid_alive "$p"
}

vlock_lock_alive() { # vlock_lock_alive <dir> -- is the owner recorded in <dir> still running?
  local d="$1" p w age
  p=$(cat "$d/pid" 2>/dev/null || printf '')
  w=$(cat "$d/winpid" 2>/dev/null || printf '')
  vlock_pid_alive "$p" && return 0
  [ -n "$w" ] && [ "$w" != "$p" ] && vlock_alive_pid "$w" && return 0
  # Neither pid resolved here. A heartbeat this fresh can only have been written by the
  # owner heartbeat child, and that child stamps $d/gone the moment its owner dies -- so a
  # fresh heartbeat with no gone stamp is proof of a live owner (D72 defence 2).
  if [ -f "$d/owner" ] && [ -f "$d/heartbeat" ] && [ ! -f "$d/gone" ]; then
    age=$(vlock_age "$d")
    [ "$age" -le "$VLOCK_HEARTBEAT_GRACE" ] && return 0
  fi
  return 1
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

vlock_heartbeat_start() { # vlock_heartbeat_start <dir> <owner-winpid> <nonce>
  local d="$1" owner="$2" nonce="$3"
  (
    exec </dev/null >/dev/null 2>&1
    while vlock_alive_pid "$owner"; do
      # An orphan child left over from an earlier lock must never refresh (or stamp) this one.
      [ "$(cat "$d/nonce" 2>/dev/null)" = "$nonce" ] || exit 0
      date +%s > "$d/heartbeat" 2>/dev/null || exit 0
      sleep "$VLOCK_HEARTBEAT_SECONDS"
    done
    # The owner is gone: say so, rather than leave a heartbeat that still looks fresh.
    [ "$(cat "$d/nonce" 2>/dev/null)" = "$nonce" ] && date +%s > "$d/gone" 2>/dev/null
    exit 0
  ) &
  printf '%s\n' "$!" > "$d/heartbeat.pid"
}

vlock_init() { # vlock_init <dir> <owner>
  local d="$1" owner="$2" win nonce
  win=$(vlock_winpid)
  nonce="$$-${RANDOM}${RANDOM}${RANDOM}"
  printf '%s\n' "$owner" > "$d/owner"
  printf '%s\n' "$$" > "$d/pid"       # this runtime pid -- what release still compares
  printf '%s\n' "$win" > "$d/winpid"  # the Windows pid -- what another runtime checks
  printf '%s\n' "$nonce" > "$d/nonce"
  date +%s > "$d/started"
  date +%s > "$d/heartbeat"
  vlock_heartbeat_start "$d" "$win" "$nonce"
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
  vlock_lock_alive "$d" && alive=yes
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