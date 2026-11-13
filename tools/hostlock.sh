#!/usr/bin/env bash
# hostlock.sh v2.2 (2026-10-20) -- serialized host access: builds, product
# launches, captures, mutation suites and audio-device runs are one-at-a-time.
#
# v1 had no queue and starved an agent. v2 added FIFO tickets; v2.1 fixes two
# defects found in the first rollout: dead tickets blocked the head (no
# heartbeat), and a reservation could not hand the lock to its owner because a
# non-reserved ticket stayed at the head. Now every waiter touches its ticket
# each poll (dead tickets are pruned after 3 min), and when the Lead has
# reserved a window the reserved owner may take the lock out of queue order
# while every other owner backs off until the reservation expires. The reserved
# owner consumes the reservation when it acquires, so the window closes itself.
#
# USAGE
#   bash hostlock.sh status
#   bash hostlock.sh run <owner> <cmd...>
#   bash hostlock.sh acquire <owner> [timeout_s]
#   bash hostlock.sh release <owner> [--force]
#   bash hostlock.sh reserve <owner> [ttl_s]     # Lead only
#   bash hostlock.sh unreserve
#   bash hostlock.sh break <owner>
set -uo pipefail
ROOT=/d/TF2_Native_Worktrees/_shared
LOCK=$ROOT/host.lock
QUEUE=$ROOT/queue
RESERVATION=$ROOT/reserved
STALE=${HOSTLOCK_STALE_SECONDS:-3600}
mkdir -p "$QUEUE"

now() { date +%s; }
# v2.3: how long since the holder last touched its heartbeat, in seconds.
# Used by status and by break so a takeover can say what it displaced.
heartbeat_age() {
  local f="$LOCK/start"
  [ -e "$f" ] || { echo 0; return 0; }
  local ts
  ts=$(stat -c %Y "$f" 2>/dev/null || echo 0)
  [ "$ts" -gt 0 ] || { echo 0; return 0; }
  echo $(( $(now) - ts ))
}
start_epoch() { if [ -f "$LOCK/start" ]; then stat -c %Y "$LOCK/start" 2>/dev/null || echo 0; else echo 0; fi; }
product_procs() {
  # v2.3: tasklist + grep -ci returned an empty string (not 0) in some shells,
  # and an empty string compares as 0 -- which reads as "nothing is running"
  # while a capture is on screen. Ask the OS through PowerShell and take the
  # first integer; if that fails, say so with a sentinel instead of a silent 0.
  local n
  n=$(powershell -NoProfile -NonInteractive -Command "@(Get-CimInstance Win32_Process -Filter \"Name='tf2_demo_native.exe'\").Count" 2>/dev/null | tr -dc '0-9')
  if [ -z "$n" ]; then printf 'unknown'; else printf '%s' "$n"; fi
}
queue_head() { ls "$QUEUE" 2>/dev/null | sort | head -1; }

reservation_owner() {
  [ -f "$RESERVATION" ] || return 0
  r_owner=""; r_until=0
  read -r r_owner r_until _ < "$RESERVATION" 2>/dev/null || true
  if [ -n "$r_owner" ] && [ "$(now)" -lt "${r_until:-0}" ]; then printf '%s' "$r_owner"; fi
}

status() {
  if [ -d "$LOCK" ]; then
    owner=$(head -1 "$LOCK/owner" 2>/dev/null | tr -d '\r')
    se=$(start_epoch); echo "LOCKED owner=${owner:-unknown} heartbeat_age=$(( $(now) - se ))s"
  else
    echo "FREE"
  fi
  echo "queue=$(ls "$QUEUE" 2>/dev/null | tr '\n' ' ')head=$(queue_head)"
  r=$(reservation_owner); echo "reservation=${r:-none}"
  pp=$(product_procs)
echo "product_processes=$pp"
if [ ! -d "$LOCK" ] && [ "$pp" != "0" ] && [ "$pp" != "unknown" ]; then
  echo "ANOMALY product_running_without_lock=1 (lock is free but tf2_demo_native.exe is alive: a capture is unprotected and a new run would share the host)"
fi
}

acquire() {
  owner=$1
  timeout=${2:-5400}
  deadline=$(( $(now) + timeout ))
  seq=$(date +%s%N)
  ticket="$QUEUE/$seq-$owner-$$"
  # v2.4 (D-52): TERM/INT while queued must cancel the wait, not just delete
  # the ticket. v2.3 kept `trap 'rm -f "$ticket"'` and the loop kept running, so
  # the next `touch "$ticket"` re-created the ticket: the caller believed it had
  # cancelled while the waiter stayed in line and could still acquire the host
  # later. The trap now only records the signal; the loop checks it before every
  # touch and again right after mkdir, then removes the ticket and returns
  # 130 (INT) / 143 (TERM).
  cancel_sig=""
  trap 'cancel_sig=INT' INT
  trap 'cancel_sig=TERM' TERM
  : > "$ticket"
  last_note=0
  waited_from=$(now)
  while :; do
    [ -n "$cancel_sig" ] && break
    touch "$ticket" 2>/dev/null
    [ -n "$cancel_sig" ] && break
    tnow=$(now)
    if [ "$tnow" -ge "$deadline" ]; then
      rm -f "$ticket"
      trap - INT TERM
      echo "TIMEOUT owner=$owner waiter_timeout=${timeout}s ticket_removed=1" >&2
      return 75
    fi
    find "$QUEUE" -maxdepth 1 -type f -mmin +"${HOSTLOCK_TICKET_STALE_MINUTES:-3}" -delete 2>/dev/null || true
    r=$(reservation_owner)
    if [ -n "$r" ] && [ "$r" != "$owner" ]; then
      if [ $(( tnow - last_note )) -ge 60 ]; then echo "WAITING owner=$owner reason=reservation holder=$r"; last_note=$tnow; fi
      sleep 2; continue
    fi
    if [ -z "$r" ]; then
      head=$(queue_head)
      if [ "$head" != "$(basename "$ticket")" ]; then
        if [ $(( tnow - last_note )) -ge 60 ]; then echo "WAITING owner=$owner queue_head=${head:-none}"; last_note=$tnow; fi
        sleep 2; continue
      fi
    fi
    if mkdir "$LOCK" 2>/dev/null; then
      if [ -n "$cancel_sig" ]; then
        # created a moment ago and nobody can own it yet
        rm -rf "$LOCK"
        break
      fi
      rm -f "$ticket"
      printf '%s\n' "$owner" > "$LOCK/owner"
      if [ "$(reservation_owner)" = "$owner" ]; then rm -f "$RESERVATION"; fi
      now > "$LOCK/start"
      trap - INT TERM
      echo "ACQUIRED owner=$owner at $(date '+%Y-%m-%d %H:%M:%S')"
      return 0
    fi
    se=$(start_epoch)
    if [ "$se" -gt 0 ]; then
      age=$(( tnow - se ))
      if [ "$age" -gt "$STALE" ]; then
        echo "BREAK-STALE heartbeat_age=${age}s moved=$(head -1 "$LOCK/owner" 2>/dev/null | tr -d '\r')"
        mv "$LOCK" "$ROOT/stale-$(date +%s)-$$" 2>/dev/null || true
        continue
      fi
    else
      if [ -d "$LOCK" ] && [ -n "$(find "$LOCK" -maxdepth 0 -mmin +2 2>/dev/null)" ]; then
        echo "BREAK-INCOMPLETE lock dir without heartbeat"
        mv "$LOCK" "$ROOT/stale-incomplete-$(date +%s)-$$" 2>/dev/null || true
        continue
      fi
    fi
    sleep 2
  done
  # only the cancel path reaches here
  rm -f "$ticket"
  trap - INT TERM
  echo "CANCELLED owner=$owner signal=$cancel_sig ticket_removed=1 waited=$(( $(now) - waited_from ))s" >&2
  if [ "$cancel_sig" = "INT" ]; then return 130; else return 143; fi
}

release() {
  owner=$1; mode=${2:-}
  cur=$(head -1 "$LOCK/owner" 2>/dev/null | tr -d '\r')
  if [ "$cur" = "$owner" ] || [ "$mode" = "--force" ]; then
    rm -rf "$LOCK"; echo "RELEASED owner=${cur:-none} by=$owner"
  else
    echo "REFUSED release owner=$owner current=${cur:-none}" >&2; return 1
  fi
}
break_lock() {
  # v2.3: a break must say who is breaking whom. Before this line the command
  # was `release "$1" --force`, so any owner string released anybody's lock:
  # on 2026-10-23 12:26 a task used it on a lock it did not hold, and the
  # holder kept building for minutes with no lock at all. The damage was not
  # the release, it was that nobody could tell afterwards whether two runs had
  # shared the host. So: an owner may always drop its own lock; dropping
  # somebody else's needs --force and is written to host.lock.history.
  owner=$1; mode=${2:-}
  if [ ! -d "$LOCK" ]; then echo "BREAK-NONE lock is free, nothing to break"; return 0; fi
  cur=$(head -1 "$LOCK/owner" 2>/dev/null | tr -d "\r")
  age=$(heartbeat_age)
  if [ "$cur" = "$owner" ]; then
    release "$owner"; return 0
  fi
  if [ "$mode" = "--force" ]; then
    echo "$(date +%Y-%m-%dT%H:%M:%S) BREAK owner=$cur by=$owner forced heartbeat=${age}s" >> "$ROOT/host.lock.history"
    release "$owner" --force; return 0
  fi
  echo "REFUSED break owner=$owner: the lock is held by ${cur:-none} (heartbeat ${age}s)" >&2
  echo "  break drops YOUR OWN lock. To drop somebody else\047s, say so explicitly:" >&2
  echo "    hostlock.sh break $owner --force" >&2
  return 1
}
reserve() { echo "$1 $(( $(now) + ${2:-1800} ))" > "$RESERVATION"; echo "RESERVED owner=$1 ttl=${2:-1800}s"; }
unreserve() { rm -f "$RESERVATION"; echo "UNRESERVED"; }

case "${1:-status}" in
  status) status ;;
  acquire) shift; acquire "$@" ;;
  release) shift; release "$@" ;;
  reserve) shift; reserve "$@" ;;
  unreserve) unreserve ;;
  break) shift; break_lock "$@" ;;
  run)
    shift; owner=$1; shift
    acquire "$owner" "${HOSTLOCK_TIMEOUT:-5400}"; arc=$?
    # D-52: a cancelled waiter exits 130/143 and must not run the command;
    # only a real TIMEOUT stays 75 (HOSTLOCK-V2.md documents 75).
    [ "$arc" -eq 0 ] || exit "$arc"
    ( while :; do sleep 60; if [ -d "$LOCK" ]; then touch "$LOCK/start" 2>/dev/null; else exit 0; fi; done ) & HB=$!
    cleanup() { kill "$HB" 2>/dev/null; release "$owner" >/dev/null 2>&1 || true; }
    trap cleanup EXIT INT TERM
    "$@"; rc=$?
    echo "LOCKED-COMMAND-EXIT rc=$rc owner=$owner"
    exit "$rc" ;;
  *) echo "usage: hostlock.sh {status|acquire|release|reserve|unreserve|break|run}" >&2; exit 2 ;;
esac