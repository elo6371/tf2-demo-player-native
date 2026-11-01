#!/usr/bin/env bash
# verify-all.sh -- one command that reproduces the whole P0 evidence chain.
#
#   1. clean full Release build (25 exe) and check error/warning counts
#   2. census all nine local demos
#   3. run the 58 wire fixtures
#   4. cross-check against the independent Rust oracle (nine demos)
#   5. cross-check against the oracle on a sample drawn from the pinned
#      evidence/corpus-calib/demos.txt universe (not the live demos directory)
#   6. pin the recording type (POV vs SourceTV) of the nine oracle demos
#   7. prove the probe's added output lines moved no existing counter, and that
#      the added lines themselves have not moved since they were introduced
#   8. prove the readings can go red (C++ mutation suite, P0 and P1 cases)
#   9. prove the corpus-census verdict can go red
#  10. compare reconstructed entity positions against the independent oracle
#      *value by value* (counts cannot see a value read from the wrong property)
#  11. bound how old an out-of-window entity answer is allowed to be (step 10
#      only sees the live window; nothing before this step said how far back a
#      Checkpoint answer may fall)
#  12. prove a weapon's render request names the weapon and not the hands that
#      hold it (m_nModelIndex is the first-person composite on a weapon entity;
#      the world pass has to use m_iWorldModelIndex)
#  13. resolve TF2's observer pair -- m_iObserverMode and m_hObserverTarget -- and
#      measure what believing it would cost (it is left unwired on purpose; see
#      observer-focus-check.sh for the 45-unit residual that decision now rests on,
#      for the 2729 it read before step 14 moved it, and for the 2026-10-08
#      attribution: the camera there is a held deathcam, the pair only ever names the
#      recorder's own body, and so the demo cannot decide the wiring question)
#  14. prove the entity-property selection rule takes the slot a later packet
#      wrote, not the one its rank prefers (a player's origin arrives twice and
#      only one copy keeps updating; step 13's 2729 -> 45 is this step's work)
#  15. decode every real SourceTV recording this machine has -- all nine of them --
#      and assert zero failures on each, so the SourceTV half of the protocol has
#      more than one demo behind it
#  16. take a frame back out of the running program and prove the instrument can
#      fail -- that the file is a self-consistent BMP, that the same scene twice is
#      byte-identical, and that a different scene is not. Steps 1-15 can all be
#      green while the picture on screen is wrong; this is the first step that can
#      say anything about the picture at all, and it deliberately says nothing
#      about whether the picture is *right* -- only that a later claim about the
#      picture would be measurable.
#  17. name what the demo scene can actually load. Step 16 pins the instrument but
#      never asks what is in the frame; on this machine its demo (koth_bagel_rc13)
#      has no installed BSP, so the capture came back as the renderer's fallback
#      full-screen spray-decal quad and step 16 passed anyway. This step asserts
#      which of main.cpp's five hardcoded archives opened, whether the demo map's
#      BSP is reachable, how many of its materials resolve to pixels, and how many
#      survive the 512x512 world-atlas cap -- resolvable is not drawable.
#  18. read the loop's *rate* rather than its output. The main loop awaited the
#      next frame in 1 ms slices, so an idle window ran the whole body ~1000 times
#      a second, pushing a title and UI controls for state that had not moved; a
#      hot loop still produces correct output, which is why no step above could
#      see it. This step asserts main_loop_iterations per rendered frame, that the
#      two state-driven updaters stay far below the loop count when nothing moves,
#      and that the metrics throttle still holds.
#  19. prove the class-fallback sweep does not build a transform for an entity it
#      is about to discard. The sweep used to call extractTransform on every
#      player-named entity and only then ask whether the transform had a player
#      class; extractTransform scans the whole property map once per suffix, so a
#      discarded entity paid for all of it. The witness is a synthetic fixture --
#      the demos cannot see it, because every player entity on every demo already
#      has a render request and the sweep is cold -- and the assertion is that the
#      fixture's transform count drops while its instance count does not move.
#  20. prove a model drawn as an entity carries its own paint, at a tick where
#      entities exist. Two gaps in one step: the entity draw block bound the map
#      atlas to every model, so a model was painted with whatever the atlas held
#      at the model's texel and no count could tell that from "no paint"; and
#      every gate that runs the program left at playback tick 16, before this
#      demo's first checkpoint at scene tick 10320, so nothing in the chain had
#      ever seen an entity. On 2026-10-09 the program died on the first drawable
#      snapshot while all nineteen steps above stayed green. The step asserts the
#      resolution chain per model from bytes, the instance-level binding count
#      read out of the window title past the boundary, and the 20 pixels of the
#      one visible instance against a committed witness captured before the
#      binding existed.
#  21. measure whether a demo carries the animation state that per-entity skeletal
#      animation would need, before wiring any. The 2026-10-07 recon listed bone
#      animation as a wiring task -- compile the decoder in, open
#      uploadBoneMatrices(..., true), advance the sequence by tick -- which assumes
#      the demo says which sequence a player is playing. It does not: TF2 strips
#      m_nSequence / m_flCycle / m_flPlaybackRate from CTFPlayer's send table
#      (source-sdk-2013 declares them in DT_BaseAnimating but routes m_flCycle via
#      SendProxy_ClientSideAnimation, and c_baseanimating.cpp:1168 notes that
#      player entities do not network m_nSequence). The step asserts the absence
#      on three recordings, the positive control on the same run (197 classes DO
#      carry it -- a rule that can only say no measures nothing), and the
#      independent oracle's agreement. 23 assertions; m13 makes it go red.
#  22. prove the product *skins* those models from real demo animation input, on
#      the GPU, and that the pose follows the tick rather than merely advancing a
#      frame cursor. Step 21 measured that a demo does not carry a player's
#      sequence; it said nothing about whether the program ever uses the sequence
#      state it does carry. It did not: `animation_decoder.cpp` was not compiled
#      into the main target and `uploadBoneMatrices` was called with skinning off,
#      so the animation chain was proven only by a probe. The step asserts the
#      per-instance GPU skinning counters on three playback ticks, the standalone
#      `--model` contract (identity skin matrices must leave the bind pose
#      bit-identical while a sequence must move it), that a 100-frame game
#      sequence renders differently at two ticks and identically at tick 0 and
#      tick 100, and that two mutations -- the entity contract's bind pose and the
#      single-model identity matrices -- each turn it red.
#  23. measure the ViewModel's animation input rather than assert its absence. T2
#      drew the ViewModel in its bind pose and reported animationInput=missing;
#      step 21 had answered "no" for CTFPlayer, and this step answers it for the
#      class that does carry it. CTFViewModel (id 320) networks m_nSequence and
#      m_flPlaybackRate but not m_flCycle; the entity names c_<class>_arms.mdl
#      (43 sequences, 58 bones) while the pass draws v_<weapon>.mdl (7 sequences,
#      45 bones), so 104 of 141 observed sequences are past the drawn table and 37
#      merely look legal -- and those 37 are the trap. The owning entity is read
#      from m_hOwner after svc_SetView names the view entity, and on one of the two
#      pinned recordings that pairing does not resolve at all, which is asserted as
#      its own negative. The product refuses the out-of-range sequence and keeps
#      the bind pose; the accepted pose is reachable but opt-in, because applying
#      it would put a scattergun pose on a sniper rifle. `--mutation` neuters the
#      resolver's range branch and must turn the refusal red.
#
#  24. prove the ViewModel pass draws the c_*_arms model named by the entity and
#      keeps owner, model path, sequence table and bone coverage as one proof.
#
# Anything that must be true for the delivery is asserted here, so a reviewer does
# not have to read twenty-four reports. Every step writes its raw output to
# evidence/ before the assertion runs.
#
# Step 10 exists because the P1 z defect was invisible to every count-based
# check: the decoder was reading the right bits, writing the right property, and
# the renderer was reading a different one, so every entity sat at z = 0 while
# steps 4-6 stayed green.
#
# Step 11 exists because "the answer is a Checkpoint" said nothing about how old
# the checkpoint was: the retention policy could pin the head of the archive and
# answer a tick 54392 ticks stale (13.7 minutes) while every count stayed green
# and step 10 stayed green too (it only compares inside the live window).
#
# Step 12 exists because a path can be wrong and still resolve. A weapon entity's
# m_nModelIndex names its first-person arms composite -- a real asset -- so a
# world pass driven by it draws a pair of hands where the weapon belongs, and
# every counting step above stays green while it does. The step compares the
# index against the oracle's own packet value and pins which route chose the
# path, on the fixture, on two demos, and end to end.
#
# Steps 5, 6, 7 and 9 exist because the first P0 acceptance pass got four things
# wrong that no green reading caught:
#   * it reported all nine oracle demos as POV; five are SourceTV match demos
#     (their clientname is "SourceTV Demo"), so the SourceTV corpus was never
#     actually missing -- the classifier was.
#   * the oracle gate only ever saw nine demos, so "POV keeps zero failures" had
#     no independent-implementation evidence on the 1634-demo POV corpus.
#   * the corpus census printed PASS while parsing only the first line of each
#     report, so every "must be zero" field was absent and skipped.
#   * the probe gained an output line with nothing proving the old counters
#     were untouched.
#
# A fifth class of error turned up while re-reading the chain: a step that passes
# without checking anything. check-probe-output-additive.sh printed SKIP for all
# nine demos (it used repo-relative paths, and the isolated tree has no testdata/)
# and still ended in PASS; the fixture step asserted fixture_failures=0 but not
# that any fixture ran; the build step asserted rc=0 but not that any exe was
# produced; the mutation step asserted MUTATION-SUITE=PASS but not that any case
# ran. Each of those now asserts a non-zero work count as well.
#
# The P1 pass added a sixth: a line that is *stripped* from a comparison is a line
# nothing verifies. check-probe-output-additive.sh now keeps a second frozen file
# for the lines it strips, so "additive" and "unverified" stop being the same word.
#
# 2026-10-08 added a seventh, found the expensive way: an input set that is a
# property of the machine. Step 9 used to run `--sample 24` over the live Steam
# demos directory; two recordings landed while the round was being worked on, the
# evenly spaced sample moved to neighbouring files, and 23 of the 24 frozen
# reports stopped being read -- so every mutation would have been inert and the
# step went red for the right reason. The set is now pinned by name in
# evidence/corpus-calib/demos.txt and the step asserts the 24-report count.
# Step 5 had the same latent defect -- its own `--sample` globbed the same live
# directory -- and would have produced non-reproducible evidence on any round
# where a recording landed mid-run (it in fact did, on 2026-10-09: the eight
# compared demos were not the eight the committed file named). It now reads the
# same pinned list and asserts the compared count.
#
# Step 15 is the eighth, and it is a coverage hole rather than a wrong reading: the
# pinned 24-demo calibration set holds one SourceTV recording, so of its 1361392
# packets 112175 were SourceTV. A whole-corpus header scan found nine SourceTV
# recordings, so the fix is bounded -- decode all nine (664 MB, 917543 packets,
# ~3.5 min) instead of running a four-hour census over 1634 POV files that share one
# code path with the five POV demos already in the oracle set. Each demo is checked
# for zero entity failures *and* for having been read end to end (index_tail_bytes
# equals the file size), because a probe that stopped early reports the same zeros.
#
# Cost: steps 1 and 5 dominate. Step 1 ~4 min, step 5 ~20 min at --sample 40
# (both now sample *within* the pinned 24-name list, so the same demo set is
# compared on every machine). The pinned list caps the reachable sample at 24.
# Step 10 adds ~2.5 min (the oracle walks the whole demo once per compared tick).
# Step 11 adds ~1.5 min (one bagel scan for the staleness sample).
# Step 12 adds ~4 min (one bagel scan, one POV scan, and one oracle run per
# packet in the live window -- 68 of them).
# Step 13 adds ~4 min (the gate twice: once straight, once with --mutation).
# Step 15 adds ~3.5 min (nine full decodes; --mutation reuses those dumps, because
# it perturbs the comparison and not the decode).
# Step 18 adds ~5 min (two 12 s idle/play samples, plus the mutation, which
# rebuilds tf2_demo_native once to put the 1 ms wait back and once to restore it).
# Step 19 adds ~1.5 min (four probe runs over bagel, one over the POV demo, plus
# the mutation, which rebuilds entity_model_probe once and once again to restore).
# Step 20 adds ~3.5 min (the gate twice: once straight, once with --mutation. Each
# run is a 57 s demo run to playback tick 3001 -- the program has to survive the
# snapshot boundary for any of the readings to exist -- plus five probe runs over
# the installed VPK and a 2.5 MB frame compare).
# Anything that must be true for the delivery is asserted here, so a reviewer does
# not have to read twenty-four reports. Every step writes its raw output to
# evidence/ before the assertion runs.
#
# Usage: bash verify-all.sh [--quick]     (--quick: oracle corpus sample of 8)
# Exit:  0 = every step passed.
set -uo pipefail
cd "$(dirname "$0")"

# A full chain owns the build tree and every path under evidence/verify. Two
# callers sharing those paths produce a plausible but unusable hybrid report:
# one run can overwrite another run's SourceTV output or mutation counts. mkdir
# is atomic on the filesystem, so a second caller fails before it starts a
# build. Remove the directory only on normal shell exit; a stale lock after a
# killed process must be cleared deliberately rather than silently overlapping.
LOCK_DIR=.scratch/verify-all.lock
if ! mkdir "$LOCK_DIR" 2>/dev/null; then
  echo "VERIFY-LOCK=FAIL (another verify-all run owns $LOCK_DIR)"
  exit 2
fi
trap 'rmdir "$LOCK_DIR" 2>/dev/null || true' EXIT

ORACLE_SAMPLE=40
[ "${1:-}" = "--quick" ] && ORACLE_SAMPLE=8

OUT=evidence/verify
mkdir -p "$OUT"
rc_all=0

step() { printf '\n=== %s ===\n' "$1"; }

step "1/30 clean full Release build"
bash build-cmake.sh > "$OUT/1-build.txt" 2>&1
tail -6 "$OUT/1-build.txt"
# exe_count pins that the build actually produced the targets: rc=0 with zero
# exes would otherwise read as a clean build.
#
# T15 (2026-10-18): the pin reads 38, and the value it replaced -- 33 -- had been
# "correct" for three rounds without ever describing a complete build.
#   `native_ui.cpp` calls `planMapLookup` and `AssetRoot::fromPath`, but
#   native_ui_probe's source list never gained `map_lookup.cpp` or
#   `asset_root.cpp`. After `--target clean` the link failed with three LNK2019s,
#   NMAKE stopped, and the four targets ordered after it were never linked. The
#   tree had 37 add_executable lines and the build produced exactly 33 exes, so
#   the pin agreed with a TRUNCATED build. rc=0 would have caught it; the pin
#   alone could not. This is the 2026-10-06 `exe_count=17` incident a second
#   time, with the difference that the number happened to match.
#   T15 fixed the link, added vpk_integrity_probe, and measured the result:
#   `grep -c '^add_executable('` = 38, a clean full build printing
#   cmake_build_rc=0 errors=0 exe_count=38.
#
# History of the number, kept so the next addition stays a deliberate edit:
# 33 was the count after the entity-material round, the skeleton_skin_probe
# target, the T1 animation round's weapon_animation_probe, the T2 round's
# viewmodel_contract_probe, the T2.5 round's viewmodel_animation_probe, the T3
# audio content round's sound_content_map_probe, and the T7 audio coverage
# round's sound_corpus_scan_probe.
# Then four more landed without the pin moving: the T9 round's map_lookup_probe,
# the T10/T11 rounds' world_geometry_probe, the T12 round's terrain_blend_probe
# and the T13 round's viewmodel_range_probe -- 37 lines, still pinned at 33.
# T2 added exactly one target: viewmodel_fov_probe was already in the baseline's
# 25 and only gained source files (measured 2026-10-11: `git show
# 80c7949:native/CMakeLists.txt` has 25 add_executable lines and includes
# viewmodel_fov_probe; the tree after T2 has 26 and the tree after T2.5 has 27).
# The 2026-10-15 T3 content round is the next such addition: sound_content_map_probe,
# measured by `grep -c '^add_executable('` = 32 and by a full build printing
# exe_count=32. The 2026-10-16 T7 round added sound_corpus_scan_probe, measured
# the same two ways as 33.
# (The pin read 23 while the tree built 24 between 00b6033 and this line: the
# probe target was added without the pin moving, so step 1 reported BUILD=FAIL on
# every chain run in that window. The warning count is deliberately NOT asserted
# -- the C4457 in main.cpp predates all of this and only its line number moves;
# the 2026-10-18 build reports 9 warnings, all of them pre-existing.)
#
# 2026-10-20 T29-T33 round: 38 -> 42, a deliberate edit measured two ways on the
# merged tree (Lead, integration 1b0d1a2): grep -c '^add_executable(' = 42, and a
# clean full Release build printing cmake_build_rc=0 errors=0 warnings=10
# exe_count=42. Four targets joined the tree since the 38 pin was written, each
# named from the commit that added it rather than counted by eye:
#   T19 0ade875 material_coverage_probe, T24 593065d lighting_semantics_probe,
#   T27 fa1cf8d world_feature_probe, T30 909a253 model_robustness_probe.
#   Three of them (T19/T24/T27) landed without the pin moving, so the pin had
#   already drifted to 38-vs-41 before this round; this edit closes that
#   pre-existing gap as well as adding T30's target.
#
# 2026-10-20 T37 round: 42 -> 44, measured the same two ways on the merged tree
# (Lead, after the T37 merge d305211): grep -c '^add_executable(' = 44, and a
# clean full Release build printing cmake_build_rc=0 errors=0 exe_count=44 with
# exes_on_disk=44. Both targets come from T37 (44f5cc9), not from a probe that
# drifted in unnoticed:
#   native/tools/install_cli_probe      the --install/--uninstall/--version
#                                       lifecycle driver the product answers from
#   native/tools/vpk_readiness_probe    the VPK attribution probe that
#                                       distinguishes absent / truncated /
#                                       unreadable / unsupported from each other
# The warning count moved 10 -> 11 in the same build (a C4189 unused local in
# T37's own code); warnings are not asserted here by design, and the new one is
# named in the T37 report rather than suppressed.
#
# 2026-10-23 release round: 44 -> 49, measured on the merged tree after the
# five feature merges ended at 3823b46: grep -c '^add_executable(' = 49, and a
# clean full Release build printing cmake_build_rc=0 errors=0 warnings=16
# exe_count=49. (This script's older entries also quote exes_on_disk; this
# round did not read that line, so it is not claimed here.)
# One target per feature, each named from the commit that added it:
#   T50 77f7499 camera_probe           the camera director / bookmark / key table
#   T51 e4d2983 hud_probe              the custom HUD import rules, shared with the product
#   T52 148bcfd entity_overlay_probe   the silhouette pass, driven offline
#   T53 0c409c1 projectile_track_probe the projectile series, with closed-form fixtures
#   T54 f30426c logstf_viewer_probe    the logs.tf panel contract, compiled into the product too
# The warning count moved 11 -> 16 in the same build. Warnings are still not
# asserted here by design; the five new ones are named in the release report
# rather than suppressed. This pin is edited in the release round on purpose:
# it is the number that says the build produced every target, and it has to
# move with the target list or it starts agreeing with a truncated build.
#
# 2026-10-24 review wave: 49 -> 50, measured on the merged tree after the
# T63-F1 / T68 / T67 / T69 merges (a7d0a77): grep -c '^add_executable(' = 50
# and a build of every target leaving 50 exes on disk (this round's
# evidence/release-gates/build-p0-wave2.log: `ls native/build-nmake/*.exe |
# wc -l` = 50 after BUILD_RC=0 with 0 errors). One target joined:
#   T69 eff64cd minimap_probe          the minimap projection / hit-test / panel state,
#                                      compiled from the same src/minimap.cpp as the product
# The other three merges added no target: T63-F1 and T68 extend demo_open_probe,
# T67 adds hud_font.cpp + hud_panel.h to the existing product and hud_probe.
# The warning count is unchanged at 16 in this build.
#
# 2026-10-24 T70 merge: 50 -> 51, measured on the integration tree after the
# T70 merge (42b345f): grep -c '^add_executable(' = 51 and
# build-target.sh tf2_demo_native playback_control_probe left 51 exes on disk
# with BUILD_RC=0 and 0 'error C' lines (evidence/release-gates/build-p0-t70.log).
# One target joined:
#   T70 bf06eea playback_control_probe the frame-step / short-jump / speed-tier
#                                      controller, compiled from the same
#                                      src/playback_control.cpp as the product
# The warning count is unchanged at 18 lines in this build (18 since the T52 v2 merge
# added native_renderer.cpp C4100/C4456 sites; T70 adds none).
# 2026-10-25 T66: 51 -> 52, on branch task/t66-sourcetv. One target joined:
#   T66 spectate_probe  the SourceTV relay (death handover / hidden corpses)
#                       compiled from src/spectate_target.cpp, the same file the
#                       product links, so a probe disagreeing with the product
#                       would be a link error rather than a silent divergence.
# The product binary also gained the director, but no target was added for it:
# the relay's decisions are asserted on the probe and on the product trace.
#
# 2026-10-25 T71/T72: 52 -> 53, on the integration tree after the review-workspace
# store merge (0fa74ab) and the product wiring that follows it: a full
# build-cmake.sh printed errors=0 warnings=40 exe_count=53, and both
# \grep -c '^add_executable(' native/CMakeLists.txt\ and
# \ls native/build-nmake/*.exe | wc -l\ read 53. One target joined:
#   T71/T72 0fa74ab review_workspace_probe  the workspace codec, the bookmark store
#                                           and the session, compiled from the same
#                                           src/review_workspace.cpp and
#                                           src/review_bookmarks.cpp the product links
# The warning count moved 38 -> 40 in that build (both new sites are in the review
# store); warnings are still not asserted here by design.
# This pin is edited in the release round on purpose: it is the number that says
# the build produced every target, and it must move with the target list or it
# starts agreeing with a truncated build.
if grep -q 'cmake_build_rc=0' "$OUT/1-build.txt" \
   && grep -q '^errors=0$' "$OUT/1-build.txt" \
   && grep -q '^exe_count=53$' "$OUT/1-build.txt"; then
  echo "BUILD=PASS"
else
  echo "BUILD=FAIL"; rc_all=1
fi

step "2/30 nine-demo census"
bash run-demos.sh evidence/final | tee "$OUT/2-census.txt"
if [ "$(grep -c 'entity_failures=0 malformed_packets=0 unknown_message_packets=0' "$OUT/2-census.txt")" -eq 9 ]; then
  echo "CENSUS=PASS (9/9 demos at zero)"
else
  echo "CENSUS=FAIL"; rc_all=1
fi
# The message-type coverage assertion: no observed type may lack a decoder.
if [ "$(grep -c 'message_types_seen_without_decoder: <none>' evidence/final/*.txt | grep -c ':1$')" -eq 9 ]; then
  echo "COVERAGE=PASS (9/9 demos, every observed message type decoded)"
else
  echo "COVERAGE=FAIL"; rc_all=1
fi

step "3/30 wire fixtures"
native/build-nmake/entity_message_fixture_probe.exe > "$OUT/3-fixture.txt" 2>&1
FIXTURES=$(grep -c '^PASS' "$OUT/3-fixture.txt" || true)
echo "fixture_pass_count=$FIXTURES"
tail -1 "$OUT/3-fixture.txt"
# fixture_failures=0 alone would also hold if the probe ran nothing, so the pass
# count is asserted too. 58 is the current fixture count; adding one is a
# deliberate edit here.
if grep -q '^fixture_failures=0$' "$OUT/3-fixture.txt" && [ "$FIXTURES" -eq 58 ]; then
  echo "FIXTURE=PASS ($FIXTURES/58)"
else
  echo "FIXTURE=FAIL"; rc_all=1
fi

step "4/30 oracle cross-check (nine demos)"
bash check-oracle.sh "D:/TF2_Demo_Player_Deliverable/tools/ent-oracle/target/release/ent-oracle.exe" evidence/final \
  | tee "$OUT/4-oracle.txt"
if grep -q 'ORACLE-GATE=PASS' "$OUT/4-oracle.txt"; then
  echo "ORACLE=PASS"
else
  echo "ORACLE=FAIL"; rc_all=1
fi

step "5/30 oracle cross-check on a corpus sample (n=$ORACLE_SAMPLE)"
# The input set is pinned, not sampled from the live directory. `sorted(glob())`
# plus evenly spaced sampling is a property of the machine: the live corpus grew
# from 1656 to 1658 recordings mid-round on 2026-10-08, every index moved, and the
# evidence file named eight demos the committed run had never seen. Step 9 hit the
# same seventh class first; both now read evidence/corpus-calib/demos.txt, so the
# sample is a function of a frozen file rather than of whatever is on disk. The
# sample is taken *within* that pinned list, so --quick and the full run stay
# reproducible and the full run can ask for more demos than the quick one.
PY="C:/Users/Administrator/.workbuddy-ai/binaries/python/versions/3.13.12/python.exe"
ORACLE_CORPUS_LIST="evidence/corpus-calib/demos.txt"
ORACLE_CORPUS_SET=$(grep -cv '^#' "$ORACLE_CORPUS_LIST")
"$PY" oracle-corpus-check.py --demos-list "$ORACLE_CORPUS_LIST" \
  --sample "$ORACLE_SAMPLE" --workers 2 > "$OUT/5-oracle-corpus.txt" 2>&1
tail -14 "$OUT/5-oracle-corpus.txt"
# Assert the size actually compared, capped at the pinned list, so this step cannot
# pass on zero demos and cannot silently fall back to globbing the live directory.
ORACLE_CORPUS_N=$(sed -n 's/^sampled=\([0-9][0-9]*\)$/\1/p' "$OUT/5-oracle-corpus.txt" | head -1)
ORACLE_CORPUS_WANT=$ORACLE_SAMPLE
[ "$ORACLE_CORPUS_WANT" -gt "$ORACLE_CORPUS_SET" ] && ORACLE_CORPUS_WANT=$ORACLE_CORPUS_SET
if grep -q 'ORACLE-CORPUS=PASS' "$OUT/5-oracle-corpus.txt" \
   && [ "${ORACLE_CORPUS_N:-0}" -eq "$ORACLE_CORPUS_WANT" ]; then
  echo "ORACLE-CORPUS=PASS (pinned_sample=$ORACLE_CORPUS_N of $ORACLE_CORPUS_SET pinned)"
else
  echo "ORACLE-CORPUS=FAIL (pinned_sample=${ORACLE_CORPUS_N:-0} want=$ORACLE_CORPUS_WANT of $ORACLE_CORPUS_SET)"; rc_all=1
fi
# The comparison itself must be able to reject a mismatch.
if "$PY" oracle-corpus-check.py --selftest 2>&1 | grep -q 'ORACLE-CORPUS-SELFTEST=PASS'; then
  echo "ORACLE-CORPUS-SELFTEST=PASS"
else
  echo "ORACLE-CORPUS-SELFTEST=FAIL"; rc_all=1
fi

step "6/30 oracle demo recording types"
bash oracle-recording-types.sh | tee "$OUT/6-recording-types.txt"
if grep -q 'ORACLE-RECORDING-TYPES=PASS' "$OUT/6-recording-types.txt"; then
  echo "RECORDING-TYPES=PASS"
else
  echo "RECORDING-TYPES=FAIL"; rc_all=1
fi

step "7/30 probe output is additive"
bash check-probe-output-additive.sh | tee "$OUT/7-probe-additive.txt"
if grep -q 'PROBE-OUTPUT-ADDITIVE=PASS' "$OUT/7-probe-additive.txt"; then
  echo "PROBE-ADDITIVE=PASS"
else
  echo "PROBE-ADDITIVE=FAIL"; rc_all=1
fi

step "8/30 mutation suite (C++)"
VERIFY_ALL_LOCK_HELD=1 bash mutate.sh > "$OUT/8-mutation.txt" 2>&1
tail -8 "$OUT/8-mutation.txt"
# Counting the verdict lines is what stops this step from passing on an empty
# suite: MUTATION-SUITE=PASS is also what a script that ran zero cases prints.
# 46 red + 6 hold is the current case count (m11 added three red assertions and
# one hold -- the hold is bagel's chosenStale, which must NOT move when the
# freshness term is deleted, or the gate would be passable by a rule that simply
# prefers NonLocal; m12 added three red assertions and one hold -- the reds are
# the gate's refusal, the instance counter and the ledger's uploaded count, and
# the hold is medic's resolved slot count, which must NOT move when only the
# upload is short-circuited, or the gate's parse layer and its binding layer
# would be one reading counted twice; m13 adds three red assertions and one hold
# for the animation-property positive control. m13 was run alone on 2026-10-10
# and the complete suite read 46/6/0/0/6. Adding a case is an explicit edit here,
# which is the
# point: a suite whose assertion count drifts silently is a suite that stopped
# testing. Any GREEN or BROKE line means a mutation did not move the reading it
# targets -- which is the one thing the suite exists to detect.
RED=$(grep -c '^MUTATION-RED' "$OUT/8-mutation.txt" || true)
HOLD=$(grep -c '^MUTATION-HOLD' "$OUT/8-mutation.txt" || true)
BROKE=$(grep -c '^MUTATION-BROKE' "$OUT/8-mutation.txt" || true)
GREEN=$(grep -c '^MUTATION-GREEN' "$OUT/8-mutation.txt" || true)
IDENT=$(grep -c '^RESTORED-IDENTICAL' "$OUT/8-mutation.txt" || true)
echo "mutation_red=$RED hold=$HOLD green=$GREEN broke=$BROKE restored_identical=$IDENT"
if grep -q 'MUTATION-SUITE=PASS' "$OUT/8-mutation.txt" \
   && [ "$RED" -eq 46 ] && [ "$HOLD" -eq 6 ] \
   && [ "$GREEN" -eq 0 ] && [ "$BROKE" -eq 0 ] && [ "$IDENT" -eq 6 ]; then
  echo "MUTATION=PASS"
else
  echo "MUTATION=FAIL"; rc_all=1
fi

step "9/30 census verdict is falsifiable"
bash census-negative-test.sh | tee "$OUT/9-census-negative.txt"
# The report count is asserted so this step cannot pass on an empty pinned set:
# CENSUS-NEGATIVE=PASS is also what a suite that mutated zero reports would
# print. 24 is the frozen calibration set (evidence/corpus-calib/demos.txt),
# because on 2026-10-08 the live `--sample 24` moved out from under the frozen
# reports and every mutation would have been inert.
NEG_REPORTS=$(sed -n 's/^  reports=\([0-9]*\)  digest=.*/\1/p' "$OUT/9-census-negative.txt" | head -1)
if grep -q 'CENSUS-NEGATIVE=PASS' "$OUT/9-census-negative.txt" && [ "${NEG_REPORTS:-0}" -eq 24 ]; then
  echo "CENSUS-NEGATIVE=PASS (pinned_reports=$NEG_REPORTS)"
else
  echo "CENSUS-NEGATIVE=FAIL (pinned_reports=${NEG_REPORTS:-0})"; rc_all=1
fi

step "10/30 reconstructed positions vs the independent oracle, per value"
# Steps 4-6 compare *counts* against the Rust oracle. Counts cannot see a value
# that is decoded correctly and then read from the wrong property -- which is
# exactly the P1 z defect: every counter stayed green while every player rendered
# at z = 0. This step compares values, entity by entity. It also asserts a work
# count (compared=), so a run that compares nothing cannot report PASS.
TRAJ_OUT="$OUT/10-trajectory.txt"
"$PY" oracle-trajectory-check.py --ticks 4 > "$TRAJ_OUT" 2>&1
TRAJ_RC=$?
"$PY" oracle-trajectory-check.py --ticks 2 --mutation > "$OUT/10-trajectory-mutation.txt" 2>&1
MUT_RC=$?
cat "$TRAJ_OUT"
COMPARED=$(sed -n 's/.*compared=\([0-9][0-9]*\).*/\1/p' "$TRAJ_OUT" | head -1)
if [ "$TRAJ_RC" -eq 0 ] && [ "${COMPARED:-0}" -ge 20 ] \
   && [ "$MUT_RC" -eq 0 ] && grep -q 'MUTATION-CAUGHT=PASS' "$OUT/10-trajectory-mutation.txt"; then
  echo "TRAJECTORY-ORACLE=PASS (compared=$COMPARED, mutation caught)"
else
  echo "TRAJECTORY-ORACLE=FAIL (rc=$TRAJ_RC compared=${COMPARED:-0} mutation_rc=$MUT_RC)"; rc_all=1
fi

step "11/30 entity history coverage (how old a Checkpoint answer may be)"
# Step 10 compares values only inside the ~70-packet live window, because that is
# the only place the answer is tick-exact. Everywhere else the answer falls back
# to a retained checkpoint, and nothing bounded how old that could be -- measured
# on bagel, the answer could be 54392 ticks (13.7 minutes) stale while every
# count-based step stayed green. history-coverage-check.sh asserts the pigeonhole
# bound (worst retained gap <= 2x span/(kept-1)) on a synthetic fixture and on
# bagel end to end, and the work count (sampled=512) so an empty run cannot pass.
COV="$OUT/11-history-coverage.txt"
bash history-coverage-check.sh > "$COV" 2>&1
cat "$COV"
if grep -q 'HISTORY-COVERAGE=PASS' "$COV" \
   && grep -qF 'OK   sampled=512 of the requested 512 ticks' "$COV"; then
  echo "HISTORY-COVERAGE=PASS"
else
  echo "HISTORY-COVERAGE=FAIL"; rc_all=1
fi

step "12/30 weapon world model (a weapon must not be drawn as its arms)"
# Step 10 compares values but only for positions; a model path is the other half
# of "the picture is right", and it fails differently. A weapon entity carries
# two indices: m_nModelIndex is the first-person composite (its class's c_*_arms
# model) and m_iWorldModelIndex is the weapon itself. Reading the first one is not
# an error the decoder can see -- it resolves, to a real asset -- so the wrong
# picture was reachable with every count above staying green. The gate pins the
# fixture (every branch by construction), both demo readings, the index against
# the oracle's own packet value, and the skip: no packet in the live window writes
# the property, so the count of packets that carry it must be zero, and the gate
# says so out loud instead of comparing nothing.
WSMODEL="$OUT/12-weapon-world-model.txt"
bash weapon-world-model-check.sh > "$WSMODEL" 2>&1
cat "$WSMODEL"
WS_FIXTURE=$(grep -c '^  OK   fixture' "$WSMODEL" || true)
WS_PACKETS=$(sed -n 's/.*packets checked=\([0-9][0-9]*\) .*/\1/p' "$WSMODEL" | head -1)
if grep -q 'WEAPON-WORLD-MODEL=PASS' "$WSMODEL" \
   && [ "$WS_FIXTURE" -eq 12 ] && [ "${WS_PACKETS:-0}" -gt 0 ]; then
  echo "WEAPON-WORLD-MODEL=PASS (fixture_ok=$WS_FIXTURE, live_window_packets=$WS_PACKETS)"
else
  echo "WEAPON-WORLD-MODEL=FAIL (fixture_ok=$WS_FIXTURE, live_window_packets=${WS_PACKETS:-0})"; rc_all=1
fi

step "13/30 observer focus (who a spectator is watching, and what it would cost)"
# A camera driven by the view entity alone is right for a live player and wrong
# for a spectator: TF2 names the subject in m_iObserverMode / m_hObserverTarget,
# and this pipeline read neither. Step 13 asserts the pair resolves, that it agrees
# with the oracle's own packet, and -- because the obvious wiring is wrong on this
# demo -- pins the distance it would move the camera, so the round's refusal to
# follow is a measurement rather than an opinion. Run with --mutation too: both
# compared values are nudged by one and have to go red.
OBFOCUS="$OUT/13-observer-focus.txt"
bash observer-focus-check.sh > "$OBFOCUS" 2>&1
cat "$OBFOCUS"
OB_OK=$(grep -c '^  OK   ' "$OBFOCUS" || true)
OB_DIV=$(sed -n 's/^camera-to-target distance = \([0-9]*\) units.*/\1/p' "$OBFOCUS" | head -1)
if grep -q 'OBSERVER-FOCUS=PASS' "$OBFOCUS" && [ "${OB_OK:-0}" -gt 0 ] \
   && [ "${OB_DIV:-0}" -gt 0 ]; then
  echo "OBSERVER-FOCUS=PASS (assertions_ok=$OB_OK, camera-to-target=${OB_DIV}u)"
else
  echo "OBSERVER-FOCUS=FAIL (assertions_ok=${OB_OK:-0}, camera-to-target=${OB_DIV:-0})"; rc_all=1
fi
# The gate's own mutation lives inside step 13 rather than in mutate.sh, because the
# case it guards did not ship: there is no source line to reinstate, so what has to
# be shown able to fail is the comparison itself.
OBMUT="$OUT/13-observer-focus-mutation.txt"
bash observer-focus-check.sh --mutation > "$OBMUT" 2>&1
OB_RED=$(grep -c '^  FAIL ' "$OBMUT")
if grep -q 'MUTATION-CAUGHT=PASS' "$OBMUT" && [ "${OB_RED:-0}" -eq 3 ]; then
  echo "OBSERVER-FOCUS-MUTATION=PASS (3/3 perturbations caught, red_lines=$OB_RED)"
else
  echo "OBSERVER-FOCUS-MUTATION=FAIL (a perturbation went unnoticed)"; rc_all=1
fi

step "14/30 entity property slot freshness (the rule must take the later write)"
# Step 10 compares positions but only inside one demo's live window, and on that
# demo the rank rule happens to be right. A player's origin arrives in two slots --
# DT_TFLocalPlayerExclusive (full precision, sent to the owning client) and
# DT_TFNonLocalPlayerExclusive (quantized, sent to everyone else) -- and only one of
# them keeps being written. On the POV demo's entity 3 the Local slot was last
# written at 51596 and never again while the NonLocal slot was rewritten every
# checkpoint, so a rank-only rule resolved a coordinate frozen 2379 ticks back. No
# counting step can see that: the candidate count is the same either way. Step 14
# asserts the rule takes the later write, that the tie/unknown-tick fallback still
# produces the old deterministic order, and that the ranking and the public API the
# renderer calls agree. mutate.sh m11 deletes the freshness term and requires this
# step to refuse, with bagel held unchanged.
SLOTF="$OUT/14-slot-freshness.txt"
bash slot-freshness-check.sh > "$SLOTF" 2>&1
cat "$SLOTF"
SF_OK=$(grep -c '^  OK   ' "$SLOTF" || true)
SF_STALE=$(sed -n 's/^SLOT-FRESHNESS=PASS (.*POV chosenStale=\([0-9]*\).*/\1/p' "$SLOTF" | head -1)
# 24 is the current assertion count (7 fixture + 8 POV + 5 bagel + 4 blast radius),
# pinned for the same reason step 8 pins its mutation count: a gate that keeps
# printing PASS while its assertions quietly disappear is not a gate.
if grep -q 'SLOT-FRESHNESS=PASS' "$SLOTF" && [ "${SF_OK:-0}" -eq 24 ] \
   && [ "${SF_STALE:-x}" = "0" ]; then
  echo "SLOT-FRESHNESS=PASS (assertions_ok=$SF_OK, POV chosenStale=0)"
else
  echo "SLOT-FRESHNESS=FAIL (assertions_ok=${SF_OK:-0}, POV chosenStale=${SF_STALE:-none})"; rc_all=1
fi
SLOTMUT="$OUT/14-slot-freshness-mutation.txt"
bash slot-freshness-check.sh --mutation > "$SLOTMUT" 2>&1
if grep -q 'MUTATION-CAUGHT=PASS' "$SLOTMUT" && grep -qc '^  FAIL ' "$SLOTMUT"; then
  SF_RED=$(grep -c '^  FAIL ' "$SLOTMUT")
  echo "SLOT-FRESHNESS-MUTATION=PASS (2/2 perturbations caught, red_lines=$SF_RED)"
else
  echo "SLOT-FRESHNESS-MUTATION=FAIL (a perturbation went unnoticed)"; rc_all=1
fi

step "15/30 real SourceTV coverage (all nine recordings, end to end)"
# The pinned 24-demo calibration set carries one SourceTV recording, so almost all of
# the SourceTV-side evidence in steps 1-14 is bagel and snakewater. A whole-corpus
# header scan says there are nine SourceTV recordings on this machine, and this step
# decodes all of them. The assertion that matters is not just entity_failures=0 --
# that is also what a probe that read one packet would print -- but index_tail_bytes
# equal to the file's own byte size, which is only true if the whole file was indexed
# and walked.
STV="$OUT/15-source-tv-coverage.txt"
bash source-tv-coverage-check.sh > "$STV" 2>&1
cat "$STV"
STV_OK=$(grep -c '^  OK   ' "$STV" || true)
STV_DEMOS=$(sed -n 's/^demos_read=\([0-9][0-9]*\) .*/\1/p' "$STV" | head -1)
STV_PKTS=$(sed -n 's/^demos_read=[0-9][0-9]* packets_scanned=\([0-9][0-9]*\) .*/\1/p' "$STV" | head -1)
# 47 is the current assertion count (9 demos x 5 assertions + 2 work counters),
# pinned for the same reason steps 8 and 14 pin theirs.
if grep -q 'SOURCE-TV-COVERAGE=PASS' "$STV" && [ "${STV_OK:-0}" -eq 47 ] \
   && [ "${STV_DEMOS:-0}" -eq 9 ] && [ "${STV_PKTS:-0}" -ge 900000 ]; then
  echo "SOURCE-TV-COVERAGE=PASS (demos=$STV_DEMOS packets=$STV_PKTS assertions_ok=$STV_OK)"
else
  echo "SOURCE-TV-COVERAGE=FAIL (demos=${STV_DEMOS:-0} packets=${STV_PKTS:-0} assertions_ok=${STV_OK:-0})"; rc_all=1
fi
# Both compared values are nudged by one and have to go red, nine rows each: a
# perturbation that reddens everything or nothing would not show that the comparison
# is per-demo.
STVMUT="$OUT/15-source-tv-coverage-mutation.txt"
bash source-tv-coverage-check.sh --mutation > "$STVMUT" 2>&1
if grep -q 'MUTATION-CAUGHT=PASS' "$STVMUT" && grep -qc '^  FAIL ' "$STVMUT"; then
  STV_RED=$(grep -c '^  FAIL ' "$STVMUT")
  echo "SOURCE-TV-COVERAGE-MUTATION=PASS (2/2 perturbations caught, red_rows=$STV_RED)"
else
  echo "SOURCE-TV-COVERAGE-MUTATION=FAIL (a perturbation went unnoticed)"; rc_all=1
fi

step "16/30 frame capture (the picture can be taken out of the running program)"
# Steps 1-15 are all counters and decoded values. Not one of them can speak to
# whether the picture is right, and the gap is structural rather than a missing
# assertion: step 12's weapon drawn as its arms resolved to a real asset and drew
# successfully, so a wrong picture can be a well-formed picture and every count
# above stays green. This step does not judge the picture. It pins the instrument
# -- a frame can be taken at a chosen tick, the file is a BMP whose header agrees
# with its own size, the same static scene twice is byte-identical, and the demo
# scene differs from the paused one. Those four are the precondition for any later
# claim of the form "this change moved the picture rather than a counter".
#
# Nothing here is a SKIP: a missing binary, demo or interpreter exits non-zero, and
# the invalid-tick check asserts the refusal exit code 13, so a run that could not
# capture at all cannot report PASS by capturing nothing.
FRAMECAP="$OUT/16-frame-capture.txt"
bash frame-capture-check.sh > "$FRAMECAP" 2>&1
cat "$FRAMECAP"
FC_OK=$(grep -c '^  OK   ' "$FRAMECAP" || true)
# The assertion count is pinned for the same reason steps 8, 14 and 15 pin theirs:
# a gate that keeps printing PASS while its assertions quietly disappear is not a
# gate. 18 is the current count, and it is worth naming how it is arrived at
# because an earlier version of this line said 15 and the chain went red for
# exactly that reason -- the machine caught the author's miscount, not a defect:
# 2 exit codes (paused, paused-again) + 9 header identities + 1 non-flat colour
# + 1 positive extent + 1 determinism + 1 scene-sensitivity + 2 rejection
# + 2 for the second demo (differs from paused; and is shaded geometry rather than
#   a full-screen decal, added 2026-10-08 when the original demo was found to be
#   producing the fallback spray picture).
if grep -q 'FRAME-CAPTURE=PASS' "$FRAMECAP" && [ "${FC_OK:-0}" -eq 18 ]; then
  echo "FRAME-CAPTURE=PASS (assertions_ok=$FC_OK)"
else
  echo "FRAME-CAPTURE=FAIL (assertions_ok=${FC_OK:-0})"; rc_all=1
fi
# The content comparisons are what a later picture claim leans on, so the mutation
# flips one byte of the captured image and requires the hash comparison to notice.
FCMUT="$OUT/16-frame-capture-mutation.txt"
bash frame-capture-check.sh --mutation > "$FCMUT" 2>&1
if grep -q 'MUTATION-CAUGHT=PASS' "$FCMUT"; then
  echo "FRAME-CAPTURE-MUTATION=PASS (a one-byte change moves the content hash)"
else
  echo "FRAME-CAPTURE-MUTATION=FAIL (a one-byte change went unnoticed)"; rc_all=1
fi

step "17/30 resource reachability (what the demo scene can actually load)"
# Step 16 pins the instrument but never asks what is *in* the frame it takes. That
# gap was not hypothetical on this machine: the demo named in frame-capture-check.sh
# is koth_bagel_rc13, whose BSP is not installed here, so the renderer fell through
# to its six-vertex full-screen quad textured with the fallback `texture_` -- the
# vgui spray decal -- and step 16 called that "the demo scene differs from the paused
# scene" and passed. This step names the resource facts that tell the two apart:
# which of the five archives main.cpp hardcodes actually opened (it skips a missing
# one silently), whether the demo map's BSP is reachable at all, how many of its
# materials resolve to decodable pixels, and how many survive main.cpp's 512x512
# atlas cap -- because a material that resolves but is 1024x1024 is still not
# drawable, so the triangle share is the reading that predicts the picture.
#
# The readings are for cp_snakewater_final1, the one demo here whose map is installed
# (loose BSP). The bagel map is the negative case: its BSP is absent, which is what
# produced the spray-decal frame step 16 accepted.
RR="$OUT/17-resource-reachability.txt"
bash resource-reachability-check.sh > "$RR" 2>&1
cat "$RR"
RR_OK=$(grep -c '^  OK   ' "$RR" || true)
# Assertion count pinned for the same reason as steps 8, 14, 15 and 16. 12 is
# arrived at as: 1 archive count + 1 missing-archive name + 1 BSP reachable
# + 1 BSP source + 1 materials resolve + 3 for the 512 cap (eligible, rejected,
# triangles covered) + 1 negative control (the old gate's map has no BSP)
# + 3 for the counterfactual (triangles at a 1024 cap, triangles uncapped, and
# the check that raising the cap actually recovers geometry).
# An earlier version of this line said 9 while the gate emitted 8, and the chain
# went red for exactly that reason -- the same class of author miscount as step
# 16's first run, caught by the machine rather than by reading.
if grep -q 'RESOURCE-REACHABILITY=PASS' "$RR" && [ "${RR_OK:-0}" -eq 12 ]; then
  echo "RESOURCE-REACHABILITY=PASS (assertions_ok=$RR_OK)"
else
  echo "RESOURCE-REACHABILITY=FAIL (assertions_ok=${RR_OK:-0})"; rc_all=1
fi
# The archive count is the reading that would have caught the silent skip, so the
# mutation demands five archives and requires the check to notice that four opened.
RRMUT="$OUT/17-resource-reachability-mutation.txt"
bash resource-reachability-check.sh --mutation > "$RRMUT" 2>&1
if grep -q 'MUTATION-CAUGHT=PASS' "$RRMUT"; then
  echo "RESOURCE-REACHABILITY-MUTATION=PASS (a wrong archive-count expectation goes red)"
else
  echo "RESOURCE-REACHABILITY-MUTATION=FAIL (a wrong expectation went unnoticed)"; rc_all=1
fi

step "18/30 idle spin (the loop must sleep until the next frame is due)"
# Every step above reads what the program *produced*: counters, decoded values,
# resources, pixels. None of them could see the loop running hot between frames,
# because a hot loop produces the same output -- it just produces it while
# burning a core. The defect was one line (waiting at most 1 ms instead of until
# the frame was due), and at ~100 fps it meant ~1000 iterations a second, each
# one pushing a title and UI controls for state that had not moved. This step is
# the only one in the chain that reads the loop's rate rather than its output.
#
# The assertion is the wait itself: mean_wait_ms, which the fixed build reads at
# ~8.97 (its clamp is sized to the frame gap) and the defective build at exactly
# 1.00 (min(remainingMs, 1)). The mutation is a source-level counterfactual: it
# puts the 1 ms wait back, rebuilds, and requires the same >= 4 ms floor to go
# red.
#
# An earlier version of this step asserted main_loop_iterations per rendered
# frame instead, with a ceiling of 4 against a measured 2. That reading does not
# discriminate here -- with a 120 Hz target (8.33 ms) and a loop body costing
# several ms, elapsed has already passed the target when the wait returns in both
# builds, so both read 2.0 and the defective build passed the straight run. The
# chain caught it, but only through --mutation; the ratio is now a diagnostic and
# the wait is the assertion. This is the repo's own rule applied to itself: a
# reading whose margin is that wide has to be shown failing on the defect, not on
# a tightened threshold.
IDLE="$OUT/18-idle-spin.txt"
bash idle-spin-check.sh > "$IDLE" 2>&1
cat "$IDLE"
IDLE_OK=$(grep -c '^  OK   ' "$IDLE" || true)
# Assertion count pinned for the same reason as steps 8, 14, 15, 16 and 17: a
# gate that prints PASS while its assertions quietly disappear is not a gate.
# 11 is arrived at as: 3 in step 1 (mean wait floor, update ratio, metrics rate)
# + 2 in step 2 (playback advanced, both updaters fired) + 6 in step 3 (the six
# metric columns the assertions read). The idle sample also has to carry at least
# three metrics rows or the rate cannot be computed, and that is asserted inside
# the script (it fails) rather than counted here.
if grep -q 'IDLE-SPIN=PASS' "$IDLE" && [ "${IDLE_OK:-0}" -eq 11 ]; then
  echo "IDLE-SPIN=PASS (assertions_ok=$IDLE_OK)"
else
  echo "IDLE-SPIN=FAIL (assertions_ok=${IDLE_OK:-0})"; rc_all=1
fi
IDLEMUT="$OUT/18-idle-spin-mutation.txt"
bash idle-spin-check.sh --mutation > "$IDLEMUT" 2>&1
if grep -q 'MUTATION-CAUGHT=PASS' "$IDLEMUT"; then
  echo "IDLE-SPIN-MUTATION=PASS (the 1 ms wait reads 1.00, under the 4 ms floor)"
else
  echo "IDLE-SPIN-MUTATION=FAIL (the defect stayed at or above the floor)"; rc_all=1
fi

step "19/30 entity property lookup (the class sweep must not transform what it discards)"
# The class-fallback sweep in EntityModelResolver::buildInstances used to build a
# full transform for every player-named entity it walked, then throw the work away
# when the transform came back without a player class. extractTransform scans the
# entity's whole property map once per requested suffix, so the discarded entity
# paid for all of it. The fix asks the cheap question first -- one findProperty for
# m_iClass, the same suffix and value range extractTransform applies -- and only
# transforms what passes.
#
# The demos cannot witness this: on every demo in the corpus every player entity
# already carries a render request, so the sweep walks nothing past its `covered`
# check and the pre-check never fires. That is asserted (the demo sweep must read
# 0), and the witness is the probe's synthetic fixture, which now includes one
# entity in the shape the pre-check exists for -- player-named, no m_iClass. With
# the pre-check that fixture reads 6 transforms and 19 property comparisons; with
# it removed, 7 and 24. The instance count is 6 either way, which is the property
# that must not move: the pre-check has to spare work, not change the result.
#
# Usage: bash entity-property-lookup-check.sh [--mutation]
#   --mutation removes the pre-check, rebuilds and requires the fixture to read
#   the un-spared counts; the source is restored and the binary rebuilt on exit.
ENTITY_PROP="$OUT/19-entity-property-lookup.txt"
bash entity-property-lookup-check.sh > "$ENTITY_PROP" 2>&1
cat "$ENTITY_PROP"
ENTITY_PROP_OK=$(grep -c '^  OK   ' "$ENTITY_PROP" || true)
# Assertion count pinned for the same reason as steps 8, 14, 15, 16, 17 and 18: a
# gate that prints PASS while its assertions quietly disappear is not a gate.
# 10 is arrived at as: 4 in section 1 (fallbackScanned, classLookups, transforms,
# propertyComparisons) + 2 in section 2 (instanceCount, playerFallbacks) + 4 in
# section 3 (sweep-is-cold and instanceCount for each of two demos).
if grep -q 'ENTITY-PROPERTY-LOOKUP=PASS' "$ENTITY_PROP" && [ "${ENTITY_PROP_OK:-0}" -eq 10 ]; then
  echo "ENTITY-PROPERTY-LOOKUP=PASS (assertions_ok=$ENTITY_PROP_OK)"
else
  echo "ENTITY-PROPERTY-LOOKUP=FAIL (assertions_ok=${ENTITY_PROP_OK:-0})"; rc_all=1
fi
ENTITY_PROP_MUT="$OUT/19-entity-property-lookup-mutation.txt"
bash entity-property-lookup-check.sh --mutation > "$ENTITY_PROP_MUT" 2>&1
if grep -q 'MUTATION-CAUGHT=PASS' "$ENTITY_PROP_MUT"; then
  echo "ENTITY-PROPERTY-LOOKUP-MUTATION=PASS (the un-spared build reads 7 transforms)"
else
  echo "ENTITY-PROPERTY-LOOKUP-MUTATION=FAIL (the pre-check's absence moved nothing)"; rc_all=1
fi

step "20/30 entity material (a model must carry its own paint, past the entity boundary)"
ENTITY_MAT="$OUT/20-entity-material.txt"
bash entity-material-check.sh > "$ENTITY_MAT" 2>&1
cat "$ENTITY_MAT"
ENTITY_MAT_OK=$(grep -c '^  OK   ' "$ENTITY_MAT" || true)
# Assertion count pinned for the same reason as steps 8, 14, 15, 16, 17, 18 and
# 19: a gate that prints PASS while its assertions quietly disappear is not a
# gate. 24 is arrived at as: 3 per model in section 1 (declared slots, resolved
# slots, and the CD directory the stems were resolved against) x 5 models = 15,
# + 8 in section 2 (exit code, the tick the title was read at, entityMaterials,
# entityMaterialRanges, ledger row count, ledger resolved, ledger uploaded, and
# the title's model denominator against the ledger) + 1 in section 3.
if grep -q 'ENTITY-MATERIAL=PASS' "$ENTITY_MAT" && [ "${ENTITY_MAT_OK:-0}" -eq 24 ]; then
  echo "ENTITY-MATERIAL=PASS (assertions_ok=$ENTITY_MAT_OK)"
else
  echo "ENTITY-MATERIAL=FAIL (assertions_ok=${ENTITY_MAT_OK:-0})"; rc_all=1
fi
ENTITY_MAT_MUT="$OUT/20-entity-material-mutation.txt"
bash entity-material-check.sh --mutation > "$ENTITY_MAT_MUT" 2>&1
# The mutation puts the atlas-painted pixels back, so it is the one perturbation
# that targets the property this step is about. The source-level equivalent --
# material resolution returning no SRV at all -- is m12 in mutate.sh, which
# targets the instance-level counter instead of the pixels.
if grep -q 'MUTATION-CAUGHT=PASS' "$ENTITY_MAT_MUT"; then
  echo "ENTITY-MATERIAL-MUTATION=PASS (layer 3 reads the atlas pixels as the defect)"
else
  echo "ENTITY-MATERIAL-MUTATION=FAIL (the atlas-painted input was not caught)"; rc_all=1
fi

step "21/30 animation availability (a demo does not carry a player's sequence)"
ANIM_AVAIL="$OUT/21-animation-availability.txt"
bash animation-availability-check.sh > "$ANIM_AVAIL" 2>&1
cat "$ANIM_AVAIL"
# The assertion count is pinned for the same reason as steps 8 and 14-20: a gate
# that prints PASS while its assertions quietly disappear is not a gate. 23 is
# arrived at as: 5 per recording in section 1 (the class is found, plus the four
# property counts) x 3 recordings = 15, + 5 in section 2 (the rule can report
# presence, classesWithSequence, classesWithRate, classesWithCycle, and
# CTFWeaponBase carrying a slot) + 3 in section 3 (the oracle's DT_BaseAnimating
# block is non-empty, its animation-property count is 0, and its schema contains
# CTFPlayer).
ANIM_AVAIL_OK=$(grep -c '^  OK   ' "$ANIM_AVAIL" || true)
if grep -q 'ANIMATION-AVAILABILITY=PASS' "$ANIM_AVAIL" && [ "${ANIM_AVAIL_OK:-0}" -eq 23 ]; then
  echo "ANIMATION-AVAILABILITY=PASS (assertions_ok=$ANIM_AVAIL_OK)"
else
  echo "ANIMATION-AVAILABILITY=FAIL (assertions_ok=${ANIM_AVAIL_OK:-0})"; rc_all=1
fi
# The in-script mutation is the cheap half: it rewrites the captured output and
# requires each assertion's extractor to see the rewrite, which is what catches a
# regex that silently returns nothing (the ' pose=' field did exactly that on the
# first run). The rebuilt, source-level half is m13 in mutate.sh, which neuters
# the sweep and requires the positive control to collapse to 0.
ANIM_AVAIL_MUT="$OUT/21-animation-availability-mutation.txt"
ANIM_AVAILABILITY_OUT="$OUT/21-animation-availability-mutation-evidence" \
  bash animation-availability-check.sh --mutation > "$ANIM_AVAIL_MUT" 2>&1
if grep -q 'MUTATION-CAUGHT=PASS' "$ANIM_AVAIL_MUT"; then
  echo "ANIMATION-AVAILABILITY-MUTATION=PASS (every rewritten reading reached its extractor)"
else
  echo "ANIMATION-AVAILABILITY-MUTATION=FAIL (a reading was assumed rather than extracted)"; rc_all=1
fi

step "22/30 weapon animation (the product skins models from real demo input)"
# The gate's default output directory holds frozen witness frames. The chain
# redirects it, and the trace directory, into this step's own output, so a chain
# run cannot rewrite a committed frame. The frames are the point: the gate's
# central claim is about the picture, and a reading without its frame is not
# checkable.
WA_OUT="$OUT/22-weapon-animation"
WA_TRACE="$OUT/22-weapon-animation-traces"
WEAPON_ANIMATION_OUT="$WA_OUT" WEAPON_ANIMATION_TRACE_DIR="$WA_TRACE" \
  bash weapon-animation-check.sh > "$OUT/22-weapon-animation.txt" 2>&1
tail -16 "$OUT/22-weapon-animation.txt"
# The assertion count is pinned for the same reason as steps 8 and 21: a gate
# that prints PASS while its assertions quietly disappear is not a gate.
#
# Measured on the T34 tree (task/t34-nearclip), not predicted from the section
# descriptions: the gate printed 56 assertion lines (55 OK + 1 FAIL, the FAIL
# being the identity-skinning pair) with per-section counts 27 / 6 / 5 / 11 / 6 /
# 1, which is why the pin was 56 rather than the 57 T34's report had carried: the
# pin counts the assertions a green run prints, and a pin above that keeps the
# step red forever -- the failure mode this pin exists to prevent, inverted.
#
# T39 replaced section 4's whole-frame byte bound with the measured-silhouette
# rule and its real-pose control (11 -> 12 assertions; see
# T39-WEAPON-ANIMATION-2026-10-21.md), and the forward run on the final tree
# emits 57 OK + 0 FAIL = 57 with per-section counts 27 / 6 / 5 / 12 / 6 / 1.
#
# 2026-10-29, after T60: 57 -> 62, per-section 27 / 6 / 5 / 17 / 6 / 1.
#   T60 (0819fce) keeps the base model's sequence table as the entity's index
#   space and binds each entry to the companion BY NAME, refusing a base sequence
#   whose label has no same-named clip. scout.mdl is that shape (306 base
#   sequences, 303 bound) and `ref` at index 0 is one of the three unbound ones,
#   so index 0 could no longer be section 4's positive sample: it read
#   `source=identity frame=0/0 reason=sequence has no animation binding`. The
#   sample is now index 212 (`run_PRIMARY`, a 49-frame clip) and `ref` is kept as
#   a named negative control -- five new assertions: the positive clip's frame
#   count, and the refusal's source, frame count, reason and bone upload. The
#   count is what the gate emits on this tree with this change (measured run:
#   .scratch/anim-recon-20261029/s22/forward.log), not a number chosen to make
#   the step green: the same gate printed 54 OK + 4 FAIL before the change.
#
# 2026-10-29, review finding: 62 -> 65, per-section 27 / 6 / 5 / 20 / 6 / 1.
#   The sample's identity was implied by the command line rather than read off the
#   product. A 49-frame denominator proves a 49-frame clip was reached, not WHICH
#   clip, so another 49-frame sequence -- or a correctly-bound sequence that is
#   not the intended one -- would have passed. Section 4 now also asserts the
#   index and label the product itself reports (`seq=212`, `reason=run_PRIMARY`)
#   and the index the negative control refused (`seq=0`), all against literals so
#   that editing MODEL_SEQUENCE/MODEL_FRAMES cannot move the expectation, and a
#   field the product did not print reads as empty and fails. Measured:
#   65 OK + 0 FAIL = 65 (.scratch/t60-s22-identity-20261029/forward.log).
#
# 27 = 9 per playback tick in section 1 (capture rc, skinned instances, uploaded
# matrices, GPU-accepted instances, GPU matrices == caller matrices, bind-bone
# mismatches, the weapon-class instance count being reported at all, every
# bind-pose fallback carrying a reason, and a named witness) x 3, + 6 in section
# 2 (four last-tick readings plus the two growth comparisons) + 5 in section 3
# (T34: the pose was published at all three ticks, the camera reading resolved a
# branch, the frames are one picture on a still camera, that frame is a picture
# and not a flat fill, and the frames were drawn through the T29 near plane) +
# 12 in section 4 (five field equalities, the measured-silhouette rule, its
# real-pose control, the sampled-sequence move, the witness frame count, the
# two-tick frame inequality, the tick-following compare, and the
# wrap-at-frame-count compare) + 6 in section 5 (both halves of the bind-pose
# mutation pair, each with its own compared-entity count) + 1 in section 6.
WA_OK=$(grep -c '^  OK   ' "$OUT/22-weapon-animation.txt" || true)
if grep -q 'WEAPON-ANIMATION=PASS' "$OUT/22-weapon-animation.txt" && [ "${WA_OK:-0}" -eq 65 ]; then
  echo "WEAPON-ANIMATION=PASS (assertions_ok=$WA_OK)"
else
  echo "WEAPON-ANIMATION=FAIL (assertions_ok=${WA_OK:-0})"; rc_all=1
fi
# This gate owns three contracts, and the second one is the shape that was
# actually wrong. `--mutation` forces the sampled pose back to the bind pose and
# must turn section 1 red: the entity path's per-instance matrices are what
# carries the pose. `--mutation-model` uploads the model's inverse bind matrices
# on the standalone path -- the historical defect -- and must turn section 4 red:
# identity skin matrices through the skinning branch are what keeps the bind pose
# where it is, and no counter in the program can see the difference.
# `--mutation-identity` moves the identity matrices' model transform by 0.02 of
# the normalized box: the picture moves, no counter moves, and the verdict is
# required to come from the measured-silhouette rule itself (the gate checks the
# failing line's name), which is the rule that replaced the byte bound.
WA_MUT="$OUT/22-weapon-animation-mutation.txt"
WEAPON_ANIMATION_OUT="$OUT/22-weapon-animation-mutation-evidence" \
WEAPON_ANIMATION_TRACE_DIR="$OUT/22-weapon-animation-mutation-traces" \
  bash weapon-animation-check.sh --mutation > "$WA_MUT" 2>&1
if grep -q 'MUTATION-CAUGHT=PASS' "$WA_MUT"; then
  echo "WEAPON-ANIMATION-MUTATION=PASS (the forced bind pose turns the entity contract red)"
else
  echo "WEAPON-ANIMATION-MUTATION=FAIL (the forced bind pose was not caught)"; rc_all=1
fi
WA_MUTMODEL="$OUT/22-weapon-animation-mutation-model.txt"
WEAPON_ANIMATION_OUT="$OUT/22-weapon-animation-mutation-model-evidence" \
WEAPON_ANIMATION_TRACE_DIR="$OUT/22-weapon-animation-mutation-model-traces" \
  bash weapon-animation-check.sh --mutation-model > "$WA_MUTMODEL" 2>&1
if grep -q 'MUTATION-CAUGHT=PASS' "$WA_MUTMODEL"; then
  echo "WEAPON-ANIMATION-MUTATION-MODEL=PASS (the inverse bind matrix on the --model path is caught)"
else
  echo "WEAPON-ANIMATION-MUTATION-MODEL=FAIL (the historical --model defect was not caught)"; rc_all=1
fi
# The third mutation is the one that guards the rule T39 added. The byte bound it
# replaced could only fail on any difference; the silhouette rule must still fail
# when the difference is a moved transform with every counter intact. The gate
# itself requires the failing assertion to be the silhouette rule, so this line
# cannot go green on an unrelated red.
WA_MUTIDENTITY="$OUT/22-weapon-animation-mutation-identity.txt"
WEAPON_ANIMATION_OUT="$OUT/22-weapon-animation-mutation-identity-evidence" \
WEAPON_ANIMATION_TRACE_DIR="$OUT/22-weapon-animation-mutation-identity-traces" \
  bash weapon-animation-check.sh --mutation-identity > "$WA_MUTIDENTITY" 2>&1
if grep -q 'MUTATION-CAUGHT=PASS' "$WA_MUTIDENTITY"; then
  echo "WEAPON-ANIMATION-MUTATION-IDENTITY=PASS (a moved model transform outside the silhouette rule is caught)"
else
  echo "WEAPON-ANIMATION-MUTATION-IDENTITY=FAIL (the silhouette rule did not catch a moved transform)"; rc_all=1
fi

step "23/30 viewmodel animation (the entity's sequence is refused by the drawn model's own table)"
# T2.5. T2 drew the ViewModel in its bind pose and reported animationInput=missing;
# this step is what turns that string into a measurement. Its machine products
# (a trace and two 2.5 MB witness frames per launch) go to the ignored
# evidence/fast tree, so a chain run cannot rewrite anything committed -- the
# committed record is this step's log plus evidence/viewmodel/animation-inputs.txt.
VM_ANIM_TRACE="$OUT/23-viewmodel-animation-traces"
VIEWMODEL_ANIMATION_TRACE_DIR="$VM_ANIM_TRACE" \
  bash viewmodel-animation-check.sh > "$OUT/23-viewmodel-animation.txt" 2>&1
tail -18 "$OUT/23-viewmodel-animation.txt"
# The assertion count is pinned for the same reason as steps 8 and 21-22: a gate
# that prints PASS while its assertions quietly disappear is not a gate. It is
# intentionally read from this gate, not copied from a previous branch report.
#
# 2026-10-20 T29-T33 round: 139 -> 154. Measured twice on the merged tree with
# this gate's own PASS verdict: the chain run (evidence/verify/23-...txt, 154 OK)
# and a standalone rerun (.scratch/final-verify-1/pincheck/23.txt, 154 OK). The
# pin had gone stale when later rounds added the pose/history assertions; the
# same stale-pin shape as exe_count above, and fixed the same way: the number is
# what the gate emits, not a number chosen to make the step green.
#
# 2026-10-20 T34-T38 round: the number above was never applied. b343728 wrote
# this comment and its commit message saying 139 -> 154, and changed step 24's
# 108 -> 114 in the same commit, but left THIS line at 139 - so step 23 has been
# red on every chain run since, with the gate printing its own PASS and the pin
# disagreeing with it. Found by the T34-T38 integration chain run
# (.scratch/final-chain/verify-all.log: VIEWMODEL-ANIMATION=FAIL,
# assertions_ok=154). The number is now the measured 154, and the current
# per-section reading is 5 / 6 / 17 / 13 / 11 / 42 / 48 / 12 = 154 (sections
# 0/7 through 7/7), taken from the chain run's own log rather than from the
# section descriptions. This is a stale-pin repair, not a weakened gate: the same
# pin still goes red when an assertion disappears.
#
# The historical 111 was arrived at as: 5 in section 0 (two recordings pinned by size and sha256, both
# identified, and the verified count) + 5 in section 1 (the self-test rc, its
# failure count, the pinned 49 assertions, the cross-check note, and the
# per-branch work count) + 17 in section 2 (the probe rc, three per-field columns
# on CTFViewModel, the CTFPlayer control, five summary counts, the census count,
# three series readings, the observed range, and two model-path counts) + 11 in
# section 3 (five model readings, two growth comparisons, the two counts that
# separate the trap from the refusal, the entity-model equality and the
# index-space verdict) + 11 in section 4 (four on the POV pairing, the probe rc,
# and six on the recording where the pairing does not resolve) + 25 in section 5
# (sixteen on the refusal and nine on the missing pairing) + 24 in section 6 (the
# derived tick, five on the history run, eight on the accepted pose in bind-pose
# mode, five in applied mode, the effect reading, the coverage pair, the
# all-applied equality and the frame diff) + 12 in section 7 (five paired
# mutations, each with its named assertion and its control, plus the two work
# counts).
#
# 2026-10-29, after T60: the pin stays 154, and the gate's own count on this tree
# is 154 OK + 0 FAIL. Three readings inside it changed, none of them the count:
#   * `pov refusal sequence` was pinned to the literal 29 and is now the invariant
#     `sequence == entitySequence`. The literal was a snapshot of how many frames
#     the run had drawn -- the entity's m_nSequence advances with the demo and the
#     line read is the last one (this run: two observations, both 28).
#   * `pov companion` 51 -> 31 and `pov history` 61 -> 61-from-the-maximum. T60
#     keeps the base table as the index space, so the entity's index now resolves
#     to the same label's clip in the companion instead of the companion's entry
#     at that index (51 frames at index 28 became the `draw` clip's 31). Both read
#     the MAXIMUM over the run rather than the last line, because the last line is
#     whichever sequence the final drawn frame carried (measured: 248 one-frame
#     clips, 1,003 of 31 and 486 of 61 in one history run).
#   * `matcha pairing says nothing was there`: the absence now takes the second
#     branch of entity_model.cpp's message ("no ViewModel entity names the view
#     entity in m_hOwner") because the scan reports ViewModel entities present but
#     none owned by the view entity, which is a more specific absence than the
#     one pinned on 2026-10-22. The rule and the refusal are unchanged.
VM_ANIM_OK=$(grep -c '^  OK   ' "$OUT/23-viewmodel-animation.txt" || true)
if grep -q 'VIEWMODEL-ANIMATION=PASS' "$OUT/23-viewmodel-animation.txt" \
   && [ "${VM_ANIM_OK:-0}" -eq 154 ]; then
  echo "VIEWMODEL-ANIMATION=PASS (assertions_ok=$VM_ANIM_OK)"
else
  echo "VIEWMODEL-ANIMATION=FAIL (assertions_ok=${VM_ANIM_OK:-0})"; rc_all=1
fi
# Section 7's five probe mutations run on every invocation and cost nothing. This
# is the rebuilt, source-level half: the resolver's range branch stops firing, so
# the refusal loses its name (sequenceOutOfRange -> missing), its reason stops
# naming the drawn table, and the out-of-range count collapses. It does NOT make an
# unsafe pose available -- the T1 layer refuses an out-of-range sequence on its own
# -- which is why pose availability is a control that must hold rather than a
# reading that must move. VERIFY_ALL_LOCK_HELD is set because this script already
# owns the build lock -- the same reason step 8 sets it for mutate.sh.
VM_ANIM_MUT="$OUT/23-viewmodel-animation-mutation.txt"
VERIFY_ALL_LOCK_HELD=1 VIEWMODEL_ANIMATION_TRACE_DIR="$OUT/23-viewmodel-animation-mutation-traces" \
  bash viewmodel-animation-check.sh --mutation > "$VM_ANIM_MUT" 2>&1
tail -10 "$VM_ANIM_MUT"
if grep -q 'MUTATION-CAUGHT=PASS' "$VM_ANIM_MUT"; then
  echo "VIEWMODEL-ANIMATION-MUTATION=PASS (the neutered range branch turns the refusal red)"
else
  echo "VIEWMODEL-ANIMATION-MUTATION=FAIL (the range branch was not load bearing)"; rc_all=1
fi

step "24/30 viewmodel companion proof (the pass draws the model named by the entity)"
# The companion path is the product-side closure of T2.5: owner, model index,
# model path equality, sequence table membership and bone coverage are checked
# against the model actually uploaded by the pass. It runs under the chain lock,
# so its source mutation must restore the tree while leaving the parent lock in
# place.
VM_COMPANION_OUT="$OUT/24-viewmodel-companion.txt"
VIEWMODEL_COMPANION_TRACE_DIR="$OUT/24-viewmodel-companion-traces" \
  VERIFY_ALL_LOCK_HELD=1 bash viewmodel-companion-check.sh > "$VM_COMPANION_OUT" 2>&1
tail -18 "$VM_COMPANION_OUT"
# 2026-10-20 T29-T33 round: 108 -> 114, measured the same way as step 23 above
# (chain run and a standalone rerun both emitted 114 OK lines with the gate's own
# PASS). The three viewmodel-companion sections that grew were the pose-applied
# and control assertions; the count is pinned so a section that silently stops
# running is red, not a smaller number nobody reads.
#
# 2026-10-22 T49.1: 114 -> 125. The gate gained section 1b, the retry self-test
# (11 assertions: three for deny-then-ok, three for always-deny, three for
# real-failure and two for the wait bound). Those are real assertions that run on
# every invocation, so they belong in the count. This pin is the reason the T49.1
# chain run was red at 125 before the number was updated -- the failure was the
# pin doing its job, not a gate defect, and it is recorded rather than quietly
# fixed: a count that moves without an explanation is exactly what the pin exists
# to catch.
VM_COMPANION_OK=$(grep -c '^  OK   ' "$VM_COMPANION_OUT" || true)
if grep -q 'VIEWMODEL-COMPANION=PASS' "$VM_COMPANION_OUT" \
   && [ "${VM_COMPANION_OK:-0}" -eq 125 ]; then
  echo "VIEWMODEL-COMPANION=PASS (assertions_ok=$VM_COMPANION_OK)"
else
  echo "VIEWMODEL-COMPANION=FAIL (assertions_ok=${VM_COMPANION_OK:-0})"; rc_all=1
fi
# THE TWO GATES CANNOT OVERLAP THE PRODUCT IMAGE (T49).
#   The forward gate above ends with a product launch, and the mutation gate below
#   starts its own launches seconds later. Windows keeps the executable locked
#   until the last handle closes, so in the T49 chain (2026-10-22 01:30) the
#   mutation half began 4 s after the forward half's last exit and every launch
#   before its first rebuild failed with rc=126 "Permission denied", turning 86
#   assertions red for a reason that had nothing to do with the proof. The gate
#   now retries that one shape itself (see run_product in
#   viewmodel-companion-check.sh); this wait makes the handover explicit rather
#   than relying on the first launch to discover it, and it is bounded so a
#   permanently locked file still fails.
wait_for_companion_image() { # wait until no product image is still running or releasing
  local deadline pid_count idle=0
  deadline=$(( $(date +%s) + 60 ))
  echo "  settling the product image before the mutation half"
  while :; do
# The COMMAND column only: a grep of the whole ps line also matches any process
    # whose arguments mention the binary, including the gate that is doing the
    # waiting. That false positive makes "a product is still alive" always true.
    pid_count=$(ps -W 2>/dev/null | awk '{print $NF}' | grep -ci 'tf2_demo_native' || true)
    if [ "${pid_count:-0}" -eq 0 ]; then
      # Two consecutive clear readings: "no process" alone is what the forward
      # half's last launch leaves behind for the few seconds Windows still holds
      # the image, and the whole point of this wait is to outlast that window.
      idle=$((idle + 1))
      if [ "$idle" -ge 4 ]; then
        echo "  product processes=0 for 2s; the mutation half may start"
        return 0
      fi
    else
      idle=0
    fi
    if [ "$(date +%s)" -ge "$deadline" ]; then
      echo "  note: the product image stayed busy for 60s (processes=${pid_count:-?}); running anyway"
      return 1
    fi
    sleep 0.5
  done
}
# THE WAIT IS A PRECONDITION, NOT A COURTESY (T49.1).
#   The first version called this and threw the status away, so a product image that
#   stayed busy for the whole 60 s bound still went on to run the mutation half --
#   which is exactly the state that produced the original false red (a launch that
#   cannot exec the image reports Permission denied and every dependent assertion
#   reads a frame that was never drawn). A wait whose failure is not acted on is
#   not a guard: it is a delay with a comment. So the mutation half now runs ONLY
#   when the image actually settled, and a timeout is a named red that skips it.
#   Skipping is deliberate: a mutation suite run against an unavailable image would
#   report "not caught" for reasons that have nothing to do with the proof, and a
#   verdict built from that is worse than no verdict.
if wait_for_companion_image; then
  VM_COMPANION_MUT="$OUT/24-viewmodel-companion-mutation.txt"
  VERIFY_ALL_LOCK_HELD=1 VIEWMODEL_COMPANION_TRACE_DIR="$OUT/24-viewmodel-companion-mutation-traces" \
    bash viewmodel-companion-check.sh --mutation > "$VM_COMPANION_MUT" 2>&1
  tail -12 "$VM_COMPANION_MUT"
  if grep -q 'MUTATION-CAUGHT=PASS' "$VM_COMPANION_MUT"; then
    echo "VIEWMODEL-COMPANION-MUTATION=PASS (all six proof mutations were caught)"
  else
    echo "VIEWMODEL-COMPANION-MUTATION=FAIL (a companion proof mutation was not caught)"; rc_all=1
  fi
else
  echo "VIEWMODEL-COMPANION-MUTATION=SKIPPED (the product image did not settle; see the wait above)"; rc_all=1
fi

step "25/30 player animation (the demo's ticks drive a player's cycle, one link at a time)"
# T61. The demo does not carry a player's m_nSequence, so the product derives the
# animation input from the entity's own velocity and the playback tick. Each link
# of that chain is asserted separately (sequence table, pose, continuity, clock
# rate, tick script), with three negative controls first, so a link that stops
# working is a named red rather than a smaller number of green lines. The counter
# is pinned: a section that silently stops running is red.
# 2026-10-30 T6: step 19's entity-property-lookup mutation edits
# native/src/entity_model.cpp and restores it, then rebuilds only
# entity_model_probe; the model_pose_probe that this gate reads is still the one
# step 1 built, so the gate's own STALE-BINARY guard refuses to measure it (as it
# should). Rebuild the probe here, so the reading describes the tree. The guard
# stays a guard: it refuses on a dirty tree, and the caller decides to rebuild.
bash build-target.sh model_pose_probe > "$OUT/25-probe-build.txt" 2>&1
ANIMATION_BUILD_RC=$?
if [ "$ANIMATION_BUILD_RC" -ne 0 ]; then
  tail -20 "$OUT/25-probe-build.txt"
  echo "PLAYER-ANIMATION-CHECK=FAIL (model_pose_probe rebuild rc=$ANIMATION_BUILD_RC; see $OUT/25-probe-build.txt)"
  rc_all=1
fi
ANIMATION_OUT="$OUT/25-player-animation"
PLAYER_ANIMATION_OUT="$ANIMATION_OUT" bash player-animation-check.sh > "$ANIMATION_OUT.txt" 2>&1
ANIMATION_RC=$?
tail -8 "$ANIMATION_OUT.txt"
ANIMATION_OK=$(grep -c '^  OK ' "$ANIMATION_OUT.txt" || true)
# 2026-10-25 T61: 213 = 209 + 4 determinism (the same probe twice must give the
# same pose digest: repeat exit, repeat line count, lines to compare, digest).
# Six sections: freshness, negative controls, A (nine class
# models x 6 readings), B (four cycle points x 2 readings + checksum, per class),
# C (continuity), D (clock rate), E (tick script, synthetic and demo).
#
# 2026-10-26 T61.1 review 2: 232 = 213 + 11 matcher + 8 entity id. Two sections
# were added because two properties the reviewer named had NO assertion anywhere:
#
#   1b/6 matcher (11) -- the selector and the ranker must agree about ORDER.
#     `rankSequenceCandidates` nested the needle outside the index while
#     `chooseSequenceByNeedles` nests the index outside, so the probe could try,
#     and report a pose for, a sequence the product would never pick first. The
#     probe's --self-test existed but NO gate called it, which is why a unit test
#     is worthless until a section runs it and pins every field.
#
#   1c/6 entity id (8) -- the id view paints body k red k/255 and the reader
#     decodes a pixel by that byte alone, so two ids sharing a colour merge two
#     entities silently. The encoder now refuses instead of clamping and the
#     product exits 17 rather than report a merged frame; the encoder is unit
#     tested because the scene cannot reach the ceiling (the draw budget is 96).
if [ "$ANIMATION_RC" -eq 0 ] && grep -q 'PLAYER-ANIMATION-CHECK=PASS' "$ANIMATION_OUT.txt" && [ "$ANIMATION_OK" = "232" ]; then
  echo "PLAYER-ANIMATION-CHECK=PASS (232 assertions)"
else
  echo "PLAYER-ANIMATION-CHECK=FAIL (rc=$ANIMATION_RC ok=$ANIMATION_OK expected 232)"; rc_all=1
fi

step "26/30 player animation pixels (the body is a shaded surface, and the drive moves it)"
# T61. The end of the chain above, measured in the frame: the entity pass's own
# mask, the body's colours and luminance inside it, and the share of those pixels
# that change when --player-animation is removed at the same scene tick. The mask
# is not a hardcoded box, so a pass that stops drawing there fails.
ANIM_PIXEL_OUT="$OUT/26-animation-pixels"
T61_PIXEL_OUT="$ANIM_PIXEL_OUT" bash player-animation-pixels.sh > "$OUT/26-animation-pixels.txt" 2>&1
ANIM_PIXEL_RC=$?
tail -10 "$OUT/26-animation-pixels.txt"
ANIM_PIXEL_OK=$(grep -c '^  OK ' "$OUT/26-animation-pixels.txt" || true)
# 2026-10-25 T61: 19 = 17 + 2. Freshness, FOUR captures (coverage/animated/off
# and a repeat of animated, which must land the same ticks and be byte-identical,
# so the on/off pair is a controlled A-B), six
# identity readings that exist in both runs, three frames non-empty, and five
# pixel assertions from t61-pixels.py.
# 2026-10-26 T61.1: the step went 19 -> 26 assertions, and the first review is why.
# It rejected scene-level evidence: the gate merged every model legal sequence
# into one set and compared that set with a scene line that carries no entity
# identity, so a single body drawing the wrong clip hid behind another model
# legal clip. F5 answers it with a fifth capture under --world-entity-id, which
# paints each drawn instance its own red and prints one ledger line per
# instance; the decoder t61-entityid.py joins id -> model -> pick -> sample ->
# pixels on ONE named body, and its band over the ledger judges every drawn
# player instance, not only the eight the sample trace prints. The withdrawn
# idle assertion is the only line this step lost; the four F5 lines are what it
# gained. Measured on this tree: 26 OK / 0 FAIL; ENTITY-ID=PASS, LEDGER
# entries=96, DRAWN 2, MOTION 86.56%.
#
# 2026-10-26 T61.1 review 2: 26 -> 28. The reviewer caught the identity frame
# clock assertions being permissive: frame_tick was only checked when non-empty,
# so a capture that never published one passed, and scene_tick was printed but
# never asserted. Both are now mandatory, and scene_tick must equal the value the
# animated capture reported -- the identity frame is compared pixel-for-pixel
# against that frame, so a different moment would make the comparison
# meaningless rather than wrong.
# Measured on this tree: 28 OK / 0 FAIL (64161/74481 in all three captures).
# The verdict grepped here is the GATE'S OWN, not the inner tool's. Until
# 2026-10-26 this line matched 'PIXEL-CHECK=PASS', which t61-pixels.py prints
# and the gate only echoes on a pass -- so it happened to work, but it named a
# quantity one level below the thing being judged. Step 25 had the same shape
# and did NOT happen to work: it grepped a token the gate never printed at all
# (see the note there). Both gates now print `NAME=PASS` and both steps grep it.
if [ "$ANIM_PIXEL_RC" -eq 0 ] && grep -q 'PLAYER-ANIMATION-PIXELS=PASS' "$OUT/26-animation-pixels.txt" && [ "$ANIM_PIXEL_OK" = "28" ]; then
  echo "PLAYER-ANIMATION-PIXELS=PASS (28 assertions)"
else
  echo "PLAYER-ANIMATION-PIXELS=FAIL (rc=$ANIM_PIXEL_RC ok=$ANIM_PIXEL_OK expected 28)"; rc_all=1
fi

step "27/30 SourceTV relay camera (the death handover runs on the wall clock, and the corpse leaves the set)"
# T66. A SourceTV recording is watched by nobody in particular: the camera has to
# follow the match itself. Three clauses of that are asserted here, and each one is
# invisible to the reading the others use.
#   * the death is detected and the body leaves the submitted set -- read from the
#     product's own corpse-filter line, naming the entity indices that left, because
#     a hidden instance that is off camera changes no pixel (the T66 report measures
#     the corpse 4200 units outside the frame, which is why the picture is the
#     negative control here and not the evidence);
#   * the handover between players takes about a third of a second and travels
#     rather than cutting -- read from intermediate trace lines, since a cut has the
#     same two endpoints;
#   * the handover clock is the wall clock, so pausing the recording must not freeze
#     a handover in flight -- read from a run that pauses for two seconds after the
#     first death, where the same scene tick must appear on many lines with the
#     progress rising. Every other section reads tick-keyed quantities and stays
#     green on a frozen relay; that is the whole reason section 8 exists.
# The gate refuses a stale binary first (BINARY-FRESH=1) and runs three negative
# controls, so a skipped run is a named red instead of a smaller number.
SPECTATE_OUT="$OUT/27-sourceTV-relay"
SPECTATE_OUT="$SPECTATE_OUT" bash spectate-check.sh > "$OUT/27-sourceTV-relay.txt" 2>&1
SPECTATE_RC=$?
tail -8 "$OUT/27-sourceTV-relay.txt"
SPECTATE_OK=$(grep -c '^  OK ' "$OUT/27-sourceTV-relay.txt" || true)
# 71 is the floor, not the count: sections 0/8 through 8/8 hold 71 unconditional
# assertions, and the pause section adds two more (>= 8 handover lines on one tick,
# >= 5 progress values) that only read on a host that gave the window at least 8
# frames -- on a starved host the gate prints a NOTE and skips them. So a healthy
# host prints 73 and a starved host prints 71; pinning either number exactly fails
# the other one (PGD-9, 2026-11-01). A floor keeps the property that matters here --
# a gate that prints PASS while its assertions quietly disappear is not a gate --
# without measuring the host's frame rate.
if [ "$SPECTATE_RC" -eq 0 ] && grep -q 'SPECTATE-CHECK=PASS' "$OUT/27-sourceTV-relay.txt" && [ "$SPECTATE_OK" -ge 71 ]; then
  echo "SPECTATE-CHECK=PASS ($SPECTATE_OK assertions, floor 71)"
else
  echo "SPECTATE-CHECK=FAIL (rc=$SPECTATE_RC ok=$SPECTATE_OK expected >= 71)"; rc_all=1
fi

step "28/30 world geometry at the pinned pose (every empty pixel is the map's own sky)"
# T58. This gate was NOT in the chain, and that is exactly how its pins rotted:
# T26's frames were photographed on a tree that predates the T63-F1 clock fix
# (5ee9642). Before that fix, scene queries resolved entity history by the signon
# entry tick while frames advance on the server net_Tick, so "the camera at
# playback tick 2000" was a pose from deep inside the map, and nothing noticed for
# three days because no chain step ran this file. T58 re-measured at the verified
# pose and re-pinned; this step is what stops the next silent drift.
#
# Cost and reuse: the matrix is 18 frames at ~55 s each. It is reused only when
# the committed identity still matches this executable -- exe hash, demo and map
# hashes, tick, window flag, capture flags and the capture script itself, per
# world-geometry-hole-capture.sh. A fresh Release build therefore recaptures the
# matrix once (~16 min); the pixels are deterministic (T58 proved it by rebuilding
# from restored sources and reproducing the frame byte for byte), so the recapture
# reproduces the committed frames and leaves the tree clean.
#
# If a later wave changes world rendering ON PURPOSE -- T59's material fixes do --
# the two frame pins (world-geometry-hole-check.sh, PIN_TEXTURED_SHA /
# PIN_REF_SHA) go red HERE until that wave re-pins them with its own attribution.
# T35 and T58 are the two worked examples of that procedure. Do not re-pin to make
# this chain green: the pin may only move after the changed pixels are explained,
# and the re-pin commit must say which reading moved and why.
#
# One frame difference is NOT tolerated by the two SHA pins but IS tolerated by
# the arithmetic pins (missing / sky / enclosed / seam / cull), which is the
# intended split: a material change moves the picture and must be acknowledged, a
# geometry change moves the account.
GEOMETRY_OUT="$OUT/28-world-geometry"
mkdir -p "$GEOMETRY_OUT"
GEOMETRY_LOG="$GEOMETRY_OUT/geometry.txt"
bash world-geometry-hole-check.sh > "$GEOMETRY_LOG" 2>&1
GEOMETRY_RC=$?
tail -10 "$GEOMETRY_LOG"
GEOMETRY_OK=$(grep -c '^  PASS ' "$GEOMETRY_LOG" || true)
# 53 is the forward assertion count measured on 6c26fb2+T58 (2026-10-25, owner
# t58-repin: 53 OK / 0 FAIL). Pinned for the same reason as steps 8, 14, 22, 23
# and 27: a gate that prints PASS while its assertions quietly disappear is not a
# gate. Re-measure, never copy, when a later wave adds an assertion.
if [ "$GEOMETRY_RC" -eq 0 ] && grep -q '^PASS: every empty pixel' "$GEOMETRY_LOG" \
    && [ "$GEOMETRY_OK" = "53" ]; then
  echo "WORLD-GEOMETRY-CHECK=PASS (53 assertions)"
else
  echo "WORLD-GEOMETRY-CHECK=FAIL (rc=$GEOMETRY_RC ok=$GEOMETRY_OK expected 53)"; rc_all=1
fi
step "29/30 world materials: a patch wrapper's include reaches the pixels"
# T59. A map's own pak ships "patch" VMTs: the wrapper is only {include, replace}
# and the shader with its $basetexture lives in the included file. The product used
# to read the wrapper's root only, refuse it as "no $basetexture", and paint it a
# flat grey through the untiled branch -- pl_odyssey lost 14 materials that way.
# T56 had documented the limitation; no chain step ran this gate, so it stayed.
#
# This step is the one that would have caught it. Section 8/8 looks straight down
# at maps/cp_snakewater_final1/tile/floor_tile_004a_5934_-1624_-24, an include-only
# wrapper, and asserts the albedo view carries >= 100 distinct colours with no
# colour owning more than 50% of the frame. Measured on the two builds before the
# bounds were written: 1 colour (share 1.0000) when the wrapper is refused, 169-236
# colours (modal share 0.24-0.28) when the include is followed. Mutant m7 in the
# gate refuses the wrapper again and requires the flat frame, so the section cannot
# pass by accident.
#
# 88 is the forward assertion count measured on 480cd20 + the T59b report
# (2026-10-26T00:56, owner lead-covpin: 88 OK / 0 FAIL, MATERIAL-COVERAGE=PASS,
# log .scratch/covpin.log). It was 87 on c06e82c+T59 (owner t59-covfwd) and moved
# because T59b added one assertion: the wrapper-ledger closure that replaces the
# tautological resolved==declared (86 assert_ calls in the gate, was 85).
# Pinned for the same reason as the other steps: a gate that prints PASS while
# its assertions quietly disappear is not a gate. Do not re-pin to make this
# chain green; a re-pin must name the reading that moved and why.
COV_OUT="$OUT/29-world-materials"
mkdir -p "$COV_OUT"
COV_LOG="$COV_OUT/coverage.txt"
MATERIAL_COVERAGE_OUT="$COV_OUT/evidence" MATERIAL_COVERAGE_SCRATCH="$COV_OUT/scratch" \
  bash material-coverage-check.sh > "$COV_LOG" 2>&1
COV_RC=$?
tail -10 "$COV_LOG"
COV_OK=$(grep -c '^  OK' "$COV_LOG" || true)
if [ "$COV_RC" -eq 0 ] && grep -q '^MATERIAL-COVERAGE=PASS' "$COV_LOG" \
    && [ "$COV_OK" = "88" ]; then
  echo "MATERIAL-COVERAGE-CHECK=PASS (88 assertions)"
else
  echo "MATERIAL-COVERAGE-CHECK=FAIL (rc=$COV_RC ok=$COV_OK expected 88)"; rc_all=1
fi
# 30. the review workspace, driven by the product ---------------------------------
# T71/T72 shipped as a store plus a probe. Every assertion passed while the only
# writer was the probe, and the product's own two-run roundtrip was unmeasured --
# and the first product run wrote a file the product could not read back (the
# writer keyed camera slots by the slot NUMBER, the parser by the ARRAY POSITION;
# a probe fixture that builds 0-based slots agrees with the codec forever).
# Section 8 of this gate drives four real runs -- create, reopen + jump, a wrong
# demo hash, a corrupt file -- and reads only trace lines and the bytes of the
# file the product wrote, so "the probe can save a workspace" cannot stand in for
# "the player can save a workspace".
#
# 186 is the count measured on this tree (2026-10-25, owner lead-t71-gate3:
# 186 OK / 0 FAIL). Pinned for the same reason as every other step, and the same
# rule applies: do not re-pin to make this chain green.
step "30/30 review workspace (the product writes a file it can read back)"
WS_OUT="$OUT/30-review-workspace"
mkdir -p "$WS_OUT"
WS_LOG="$WS_OUT/check.txt"
REVIEW_WORKSPACE_OUT="$WS_OUT/evidence" bash review-workspace-check.sh > "$WS_LOG" 2>&1
WS_RC=$?
tail -6 "$WS_LOG"
WS_OK=$(grep -c '^OK ' "$WS_LOG" || true)
if [ "$WS_RC" -eq 0 ] && grep -q '^REVIEW-WORKSPACE-CHECK=PASS' "$WS_LOG" \
    && [ "$WS_OK" = "186" ]; then
  echo "REVIEW-WORKSPACE-CHECK=PASS (186 assertions)"
else
  echo "REVIEW-WORKSPACE-CHECK=FAIL (rc=$WS_RC ok=$WS_OK expected 186)"; rc_all=1
fi
if [ "$rc_all" -eq 0 ]; then echo "VERIFY=PASS"; else echo "VERIFY=FAIL"; fi
exit "$rc_all"

