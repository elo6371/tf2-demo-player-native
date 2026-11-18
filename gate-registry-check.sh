#!/usr/bin/env bash
# gate-registry-check.sh -- every *-check.sh is in a chain, or has a named
# exclusion with a reason.
#
# WHY THIS EXISTS
#   I-32: sultry-translucent-atlas-check.sh (121 assertions) was added in
#   Phase E and referenced by nothing. A check that no chain calls is not a
#   gate; it is a file. The release policy already says a step may not silently
#   disappear, but nothing said a gate may not silently appear and then be
#   forgotten.
#
# THE RULE
#   For every `*-check.sh` in the tree root:
#     1. its filename appears on a non-comment line of verify-fast.sh,
#        verify-all.sh or verify-release.sh (registered), OR
#     2. it has a row in docs/GATES-UNREGISTERED.md whose reason is non-empty
#        (named exclusion).
#   A gate may not be both. A registry row may not name a missing gate. An
#   empty reason is a failure, not a placeholder.
#
# WHY STRIP COMMENTS
#   verify-all.sh and verify-release.sh discuss gates in comments. Counting a
#   mention as an invocation is how a registry says "covered" while the gate
#   never runs. Only non-comment text counts.
#
# SELFTEST
#   `--selftest` builds a temporary tree with two gates and a one-line chain,
#   then proves five things: a listed gate passes, an unlisted gate fails, a
#   registered gate listed as excluded fails, an empty reason fails, and a row
#   for a missing gate fails. The failure text names the gate so a reader can
#   act on it.
#
# USAGE
#   bash gate-registry-check.sh [--root DIR] [--selftest]
# Exit: 0 = the registry is complete and unambiguous; 1 = a finding.
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SELFTEST=0
while [ $# -gt 0 ]; do
  case "$1" in
    --root) ROOT="$2"; shift 2 ;;
    --selftest) SELFTEST=1; shift ;;
    -h|--help) sed -n '2,40p' "$0"; exit 0 ;;
    *) echo "unknown argument: $1" >&2; exit 2 ;;
  esac
done

CHAIN_NAMES=(verify-fast.sh verify-all.sh verify-release.sh)
REGISTRY_DOC="docs/GATES-UNREGISTERED.md"

audit() { # audit <root>
  local root="$1"
  local fail=0 total=0 nreg=0 nexc=0 gate chain text
  local -a gates=() registered=() excluded=()

  if [ ! -d "$root" ]; then
    echo "GATE-REGISTRY=FAIL reason=root-missing root=$root"
    return 1
  fi

  for chain in "${CHAIN_NAMES[@]}"; do
    if [ ! -f "$root/$chain" ]; then
      echo "  FAIL chain-script-missing name=$chain"
      fail=1
    fi
  done

  for gate in "$root"/*-check.sh; do
    [ -e "$gate" ] || continue
    gate=$(basename "$gate")
    gates+=("$gate")
    total=$((total + 1))
  done

  # A gate is registered when its filename occurs on a non-comment line.
  for gate in "${gates[@]}"; do
    local hit=0
    for chain in "${CHAIN_NAMES[@]}"; do
      [ -f "$root/$chain" ] || continue
      if sed 's/#.*//' "$root/$chain" | grep -qF -- "$gate"; then hit=1; break; fi
    done
    if [ "$hit" -eq 1 ]; then registered+=("$gate"); nreg=$((nreg + 1)); fi
  done

  # Parse the Markdown table. Only rows whose first cell is a *-check.sh name
  # participate; header and separator rows do not.
  local doc="$root/$REGISTRY_DOC"
  if [ ! -f "$doc" ]; then
    echo "  FAIL registry-doc-missing path=$REGISTRY_DOC"
    fail=1
  else
    while IFS= read -r line; do
      case "$line" in
        '| `'*'-check.sh` |'*)
          local name reason
          name=$(printf '%s' "$line" | cut -d'|' -f2 | tr -d '` ')
          reason=$(printf '%s' "$line" | cut -d'|' -f4 | sed 's/^[[:space:]]*//;s/[[:space:]]*$//')
          if [ -z "$name" ]; then continue; fi
          if [ ! -f "$root/$name" ]; then
            echo "  FAIL registry-row-for-missing-gate name=$name"
            fail=1
            continue
          fi
          if [ "${#reason}" -lt 12 ]; then
            echo "  FAIL empty-reason name=$name"
            fail=1
          fi
          excluded+=("$name"); nexc=$((nexc + 1))
          ;;
      esac
    done < "$doc"
  fi

  # Every gate must be in exactly one state.
  for gate in "${gates[@]}"; do
    local in_reg=0 in_exc=0 g
    for g in "${registered[@]}"; do [ "$g" = "$gate" ] && in_reg=1; done
    for g in "${excluded[@]}"; do [ "$g" = "$gate" ] && in_exc=1; done
    if [ "$in_reg" -eq 1 ] && [ "$in_exc" -eq 1 ]; then
      echo "  FAIL registered-and-excluded name=$gate"
      fail=1
    elif [ "$in_reg" -eq 0 ] && [ "$in_exc" -eq 0 ]; then
      echo "  FAIL unregistered-gate name=$gate (add it to a chain or to $REGISTRY_DOC)"
      fail=1
    fi
  done

  if [ "$fail" -eq 0 ]; then
    echo "GATE-REGISTRY=PASS gates=$total registered=$nreg excluded=$nexc"
    return 0
  fi
  echo "GATE-REGISTRY=FAIL gates=$total registered=$nreg excluded=$nexc"
  return 1
}

selftest() {
  local tmp rc bad=0
  tmp=$(mktemp -d "${TMPDIR:-/tmp}/gate-registry.XXXXXX") || exit 2
  mkdir -p "$tmp/docs"
  : > "$tmp/verify-fast.sh"
  : > "$tmp/verify-all.sh"
  : > "$tmp/verify-release.sh"
  printf '#!/usr/bin/env bash\nexit 0\n' > "$tmp/gate-a-check.sh"
  printf '#!/usr/bin/env bash\nexit 0\n' > "$tmp/gate-b-check.sh"
  printf 'bash gate-a-check.sh\n' > "$tmp/verify-fast.sh"
  cat > "$tmp/docs/GATES-UNREGISTERED.md" <<'EOF'
| Gate | Class | Why it is not in a chain | When it runs / owner |
|---|---|---|---|
| `gate-b-check.sh` | segmented | a named reason long enough to pass | owner |
EOF

  run_case() { # run_case <name> <want-rc> <needle> <file-to-mutate> <perl-ish? no: caller writes>
    local name="$1" want="$2" needle="$3"
    local out="$tmp/out-$name.txt"
    audit "$tmp" > "$out" 2>&1
    rc=$?
    if [ "$rc" -ne "$want" ]; then
      echo "  SELFTEST-FAIL $name rc=$rc want=$want"
      bad=1
    elif [ -n "$needle" ] && ! grep -qF -- "$needle" "$out"; then
      echo "  SELFTEST-FAIL $name missing=$needle"
      bad=1
    else
      echo "  SELFTEST-OK $name rc=$rc"
    fi
  }

  run_case positive 0 'GATE-REGISTRY=PASS gates=2 registered=1 excluded=1'

  cp "$tmp/docs/GATES-UNREGISTERED.md" "$tmp/doc.bak"
  grep -v 'gate-b-check.sh' "$tmp/doc.bak" > "$tmp/docs/GATES-UNREGISTERED.md"
  run_case unlisted 1 'FAIL unregistered-gate name=gate-b-check.sh'

  cat "$tmp/doc.bak" > "$tmp/docs/GATES-UNREGISTERED.md"
  printf '| `gate-a-check.sh` | segmented | a named reason long enough to pass | owner |\n' >> "$tmp/docs/GATES-UNREGISTERED.md"
  run_case both 1 'FAIL registered-and-excluded name=gate-a-check.sh'

  cp "$tmp/doc.bak" "$tmp/docs/GATES-UNREGISTERED.md"
  sed -i 's/a named reason long enough to pass//' "$tmp/docs/GATES-UNREGISTERED.md"
  run_case empty-reason 1 'FAIL empty-reason name=gate-b-check.sh'

  cat "$tmp/doc.bak" > "$tmp/docs/GATES-UNREGISTERED.md"
  printf '| `ghost-check.sh` | segmented | a named reason long enough to pass | owner |\n' >> "$tmp/docs/GATES-UNREGISTERED.md"
  run_case missing-gate 1 'FAIL registry-row-for-missing-gate name=ghost-check.sh'

  cat "$tmp/doc.bak" > "$tmp/docs/GATES-UNREGISTERED.md"
  run_case restored 0 'GATE-REGISTRY=PASS gates=2 registered=1 excluded=1'

  rm -rf "$tmp"
  if [ "$bad" -eq 0 ]; then echo "GATE-REGISTRY-SELFTEST=PASS controls=5"; return 0; fi
  echo "GATE-REGISTRY-SELFTEST=FAIL"; return 1
}

if [ "$SELFTEST" -eq 1 ]; then selftest; exit $?; fi
audit "$ROOT"
