#!/usr/bin/env bash
# entity-property-lookup-check.sh -- the class-fallback sweep must not build a
# full transform for an entity it is about to discard.
#
# Why this exists
# ---------------
# EntityModelResolver::buildInstances has two paths:
#
#   1. the request path -- one transform per render request, unavoidable;
#   2. the class-fallback sweep -- walk the snapshot for player-classed entities
#      that carry no request, and give each one its class's default model.
#
# The sweep used to call extractTransform() on every entity that passed the name
# check (isPlayerClassName) and only then ask whether the transform had a usable
# player class. extractTransform walks the entity's whole property map once per
# requested suffix (m_vecOrigin, m_angEyeAngles, m_iClass, ...), so an entity
# about to be discarded for a missing m_iClass paid for all of it.
#
# The change makes the sweep ask the cheap question first: one
# findProperty(state, "m_iClass") against the same suffix and the same value
# range extractTransform applies. Only an entity that passes pays for the
# transform. The *set* of entities that get an instance must not change -- that
# is what the instance counts in section 2 pin -- only the work spent on the
# ones that do not.
#
# Why this is measured on a fixture and not on the demos
# ------------------------------------------------------
# On every demo in the corpus the sweep is cold: every player entity already has
# a render request, so the sweep walks nothing past its `covered` check and the
# pre-check never fires. A reading of zero there says the demos do not exercise
# the path, not that the path is cheap. The synthetic states table the probe
# builds at the end of its tf-root block is the one place the sweep runs, so the
# fixture counters are the ones this gate asserts on. The demo counters are
# printed too, and pinned at their (zero) values, so the day a demo starts
# exercising the sweep is the day this file says so.
#
# What is asserted
# ----------------
#   1. the fixture reading -- fallbackScanned=3, classLookups=1, transforms=6,
#      propertyComparisons=19. The pre-check's own witness is entity 7 in the
#      fixture: player-named but carrying no m_iClass. Without the pre-check the
#      same fixture reads transforms=7 and propertyComparisons=24 -- one full
#      transform and five property comparisons that the pre-check spares;
#   2. the fixture *result* is unchanged -- instanceCount=6, fallbacks=1, so the
#      pre-check spared work without dropping or adding an instance;
#   3. bagel and the POV demo end to end -- the resolver counters are printed and
#      the demo sweep is asserted cold (fallbackScanned=0), which is the fact
#      that makes the fixture the only witness;
#   4. the probe's new keys are additive -- the ten new keys are named here so a
#      reader can see which readings this round introduced.
#
# Usage: bash entity-property-lookup-check.sh [--mutation]
#   --mutation removes the pre-check from entity_model.cpp, rebuilds the probe,
#   and requires the fixture to read transforms=7 (the un-spared count) while the
#   instance counts stay put -- which is how the pre-check is shown to be the
#   thing section 1 is measuring. The source is restored and the binary rebuilt
#   on exit whether or not the mutation was caught.
# Exit:  0 = every assertion held (or, with --mutation, the perturbation went red).
set -uo pipefail
cd "$(dirname "$0")"

PROBE=native/build-nmake/entity_model_probe.exe
TF="D:/SteamLibrary/steamapps/common/Team Fortress 2/tf"
BAGEL="D:/TF2_Demo_Player/testdata/demos/4a9bfb9276509d0ec5f5fdc722a95b17_match-20260927-0239-koth_bagel_rc13.dem"
POV="D:/SteamLibrary/steamapps/common/Team Fortress 2/tf/demos/autorecord_2026-07-02_13-26-46.dem"
OUT="${ENTITY_PROPERTY_LOOKUP_OUT:-evidence/entity-property-lookup}"
mkdir -p "$OUT"

MUTATION=0
[ "${1:-}" = "--mutation" ] && MUTATION=1

fail=0
ok=0
bad=0
[ -x "$PROBE" ] || { echo "FATAL: missing binary: $PROBE"; exit 1; }
for path in "$TF" "$BAGEL" "$POV"; do
  [ -e "$path" ] || { echo "FATAL: missing input: $path"; exit 1; }
done

jvalue() { grep -oE "\"$2\":[0-9]+" "$1" | head -1 | cut -d: -f2; }

assert_eq() { # assert_eq <label> <actual> <expected>
  if [ "${2:-<none>}" = "$3" ]; then
    echo "  OK   $1 = $3"; ok=$((ok + 1))
  else
    echo "  FAIL $1 = ${2:-<none>}, expected $3"; fail=1; bad=$((bad + 1))
  fi
}

# ---------------------------------------------------------------------------
# The mutation lives here, before any reading, so the numbers sections 1-2 print
# are the mutated build's -- a mutation that ran after the reads would only be
# re-checking the fixed build.
# ---------------------------------------------------------------------------
MUTATED=0
# The mutated file is saved by content and restored from that copy, not with
# `git checkout --`. Checkout reverts to HEAD, which during development is the
# pre-P1a tree -- running the mutation before the change is committed would then
# silently delete the change under test. Saving the bytes is safe either way.
MUTATION_BACKUP="$OUT/entity_model.cpp.pre-mutation"
restore() {
  if [ "$MUTATED" -eq 1 ] && [ -f "$MUTATION_BACKUP" ]; then
    cp "$MUTATION_BACKUP" native/src/entity_model.cpp
    # Restoring the source is not enough: the gate itself re-reads the probe, and
    # a probe left built from the mutated source would make the next plain run
    # read the mutation. Rebuild so the binary matches the tree.
    #
    # A failed rebuild here is the dangerous case, so it is loud rather than
    # ignored: the source would be fixed while the binary stayed mutated, and
    # every later reading of this probe would be the defect. build-target.sh now
    # exits non-zero on a failed build, which is what makes this check possible.
    bash build-target.sh entity_model_probe > /dev/null 2>&1 \
      || echo "WARNING: restore rebuild failed; $PROBE may still be the mutated build" >&2
  fi
}
trap restore EXIT

if [ "$MUTATION" -eq 1 ]; then
  echo "=== mutation: remove the cheap class pre-check and require section 1 to move ==="
  cp native/src/entity_model.cpp "$MUTATION_BACKUP"
  # The pre-check is the findProperty/m_iClass guard plus the counter bump. Its
  # absence restores the old behaviour: every name-passing entity pays for
  # extractTransform, so the fixture's transform count climbs from 6 to 7.
  C:/Users/Administrator/.workbuddy/binaries/python/versions/3.13.12/python.exe - <<'PY'
import io, sys
path = "native/src/entity_model.cpp"
src = io.open(path, encoding="utf-8").read()
needle = (
    '    const auto* playerClass = findProperty(state, "m_iClass");\n'
    '    if (!playerClass || playerClass->type != SendPropType::Int\n'
    '        || playerClass->intValue < 1 || playerClass->intValue > 9) continue;\n'
    '    ++g_resolverStats.classLookups;\n'
)
if needle not in src:
    sys.stderr.write("mutation: pre-check not found; source shape changed\n")
    sys.exit(1)
io.open(path, "w", encoding="utf-8").write(src.replace(needle, '', 1))
print("mutation: pre-check removed")
PY
  if [ $? -ne 0 ]; then echo "MUTATION-SETUP=FAIL"; exit 1; fi
  MUTATED=1
  bash build-target.sh entity_model_probe > /dev/null 2>&1 || { echo "mutation: rebuild failed"; exit 1; }
fi

echo "=== 1/3 fixture: the class-fallback sweep's own work ==="
# One run of the demo probe reaches the fixture, because the fixture is built
# inside the tf-root block. Its counters are printed under `fixture*`.
"$PROBE" --tf-root "$TF" --demo "$BAGEL" > "$OUT/fixture.json" 2> "$OUT/fixture.txt"
F_SCAN=$(jvalue "$OUT/fixture.json" fixtureFallbackScanned)
F_LOOK=$(jvalue "$OUT/fixture.json" fixtureClassLookups)
F_XFORM=$(jvalue "$OUT/fixture.json" fixtureTransforms)
F_CMP=$(jvalue "$OUT/fixture.json" fixturePropertyComparisons)
echo "fixture fallbackScanned=$F_SCAN classLookups=$F_LOOK transforms=$F_XFORM propertyComparisons=$F_CMP"
# Exact, not bounded: the fixture is fixed, so these are the readings its design
# pins. A bound would let the sweep stop running as long as it stopped quietly.
#
# The mutation asserts the un-spared values and counts them as the caught signal
# directly, rather than feeding them through assert_eq and reading `bad` -- with
# the mutation in place the un-spared values are the *expected* ones, so they
# pass assert_eq and `bad` would stay 0 no matter what the mutation did.
MUTATION_CAUGHT=0
if [ "$MUTATION" -eq 1 ]; then
  assert_eq "mutation fixture transforms (back to the un-spared count)" "$F_XFORM" "7"
  assert_eq "mutation fixture propertyComparisons (un-spared)" "$F_CMP" "24"
  assert_eq "mutation fixture classLookups (the pre-check is gone)" "$F_LOOK" "0"
  # All three readings are the un-spared ones, which is the perturbation's
  # signature. Any one of them still reading its fixed value means the mutation
  # did not take effect and this run proves nothing.
  if [ "$F_XFORM" = "7" ] && [ "$F_CMP" = "24" ] && [ "$F_LOOK" = "0" ]; then
    MUTATION_CAUGHT=1
  fi
else
  assert_eq "fixture fallbackScanned" "$F_SCAN" "3"
  assert_eq "fixture classLookups (entities spared a transform)" "$F_LOOK" "1"
  assert_eq "fixture transforms (was 7 before the pre-check)" "$F_XFORM" "6"
  assert_eq "fixture propertyComparisons (was 24 before the pre-check)" "$F_CMP" "19"
fi

echo
echo "=== 2/3 fixture: the pre-check spared work without dropping or adding an instance ==="
F_INST=$(jvalue "$OUT/fixture.json" fixtureInstanceCount)
F_FALL=$(jvalue "$OUT/fixture.json" fixtureFallbacks)
echo "fixture instanceCount=$F_INST playerFallbacks=$F_FALL"
assert_eq "fixture instanceCount (unchanged by the pre-check)" "$F_INST" "6"
assert_eq "fixture playerFallbacks (unchanged by the pre-check)" "$F_FALL" "1"

echo
echo "=== 3/3 demos end to end: the sweep is cold, which is why the fixture is the witness ==="
"$PROBE" --tf-root "$TF" --demo "$BAGEL" > "$OUT/bagel.json" 2> /dev/null
"$PROBE" --tf-root "$TF" --demo "$POV" > "$OUT/pov.json" 2> /dev/null
for demo in bagel pov; do
  d_scan=$(jvalue "$OUT/$demo.json" resolverFallbackScanned)
  d_xform=$(jvalue "$OUT/$demo.json" resolverTransforms)
  d_inst=$(jvalue "$OUT/$demo.json" instanceCount)
  echo "$demo resolverFallbackScanned=$d_scan resolverTransforms=$d_xform instanceCount=$d_inst"
  # The demo sweep walking nothing is the fact that makes section 1's fixture the
  # only place this path is measured. If a demo starts exercising it, this gate
  # is no longer measuring what it says, and this line says so.
  assert_eq "$demo sweep is cold (every player already has a request)" "$d_scan" "0"
  assert_eq "$demo instanceCount (unchanged)" "$d_inst" "6"
done

echo
if [ "$MUTATION" -eq 1 ]; then
  # One perturbation, one claim: the pre-check's absence has to show in the
  # fixture's transform count (and its two companions). If all three read the
  # un-spared values, the thing section 1 checks is the thing the mutation
  # changed.
  if [ "$MUTATION_CAUGHT" -eq 1 ]; then
    echo "MUTATION-CAUGHT=PASS (the un-spared build read transforms=$F_XFORM comparisons=$F_CMP lookups=$F_LOOK)"
    exit 0
  fi
  echo "MUTATION-CAUGHT=FAIL (removing the pre-check left the readings at 6/19/1)"
  exit 1
fi

echo
if [ "$fail" -eq 0 ]; then
  echo "ENTITY-PROPERTY-LOOKUP=PASS (assertions_ok=$ok)"
else
  echo "ENTITY-PROPERTY-LOOKUP=FAIL (assertions_bad=$bad)"
fi
exit "$fail"
