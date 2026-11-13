#!/usr/bin/env bash
# Exercises the lock block exactly as weapon-animation-check.sh now spells it.
set -u
W=./t-lock.$$
rm -rf "$W"; mkdir -p "$W/.scratch"
cd "$W" || exit 1
fails=0
chk() { if [ "$2" = "$3" ]; then echo "  OK   $1 = $2"; else echo "  FAIL $1 = $2 (want $3)"; fails=$((fails+1)); fi; }
reset_lock() { rm -rf .scratch/weapon-animation.lock; }

run_case() {
  WEAPON_LOCK_STALE_SECONDS=${WEAPON_LOCK_STALE_SECONDS:-5400}
  LOCK_DIR=.scratch/weapon-animation.lock
  take_lock() {
    mkdir "$LOCK_DIR" 2>/dev/null || return 1
    { echo "pid=$$"; echo "started=$(date +%s)"; } > "$LOCK_DIR/owner" 2>/dev/null || true
    return 0
  }
  lock_owner_alive() {
    local f="$LOCK_DIR/owner" pid
    [ -f "$f" ] || return 1
    pid=$(sed -n "s/^pid=//p" "$f" 2>/dev/null | head -1)
    case "$pid" in ""|*[!0-9]*) return 1 ;; esac
    kill -0 "$pid" 2>/dev/null
  }
  if ! take_lock; then
    now=$(date +%s)
    born=$(sed -n "s/^started=//p" "$LOCK_DIR/owner" 2>/dev/null | head -1)
    case "$born" in
      ""|*[!0-9]*) born=$(stat -c %Y "$LOCK_DIR" 2>/dev/null || echo "$now") ;;
    esac
    case "$born" in ""|*[!0-9]*) born=$now ;; esac
    age=$((now - born))
    if lock_owner_alive; then echo "FAIL-ALIVE age=${age}s"; return 0; fi
    if [ "$age" -ge "$WEAPON_LOCK_STALE_SECONDS" ]; then
      rm -rf "$LOCK_DIR"
      if take_lock; then echo "STALE-REMOVED age=${age}s"; else echo "FAIL-RETRY"; fi
      return 0
    fi
    echo "FAIL-YOUNG age=${age}s"; return 0
  fi
  echo "ACQUIRED"
}

echo "=== case 1: no lock at all ==="
reset_lock
chk "fresh run acquires" "$(run_case)" "ACQUIRED"
reset_lock

echo "=== case 2: owner pid is alive ==="
mkdir -p .scratch/weapon-animation.lock
{ echo "pid=$$"; echo "started=$(date +%s)"; } > .scratch/weapon-animation.lock/owner
out=$(run_case)
case "$out" in FAIL-ALIVE*) echo "  OK   live owner is honoured ($out)";; *) echo "  FAIL live owner not honoured: $out"; fails=$((fails+1));; esac
reset_lock

echo "=== case 3: owner pid is dead, lock is old ==="
mkdir -p .scratch/weapon-animation.lock
{ echo "pid=999999"; echo "started=$(( $(date +%s) - 40000 ))"; } > .scratch/weapon-animation.lock/owner
out=$(run_case)
case "$out" in STALE-REMOVED*) echo "  OK   dead+old lock is cleared ($out)";; *) echo "  FAIL dead+old lock not cleared: $out"; fails=$((fails+1));; esac
reset_lock

echo "=== case 4: THE HISTORICAL SHAPE -- directory with NO owner file, old mtime ==="
mkdir -p .scratch/weapon-animation.lock
touch -t 202610221539 .scratch/weapon-animation.lock 2>/dev/null || touch -d "5 hours ago" .scratch/weapon-animation.lock
out=$(run_case)
case "$out" in STALE-REMOVED*) echo "  OK   the 15:38 shape is cleared ($out)";; *) echo "  FAIL the 15:38 shape still fails the gate: $out"; fails=$((fails+1));; esac
reset_lock

echo "=== case 5: owner pid dead but lock is YOUNG (must not be cleared) ==="
mkdir -p .scratch/weapon-animation.lock
{ echo "pid=999999"; echo "started=$(date +%s)"; } > .scratch/weapon-animation.lock/owner
out=$(run_case)
case "$out" in FAIL-YOUNG*) echo "  OK   a young lock is still honoured ($out)";; *) echo "  FAIL a young lock was cleared: $out"; fails=$((fails+1));; esac
reset_lock

cd ..; rm -rf "$W"
echo
if [ "$fails" -eq 0 ]; then echo "WEAPON-LOCK-SELFTEST=PASS (5 cases)"; else echo "WEAPON-LOCK-SELFTEST=FAIL ($fails)"; exit 1; fi
