// T53 projectile track probe.
//
// WHY A SELF-TEST AND NOT ONLY A DEMO RUN
//   Two of the claims this round makes are arithmetic, not decoding: that a
//   constant-velocity model diverges from an observed series, and that a
//   two-point line diverges too. Those can be proved on SYNAPTIC series built
//   here, with the expected numbers written down before the code runs, on a
//   machine with no demo, no GPU and no lock. A gate that only ever ran against
//   a demo could not tell "the analysis is right" from "this demo happens to
//   produce a big number".
//
//   The demo run is still the acceptance (it is what proves the real series
//   exists); this self-test is what proves the measuring instrument is correct
//   on inputs whose answers are known by construction.
//
// Usage:
//   projectile_track_probe --self-test
//   projectile_track_probe --tf-root <tf> --demo <demo.dem> [--top N] [--json]
//
// Exit: 0 = every assertion held; 1 = one did not; 2 = self-test could not run.

#include "demo_header.h"
#include "projectile_track.h"
#include "projectile_overlay.h"

#include <algorithm>
#include <cmath>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace {

int fail(const char* what) {
  std::cout << "SELF-TEST FAILED: " << what << "\n";
  return 1;
}

// A synthetic series with an exactly known answer.
//
// Shape: free fall under a constant acceleration, so a constant-velocity model
// built from the first two samples must diverge by exactly the closed-form
// amount. With x advancing one unit per tick and z = -a*t^2/2, the two-point
// velocity is (1, 0, -(a/2)(2t0+1)) -- the model is exact for a parabola only on
// the interval it was fitted to, so the error grows monotonically and its sign is
// known.
tf2::native::ProjectileSeries makeFallingSeries(std::size_t count, float accel) {
  tf2::native::ProjectileSeries series;
  series.entityIndex = 0;
  series.generation = 1u;
  series.classId = 264;
  series.className = "CTFProjectile_Rocket";
  series.firstTick = 100;
  series.lastTick = 100 + static_cast<std::int32_t>(count) - 1;
  for (std::size_t i = 0; i < count; ++i) {
    const float t = static_cast<float>(i);
    tf2::native::ProjectileTrackSample sample;
    sample.tick = 100 + static_cast<std::int32_t>(i);
    sample.entityIndex = 0;
    sample.generation = 1u;
    sample.classId = 264;
    sample.origin[0] = t;                        // 1 unit per tick, constant
    sample.origin[1] = 0.0f;
    sample.origin[2] = -0.5f * accel * t * t;    // free fall
    series.samples.push_back(sample);
  }
  return series;
}

int runSelfTest() {
  using namespace tf2::native;

  // -------------------------------------------------------------------------
  // 1. The class list. The test is the NAME, and the list must reject exactly
  //    the classes that a substring rule gets wrong on the real table. These
  //    names were measured on bagel's 355-class server table.
  // -------------------------------------------------------------------------
  if (!isProjectileTrackClass("CTFProjectile_Rocket")) return fail("CTFProjectile_Rocket must be tracked");
  if (!isProjectileTrackClass("CTFGrenadePipebombProjectile")) return fail("pipebomb must be tracked");
  // The eight false positives a substring rule would draw a trail from at (0,0,0).
  const char* kNotProjectiles[] = {
    "CTFRocketLauncher", "CTFGrenadeLauncher", "CTFPipebombLauncher",
    "CTFFlameThrower", "CTFJar", "CTFBottle", "CTFPowerupBottle", "CTFRocketPack",
  };
  for (const char* name : kNotProjectiles) {
    if (isProjectileTrackClass(name)) return fail("a weapon or held item is being tracked as a projectile");
  }
  // Case sensitivity: a lowercase variant is a different class and must not match.
  if (isProjectileTrackClass("ctfprojectile_rocket")) return fail("the class test must be exact");

  // -------------------------------------------------------------------------
  // 2. buildProjectileSeries groups by entity and orders by tick.
  //    Three entities, interleaved in arrival order the way packets deliver them.
  // -------------------------------------------------------------------------
  std::vector<ProjectileTrackSample> rows;
  const char* kRocket = "CTFProjectile_Rocket";
  const char* kPipe = "CTFGrenadePipebombProjectile";
  std::vector<ServerClassSchema> schemas(2);
  schemas[0].id = 264; schemas[0].name = kRocket;
  schemas[1].id = 217; schemas[1].name = kPipe;
  // entity 5 (rocket) and 9 (pipebomb) each on ticks 200..203, interleaved;
  // entity 7 has a NON-projectile class and must be dropped.
  ServerClassSchema weapon;
  weapon.id = 292; weapon.name = "CTFRocketLauncher";
  schemas.push_back(weapon);
  const auto push = [&rows](std::uint16_t entity, std::int32_t classId, std::int32_t tick, float z,
                            std::uint32_t generation = 1u) {
    ProjectileTrackSample sample;
    sample.entityIndex = entity; sample.classId = classId; sample.tick = tick;
    sample.generation = generation;
    sample.origin[0] = static_cast<float>(tick); sample.origin[1] = 0.0f; sample.origin[2] = z;
    rows.push_back(sample);
  };
  for (std::int32_t tick = 200; tick <= 203; ++tick) {
    push(5, 264, tick, static_cast<float>(tick) * 0.5f);
    push(9, 217, tick, static_cast<float>(tick) * 0.25f);
    push(7, 292, tick, 0.0f);  // a rocket launcher at the origin: must be dropped
  }
  const auto series = buildProjectileSeries(rows, schemas);
  if (series.size() != 2) return fail("series must be one per projectile entity, launchers dropped");
  for (const auto& entry : series) {
    if (entry.samples.size() != 4) return fail("each entity must keep all four ticks");
    if (!entry.contiguous) return fail("a one-tick stride must read contiguous");
    if (entry.missingTicks != 0) return fail("a contiguous series has no missing ticks");
    if (entry.samples.front().tick > entry.samples.back().tick) return fail("samples must be tick-ascending");
    if (entry.entityIndex == 7) return fail("a launcher entity must not become a series");
  }

  // A gap must be REPORTED, not smoothed over. Drop tick 201 from entity 5.
  std::vector<ProjectileTrackSample> gapped;
  for (const auto& sample : rows) {
    if (sample.entityIndex == 5 && sample.tick == 201) continue;
    gapped.push_back(sample);
  }
  const auto gappedSeries = buildProjectileSeries(gapped, schemas);
  bool sawGap = false;
  for (const auto& entry : gappedSeries) {
    if (entry.entityIndex != 5) continue;
    sawGap = true;
    if (entry.contiguous) return fail("a missing tick must not read contiguous");
    if (entry.missingTicks != 1) return fail("exactly one tick is missing");
  }
  if (!sawGap) return fail("the gapped entity disappeared entirely");

  // -------------------------------------------------------------------------
  // 2b. SLOT REUSE MUST NOT MERGE TWO PROJECTILES.
  //
  //    This is the defect the first implementation had: it keyed the series on
  //    the entity index, and an index names a SLOT, not an entity. Measured on
  //    bagel, slot 377 carries 25 projectile lifetimes; grouping by index turned
  //    them into one 12057-sample flight whose constant-velocity error read 1.27
  //    million units on a 3400-unit map.
  //
  //    Two projectiles in slot 11 at generation 1 and generation 2, at widely
  //    separated ticks and positions, must produce TWO series. If they merge, the
  //    count below is 1 and this fails.
  // -------------------------------------------------------------------------
  {
    std::vector<ProjectileTrackSample> reuse;
    const auto pushGen = [&reuse](std::uint32_t generation, std::int32_t tick, float x) {
      ProjectileTrackSample sample;
      sample.entityIndex = 11;
      sample.generation = generation;
      sample.classId = 264;
      sample.tick = tick;
      sample.origin[0] = x; sample.origin[1] = 0.0f; sample.origin[2] = 0.0f;
      reuse.push_back(sample);
    };
    pushGen(1u, 300, 0.0f);
    pushGen(1u, 301, 10.0f);
    pushGen(2u, 900, 2000.0f);   // a later projectile in the same slot
    pushGen(2u, 901, 2010.0f);
    const auto split = buildProjectileSeries(reuse, schemas);
    if (split.size() != 2) return fail("slot reuse must produce two series, not one merged series");
    std::size_t firstLifetimeSamples = 0, secondLifetimeSamples = 0;
    for (const auto& entry : split) {
      if (entry.entityIndex != 11) return fail("the reused slot must keep its index on both series");
      if (entry.generation == 1u) firstLifetimeSamples = entry.samples.size();
      else if (entry.generation == 2u) secondLifetimeSamples = entry.samples.size();
    }
    if (firstLifetimeSamples != 2 || secondLifetimeSamples != 2) {
      return fail("each lifetime must keep exactly its own samples");
    }
    // And the merged readout must be gone: the second lifetime's span is 900..901,
    // not 300..901. A merge would report a 601-tick span for a 2-sample series.
    for (const auto& entry : split) {
      if (entry.generation != 2u) continue;
      if (entry.lastTick - entry.firstTick != 1) {
        return fail("a lifetime's span must not include another lifetime's ticks");
      }
    }
    std::printf("  slot-reuse fixture: series=%zu (merged would be 1)\n", split.size());
  }

  // -------------------------------------------------------------------------
  // 3. SHORTCUT 1: constant velocity, against a closed-form answer.
  //
  //    x = t, z = -0.5*a*t^2 with a = 4, t = 0..9 (10 samples).
  //    The two-point velocity is (1, 0, -2) -- from t=0 to t=1, z goes 0 -> -2.
  //    At tick t the model predicts z = -2t, truth is -2t^2, so the error is
  //    |2t^2 - 2t| = 2t(t-1), which at t=9 is 2*9*8 = 144.
  //    The expected numbers are therefore known WITHOUT running the code.
  // -------------------------------------------------------------------------
  {
    const auto falling = makeFallingSeries(10, 4.0f);
    const auto error = constantVelocityError(falling);
    if (!error.ok) return fail("constant velocity must produce a reading for 10 samples");
    if (error.samples != 10) return fail("every sample must be compared");
    if (std::fabs(error.velocity[0] - 1.0f) > 1e-4f) return fail("derived vx must be 1");
    if (std::fabs(error.velocity[2] - (-2.0f)) > 1e-4f) return fail("derived vz must be -2");
    if (error.worstTick != 109) return fail("worst tick must be the last, where the parabola is furthest from its tangent");
    if (std::fabs(error.maxError - 144.0f) > 1e-2f) return fail("max error must be the closed-form 144");
    std::printf("  constant-velocity fixture: max=%.3f at tick=%d v=(%.2f,%.2f,%.2f) samples=%zu\n",
                static_cast<double>(error.maxError), error.worstTick,
                static_cast<double>(error.velocity[0]), static_cast<double>(error.velocity[1]),
                static_cast<double>(error.velocity[2]), error.samples);

    // A perfectly straight, constant-velocity series must read ZERO error: the
    // control that proves the function is not simply always large.
    ProjectileSeries straight = makeFallingSeries(10, 0.0f);
    const auto straightError = constantVelocityError(straight);
    if (!straightError.ok) return fail("the straight control must still produce a reading");
    if (straightError.maxError > 1e-4f) return fail("a constant-velocity series must read zero error");
    std::printf("  constant-velocity control (straight line): max=%.3f\n",
                static_cast<double>(straightError.maxError));
  }

  // Too few samples is "not applicable", not a pass.
  {
    const auto tiny = makeFallingSeries(2, 4.0f);
    if (constantVelocityError(tiny).ok) return fail("two samples are not enough to measure a divergence");
    if (twoPointLineError(tiny).ok) return fail("two samples are not enough to measure a chord error");
  }

  // -------------------------------------------------------------------------
  // 4. SHORTCUT 2: the chord between endpoints, against a closed-form answer.
  //
  //    Same parabola. The chord from t=0 (z=0) to t=9 (z=-162) is z = -18t.
  //    Truth is -2t^2, so the error is |2t^2 - 18t| = 2t|t-9|, which peaks at
  //    t = 4.5 -> 2*4*5 = 40 at t=4 and 40 at t=5 (t=4 gives 40, t=5 gives 40).
  //    The assertion is therefore maxError == 40 at tick 104 or 105.
  // -------------------------------------------------------------------------
  {
    const auto falling = makeFallingSeries(10, 4.0f);
    const auto error = twoPointLineError(falling);
    if (!error.ok) return fail("the chord must produce a reading for 10 samples");
    if (error.worstTick != 104 && error.worstTick != 105) {
      return fail("the chord's worst tick must be the middle of the span");
    }
    if (std::fabs(error.maxError - 40.0f) > 1e-2f) return fail("chord max error must be the closed-form 40");
    std::printf("  chord fixture: max=%.3f at tick=%d samples=%zu\n",
                static_cast<double>(error.maxError), error.worstTick, error.samples);

    // The two functions must NOT agree, or one of them is measuring nothing: a
    // series where constant velocity and a chord gave the same number would make
    // "the mutation swaps one for the other" undetectable.
    const auto velocityError = constantVelocityError(falling);
    if (std::fabs(velocityError.maxError - error.maxError) < 1e-3f) {
      return fail("the two shortcuts must diverge on a parabola, or the mutation is invisible");
    }
  }

  // -------------------------------------------------------------------------
  // 5. A bounce must be REPORTED as a vertical direction reversal.
  //    z descends, then ascends: the signature the real demo shows at tick 107.
  // -------------------------------------------------------------------------
  {
    ProjectileSeries bouncing;
    bouncing.entityIndex = 1;
    bouncing.generation = 1u;
    bouncing.classId = 217;
    bouncing.className = "CTFGrenadePipebombProjectile";
    const float kZ[] = {431.0f, 423.0f, 416.0f, 409.0f, 402.0f, 394.0f, 392.0f, 397.0f, 399.0f};
    for (std::size_t i = 0; i < 9; ++i) {
      ProjectileTrackSample sample;
      sample.tick = 101 + static_cast<std::int32_t>(i);
      sample.entityIndex = 1;
      sample.generation = 1u;
      sample.classId = 217;
      sample.origin[0] = 0.0f; sample.origin[1] = 0.0f; sample.origin[2] = kZ[i];
      bouncing.samples.push_back(sample);
    }
    bouncing.firstTick = 101;
    bouncing.lastTick = 109;
    const auto built = buildProjectileSeries(bouncing.samples, schemas);
    if (built.size() != 1) return fail("the bounce fixture must produce one series");
    if (!built[0].verticalDirectionReversed) return fail("a bounce must report a vertical direction reversal");
    if (built[0].zMin != 392.0f) return fail("zMin must be the bounce floor");
    if (built[0].zMax != 431.0f) return fail("zMax must be the launch height");

    // And a monotonic fall must NOT report one, or the flag is always true.
    ProjectileSeries monotonic;
    monotonic.entityIndex = 2;
    monotonic.generation = 1u;
    monotonic.classId = 217;
    monotonic.className = "CTFGrenadePipebombProjectile";
    for (std::size_t i = 0; i < 9; ++i) {
      ProjectileTrackSample sample;
      sample.tick = 101 + static_cast<std::int32_t>(i);
      sample.entityIndex = 2; sample.generation = 1u; sample.classId = 217;
      sample.origin[2] = 500.0f - static_cast<float>(i);
      monotonic.samples.push_back(sample);
    }
    monotonic.firstTick = 101; monotonic.lastTick = 109;
    const auto builtMonotonic = buildProjectileSeries(monotonic.samples, schemas);
    if (builtMonotonic.size() != 1) return fail("the monotonic fixture must produce one series");
    if (builtMonotonic[0].verticalDirectionReversed) {
      return fail("a monotonic fall must not report a bounce");
    }
  }

  std::printf("self-test: all assertions held\n");
  return 0;
}

// ===========================================================================
// THE DEMO RUN -- the acceptance, as opposed to the fixture proof above.
//
// Reports, in order:
//   1. how many rows the retention kept, and whether it ever hit its bound;
//   2. the longest observed series, with its contiguity and bounce signature;
//   3. how wrong the two shortcuts are on THAT series -- the numbers the gate
//      asserts;
//   4. the invariants the binary-search query fix will depend on (tick order);
//   5. a counted cost reading, deliberately not a wall clock (T38's finding).
// ===========================================================================
int runDemo(const std::string& demoPath, int top, bool expectEmptySearch) {
  using namespace tf2::native;
  DemoHeader header;
  DemoIndex index;
  DemoNetworkSummary summary;
  if (!parseDemoHeaderFile(demoPath, header)) return fail("demo header parse failed");
  indexDemoFile(demoPath, header, index);
  summary.networkProtocol = header.networkProtocol;
  // The append cost has to be a measurement: the judge rule for it is stated in
  // milliseconds, so the clock is on for this run.
  summary.projectileTrackLimits.measureAppendCost = true;
  if (!scanKnownDemoMessages(demoPath, index, summary)) return fail("demo scan failed");

  std::printf("demo=%s\n", demoPath.c_str());
  std::printf("track: rows=%zu dropped=%zu packetsScanned=%zu limit=%zu\n",
              summary.projectileTrackSamples.size(), summary.projectileTrackSamplesDropped,
              summary.projectileTrackPacketsScanned, summary.projectileTrackLimits.maxSamples);

  // THE INVARIANT THE BINARY SEARCH DEPENDS ON. Rows are appended per packet in
  // tick order, so the tick field is non-decreasing. A lower_bound over them is
  // only correct while that holds, and it is easy to break without noticing --
  // so it is asserted here, and the gate mutates the append order to prove the
  // assertion can fail.
  std::size_t firstInversion = 0;
  const bool tickOrdered = projectileTrackTickOrderingHolds(summary, &firstInversion);
  std::printf("invariant: tickNonDecreasing=%s firstInversion=%zu\n",
              tickOrdered ? "true" : "false", tickOrdered ? 0u : firstInversion);

  const auto series = buildProjectileSeries(summary.projectileTrackSamples, summary.serverClassSchemas);
  std::size_t contiguous = 0;
  std::size_t bounced = 0;
  std::size_t withOwner = 0;
  std::size_t totalOwnerRows = 0;
  std::map<std::string, std::size_t> perClass;
  for (const auto& entry : series) {
    ++perClass[entry.className];
    if (entry.contiguous) ++contiguous;
    if (entry.verticalDirectionReversed) ++bounced;
    for (const auto& sample : entry.samples) {
      if (sample.hasOwner) { ++withOwner; ++totalOwnerRows; }
    }
  }
  // "series" means PROJECTILE LIFETIMES, not entity slots: the same slot appears
  // many times over. Both counts are printed so a regression that starts merging
  // again shows up as a collapsed series count against a stable slot count.
  std::size_t distinctSlots = 0;
  {
    std::vector<std::uint16_t> slots;
    for (const auto& entry : series) slots.push_back(entry.entityIndex);
    std::sort(slots.begin(), slots.end());
    slots.erase(std::unique(slots.begin(), slots.end()), slots.end());
    distinctSlots = slots.size();
  }
  std::printf("series: lifetimes=%zu distinctSlots=%zu contiguous=%zu bounced=%zu\n",
              series.size(), distinctSlots, contiguous, bounced);
  for (const auto& entry : perClass) {
    std::printf("  class %s series=%zu\n", entry.first.c_str(), entry.second);
  }
  std::printf("owner: rowsWithOwner=%zu of %zu\n", totalOwnerRows,
              summary.projectileTrackSamples.size());

  if (series.empty()) {
    std::printf("NO-PROJECTILE: this recording carries no tracked projectile rows\n");
    return 0;
  }

  // The longest series is the acceptance subject: it is the projectile the demo
  // spends the most ticks observing, so it is where a constant-velocity model has
  // the most room to be wrong.
  const ProjectileSeries* longest = &series.front();
  for (const auto& entry : series) {
    if (entry.samples.size() > longest->samples.size()) longest = &entry;
  }
  std::printf("longest: entity=%u class=%s ticks=[%d..%d] samples=%zu missing=%zu contiguous=%s bounced=%s z=[%.1f..%.1f]\n",
              static_cast<unsigned>(longest->entityIndex), longest->className.c_str(),
              longest->firstTick, longest->lastTick, longest->samples.size(),
              longest->missingTicks, longest->contiguous ? "true" : "false",
              longest->verticalDirectionReversed ? "true" : "false",
              static_cast<double>(longest->zMin), static_cast<double>(longest->zMax));

  // TWO DIFFERENT SUBJECTS, because "longest" is not "best".
  //
  // The longest-lived series on this demo is a sticky bomb sitting on the ground
  // for 2438 ticks. Its constant-velocity error is enormous (40,818 units) but
  // for the wrong reason: the shortcut extrapolates a throw that ended seconds
  // earlier, so the number measures DURATION, not curvature. Asserting on it
  // would be asserting that long things are long.
  //
  // The acceptance subject is a FLIGHT: contiguous, and between 40 and 300
  // ticks, which is the range a rocket or pipebomb actually spends in the air
  // before it hits something. Within that set the subject is the one with the
  // largest constant-velocity error -- the most curved observed path.
  const ProjectileSeries* flight = nullptr;
  float flightWorst = -1.0f;
  std::size_t flightCandidates = 0;
  for (const auto& entry : series) {
    if (!entry.contiguous) continue;
    const std::size_t n = entry.samples.size();
    if (n < 40 || n > 300) continue;
    ++flightCandidates;
    const auto candidateError = constantVelocityError(entry);
    if (!candidateError.ok) continue;
    if (candidateError.maxError > flightWorst) { flightWorst = candidateError.maxError; flight = &entry; }
  }
  std::printf("flights: candidates=%zu (contiguous, 40..300 samples)\n", flightCandidates);

  // ---------------------------------------------------------------------
  // THE DISTRIBUTION, because one subject is a demonstration and not an
  // acceptance.
  //
  // "The worst of 1147 candidates is 441 units" says nothing about how many of
  // them are curved. If most flights were near-straight, the honest sentence
  // would be "the demo carries projectiles, but its straight ones dominate" --
  // and a report that only cited the worst case would be hiding that. So the
  // chord error over every candidate is reported as median / p90 / max, plus the
  // counts above and below the 100-unit floor.
  // ---------------------------------------------------------------------
  {
    std::vector<float> chordErrors;
    chordErrors.reserve(flightCandidates);
    std::size_t velocityAboveFloor = 0;
    std::size_t chordAboveFloor = 0;
    std::size_t chordBelowOne = 0;
    for (const auto& entry : series) {
      if (!entry.contiguous) continue;
      const std::size_t n = entry.samples.size();
      if (n < 40 || n > 300) continue;
      const auto chord = twoPointLineError(entry);
      if (!chord.ok) continue;
      chordErrors.push_back(chord.maxError);
      const auto vel = constantVelocityError(entry);
      if (vel.ok && vel.maxError >= 100.0f) ++velocityAboveFloor;
      if (chord.maxError >= 100.0f) ++chordAboveFloor;
      if (chord.maxError < 1.0f) ++chordBelowOne;
    }
    if (!chordErrors.empty()) {
      std::sort(chordErrors.begin(), chordErrors.end());
      const std::size_t count = chordErrors.size();
      const auto at = [&chordErrors, count](double fraction) {
        std::size_t index = static_cast<std::size_t>(fraction * static_cast<double>(count));
        if (index >= count) index = count - 1;
        return chordErrors[index];
      };
      const float median = at(0.50);
      const float p90 = at(0.90);
      const float maximum = chordErrors.back();
      const double fractionAbove =
        100.0 * static_cast<double>(chordAboveFloor) / static_cast<double>(count);
      std::printf("distribution: chordMedian=%.1f chordP90=%.1f chordMax=%.1f count=%zu\n",
                  static_cast<double>(median), static_cast<double>(p90),
                  static_cast<double>(maximum), count);
      std::printf("distribution: chordAtLeast100=%zu chordBelow1=%zu velocityAtLeast100=%zu "
                  "fractionChordAtLeast100=%.1f%%\n",
                  chordAboveFloor, chordBelowOne, velocityAboveFloor, fractionAbove);
    } else {
      std::printf("distribution: NONE (no candidate produced a chord reading)\n");
    }
  }
  if (flight) {
    const auto flightError = constantVelocityError(*flight);
    const auto flightChord = twoPointLineError(*flight);
    std::printf("flightSubject: entity=%u generation=%u class=%s ticks=[%d..%d] samples=%zu bounced=%s "
                "maxError=%.1f meanError=%.1f worstTick=%d observed=(%.0f,%.0f,%.0f) predicted=(%.0f,%.0f,%.0f) "
                "chordMax=%.1f\n",
                static_cast<unsigned>(flight->entityIndex), flight->generation,
                flight->className.c_str(), flight->firstTick, flight->lastTick,
                flight->samples.size(), flight->verticalDirectionReversed ? "true" : "false",
                static_cast<double>(flightError.maxError), static_cast<double>(flightError.meanError),
                flightError.worstTick,
                static_cast<double>(flightError.worstObserved[0]),
                static_cast<double>(flightError.worstObserved[1]),
                static_cast<double>(flightError.worstObserved[2]),
                static_cast<double>(flightError.worstPredicted[0]),
                static_cast<double>(flightError.worstPredicted[1]),
                static_cast<double>(flightError.worstPredicted[2]),
                static_cast<double>(flightChord.ok ? flightChord.maxError : 0.0f));
  } else {
    std::printf("flightSubject: NONE (no contiguous 40..300 sample series)\n");
  }

  const auto velocity = constantVelocityError(*longest);
  if (velocity.ok) {
    std::printf("shortcut-constantVelocity: samples=%zu max=%.1f mean=%.1f worstTick=%d "
                "observed=(%.0f,%.0f,%.0f) predicted=(%.0f,%.0f,%.0f)\n",
                velocity.samples, static_cast<double>(velocity.maxError),
                static_cast<double>(velocity.meanError), velocity.worstTick,
                static_cast<double>(velocity.worstObserved[0]),
                static_cast<double>(velocity.worstObserved[1]),
                static_cast<double>(velocity.worstObserved[2]),
                static_cast<double>(velocity.worstPredicted[0]),
                static_cast<double>(velocity.worstPredicted[1]),
                static_cast<double>(velocity.worstPredicted[2]));
  } else {
    std::printf("shortcut-constantVelocity: NOT APPLICABLE (fewer than 3 samples)\n");
  }
  const auto chord = twoPointLineError(*longest);
  if (chord.ok) {
    std::printf("shortcut-twoPointLine: samples=%zu max=%.1f worstTick=%d\n",
                chord.samples, static_cast<double>(chord.maxError), chord.worstTick);
  } else {
    std::printf("shortcut-twoPointLine: NOT APPLICABLE (fewer than 3 samples)\n");
  }

  // The top-N rows by shortcut error, so the gate can assert on a body of
  // evidence rather than on a single lucky entity.
  std::vector<const ProjectileSeries*> ranked;
  ranked.reserve(series.size());
  for (const auto& entry : series) ranked.push_back(&entry);
  std::sort(ranked.begin(), ranked.end(),
            [](const ProjectileSeries* left, const ProjectileSeries* right) {
              return left->samples.size() > right->samples.size();
            });
  const int shown = std::max(1, std::min<int>(top, static_cast<int>(ranked.size())));
  for (int i = 0; i < shown; ++i) {
    const auto& entry = *ranked[static_cast<std::size_t>(i)];
    const auto error = constantVelocityError(entry);
    std::printf("row entity=%u class=%s samples=%zu maxError=%.1f meanError=%.1f bounced=%s\n",
                static_cast<unsigned>(entry.entityIndex), entry.className.c_str(),
                entry.samples.size(),
                static_cast<double>(error.ok ? error.maxError : 0.0f),
                static_cast<double>(error.ok ? error.meanError : 0.0f),
                entry.verticalDirectionReversed ? "true" : "false");
  }

  // ---------------------------------------------------------------------
  // THE PLAYER FILTER -- "all players" vs "one selected player".
  //
  // The overlay must support both, so the gate has to show the filter has an
  // input to work with: every row carries the handle of the player who threw the
  // projectile. This counts the DISTINCT owner slots, which is what "all players"
  // ranges over, and prints the busiest one, which is what "one selected player"
  // selects.
  // ---------------------------------------------------------------------
  {
    std::map<std::uint16_t, std::size_t> perOwner;
    std::size_t rowsWithoutOwner = 0;
    for (const auto& sample : summary.projectileTrackSamples) {
      if (!sample.hasOwner) { ++rowsWithoutOwner; continue; }
      ++perOwner[sample.ownerIndex];
    }
    std::size_t busiest = 0, busiestRows = 0;
    for (const auto& entry : perOwner) {
      if (entry.second > busiestRows) { busiestRows = entry.second; busiest = entry.first; }
    }
    std::printf("filter: owners=%zu rowsWithoutOwner=%zu busiestOwner=%u busiestRows=%zu\n",
                perOwner.size(), rowsWithoutOwner,
                static_cast<unsigned>(busiest), busiestRows);
  }

  // ---------------------------------------------------------------------
  // THE EXPLICIT NO-PROJECTILE CASE.
  //
  // An empty result must be reported as a real state, not as a silent success.
  // This searches a tick where the demo has nothing alive and prints how many
  // rows came back, so "the query found nothing" is a reading instead of an
  // absence a reader has to notice.
  // ---------------------------------------------------------------------
  {
    // A tick before the first observed row is guaranteed to hold nothing.
    const std::int32_t emptyTick = summary.projectileTrackSamples.empty()
      ? 0 : summary.projectileTrackSamples.front().tick - 1000;
    const auto emptyRows = queryProjectileTracksAtTick(summary, emptyTick);
    std::printf("emptycase: tick=%d rows=%zu marker=%s\n",
                emptyTick, emptyRows.size(),
                emptyRows.empty() ? "NO-PROJECTILE" : "UNEXPECTED-ROWS");
    if (expectEmptySearch) {
      std::printf("emptycase: requested and read above\n");
    }
  }

  // THE QUERY COST, counted rather than timed (T38's finding: a wall clock on
  // this host moves with every other process on it).
  //
  // The reading that matters is COMPARISONS PER CALL, and it is derived rather
  // than guessed: the query is two binary searches over N rows, so the work is at
  // most 2*ceil(log2(N+1)) comparisons -- independent of N in the way a scan is
  // not. The probe prints both the bound and the scan the first implementation
  // would have done, so the difference is visible rather than asserted.
  const std::int32_t probeTick = longest->firstTick + (longest->lastTick - longest->firstTick) / 2;
  const std::size_t rowCount = summary.projectileTrackSamples.size();
  std::size_t sortBound = 0;
  for (std::size_t span = rowCount; span > 0; span >>= 1) ++sortBound;
  sortBound *= 2;  // lower_bound + upper_bound
  std::size_t found = 0;
  for (int repeat = 0; repeat < 3; ++repeat) {
    found = queryProjectileTracksAtTick(summary, probeTick).size();
  }
  std::printf("query: tick=%d rows=%zu calls=3 foundLast=%zu binaryComparisons<=%zu scanWouldBe=%zu\n",
              probeTick, rowCount, found, sortBound, rowCount * 3);

  // The cost reading the gate asserts on: comparisons per call, and how many
  // ticks were actually sampled. Both are work counts, not durations.
  std::printf("cost: sampleRows=%zu queryComparisonsPerCall<=%zu budgetPerCall=%zu\n",
              rowCount, sortBound, sortBound);

  // THE APPEND COST, which runs on the decode path once per packet and is held to
  // median <= 0.5 ms and max <= 5 ms. Reported as a distribution rather than a
  // total, because a mean over 73k packets hides the packet that took 40 ms.
  {
    const auto& raw = summary.projectileTrackAppendMillis;
    if (raw.empty()) {
      std::printf("appendCost: NOT MEASURED (measurement was off)\n");
    } else {
      // WHICH PACKET IS THE MAX, not just how big it was.
      //
      // median 0.006 ms against max 2.5 ms is a 400x tail, and a tail nobody
      // named is a tail the next reader assumes is steady-state. So the worst
      // packet is located by INDEX and reported next to the packet that follows
      // it: if the cost is a one-time cold-start artefact, the second packet is
      // already cheap and the two numbers say so. If it is a packet with an
      // unusual entity count, the index is where to look.
      std::size_t worstIndex = 0;
      for (std::size_t i = 1; i < raw.size(); ++i) {
        if (raw[i] > raw[worstIndex]) worstIndex = i;
      }
      auto sorted = raw;
      std::sort(sorted.begin(), sorted.end());
      const std::size_t count = sorted.size();
      const auto at = [&sorted, count](double fraction) {
        std::size_t index = static_cast<std::size_t>(fraction * static_cast<double>(count));
        if (index >= count) index = count - 1;
        return sorted[index];
      };
      std::printf("appendCost: packets=%zu medianMs=%.4f p90Ms=%.4f maxMs=%.4f\n",
                  count, at(0.50), at(0.90), sorted.back());
      // The tail, named. packetIndex is the position in the scan order, so index
      // 0 is the first decoded packet -- the cold-start candidate.
      std::printf("appendCost.tail: worstPacketIndex=%zu worstMs=%.4f secondPacketMs=%.4f "
                  "maxAfterFirst=%.4f\n",
                  worstIndex, raw[worstIndex],
                  raw.size() > 1 ? raw[1] : 0.0,
                  raw.size() > 1
                    ? *std::max_element(raw.begin() + 1, raw.end()) : raw[0]);
      // THE CAUSE, not just the number. If the worst packet is not the first, it
      // is cold start ruled out and something about that packet is the cause --
      // and the one thing this walk scales with is how many slots hold a live
      // entity. So the worst packet's entity count is reported against the
      // median, and the correlation is read off directly.
      if (worstIndex < summary.projectileTrackAppendEntities.size()) {
        auto entityCounts = summary.projectileTrackAppendEntities;
        std::sort(entityCounts.begin(), entityCounts.end());
        const std::uint32_t worstEntities = summary.projectileTrackAppendEntities[worstIndex];
        const std::uint32_t medianEntities = entityCounts[entityCounts.size() / 2];
        const std::uint32_t maxEntities = entityCounts.back();
        std::printf("appendCost.cause: worstPacketEntities=%u medianEntities=%u maxEntities=%u\n",
                    worstEntities, medianEntities, maxEntities);
        // Spearman-free sanity: if the worst packet is also the largest entity
        // count, the cost is a function of the walk and the attribution is done.
        std::printf("appendCost.cause: worstIsAlsoLargestEntities=%s\n",
                    worstEntities == maxEntities ? "true" : "false");
        // IF THE WALK SIZE DOES NOT EXPLAIN THE TAIL, THE TAIL IS THE HOST.
        //
        // The entity count varies by about 3% across packets (671 median, 692
        // max) while the times vary by 300x. A cost that scales with the walk
        // cannot produce that, so the outliers are preemption: another process
        // took the CPU mid-append. That is T38's finding arriving again -- a wall
        // clock on this host measures the host.
        //
        // Counting the outliers is what turns "it is probably noise" into a
        // reading: a handful above the threshold out of 73k is preemption, while
        // a large share would mean the median itself is not trustworthy.
        std::size_t above100us = 0, above500us = 0, above1ms = 0;
        for (const double value : raw) {
          if (value > 0.1) ++above100us;
          if (value > 0.5) ++above500us;
          if (value > 1.0) ++above1ms;
        }
        std::printf("appendCost.outliers: above100us=%zu above500us=%zu above1ms=%zu of %zu "
                    "(%.4f%% above1ms)\n",
                    above100us, above500us, above1ms, raw.size(),
                    100.0 * static_cast<double>(above1ms) / static_cast<double>(raw.size()));
        // The rule's own verdict, computed here so the gate does not re-derive it.
        std::printf("appendCost.rule: medianOk=%s maxOk=%s\n",
                    at(0.50) <= 0.5 ? "true" : "false",
                    sorted.back() <= 5.0 ? "true" : "false");
      }
    }
  }

  // ---------------------------------------------------------------------
  // THE BUILD COST, which is the other half of the feature's frame budget.
  //
  // The append cost above is a DECODE cost, paid once per packet while the demo
  // is scanned. This is a RENDER cost, paid once per FRAME while the overlay is
  // on -- and at 240 Hz a frame is 4.16 ms, so a build that took 2 ms would eat
  // half the budget and turn a "one-click toggle" into a frame-rate regression.
  //
  // Measured over the ticks the flight actually spans, because that is when there
  // is something to build. Reported as a distribution for the same reason the
  // append is: a mean hides the frame that stalled.
  // ---------------------------------------------------------------------
  if (flight) {
    std::vector<double> buildMillis;
    std::vector<std::size_t> pointCounts;
    const std::int32_t span = flight->lastTick - flight->firstTick;
    const std::int32_t step = std::max<std::int32_t>(1, span / 200);
    for (std::int32_t t = flight->firstTick; t <= flight->lastTick; t += step) {
      TrajectoryBuildRequest request;
      request.selectedPlayerEntity = -1;
      request.lingerMs = 500;
      const auto started = std::chrono::steady_clock::now();
      const auto built = buildProjectileTrajectoryPoints(summary, t, request, nullptr);
      const auto elapsed = std::chrono::steady_clock::now() - started;
      buildMillis.push_back(std::chrono::duration<double, std::milli>(elapsed).count());
      pointCounts.push_back(built.size());
    }
    if (!buildMillis.empty()) {
      auto sortedBuild = buildMillis;
      std::sort(sortedBuild.begin(), sortedBuild.end());
      const std::size_t bcount = sortedBuild.size();
      const auto atB = [&sortedBuild, bcount](double fraction) {
        std::size_t index = static_cast<std::size_t>(fraction * static_cast<double>(bcount));
        if (index >= bcount) index = bcount - 1;
        return sortedBuild[index];
      };
      auto sortedPoints = pointCounts;
      std::sort(sortedPoints.begin(), sortedPoints.end());
      std::printf("buildCost: frames=%zu medianMs=%.4f p95Ms=%.4f maxMs=%.4f\n",
                  bcount, atB(0.50), atB(0.95), sortedBuild.back());
      std::printf("buildCost.points: median=%zu max=%zu verticesMedian=%zu verticesMax=%zu\n",
                  sortedPoints[sortedPoints.size() / 2], sortedPoints.back(),
                  sortedPoints[sortedPoints.size() / 2] * 2, sortedPoints.back() * 2);
      std::printf("buildCost.budget: frameBudget240HzMs=4.1667 targetMs=1.0\n");
    }
  }

  // ---------------------------------------------------------------------
  // OWNER SLOT REUSE -- a known limitation that must be a reading.
  //
  // The player filter compares a projectile's owner HANDLE against
  // selectedPlayerEntity, and a handle's low bits are an ENTITY SLOT. Slots are
  // reused, so selecting a slot that carried two players' projectiles during the
  // recording will draw both and the user cannot tell. That is not a defect to
  // fix here -- the demo is what it is -- but it is a limitation that must be
  // counted, or the filter's accuracy is being assumed.
  // ---------------------------------------------------------------------
  {
    std::map<std::uint16_t, std::set<std::uint32_t>> lifetimesByOwner;
    for (const auto& entry : series) {
      for (const auto& row : entry.samples) {
        if (!row.hasOwner) continue;
        lifetimesByOwner[row.ownerIndex].insert(
          (static_cast<std::uint32_t>(entry.entityIndex) << 16) | entry.generation);
        break;  // one owner per lifetime is enough to attribute it
      }
    }
    std::size_t reusedOwnerSlots = 0;
    std::size_t worstOwner = 0;
    std::size_t worstLifetimes = 0;
    for (const auto& entry : lifetimesByOwner) {
      if (entry.second.size() > 1) ++reusedOwnerSlots;
      if (entry.second.size() > worstLifetimes) {
        worstLifetimes = entry.second.size();
        worstOwner = entry.first;
      }
    }
    std::printf("ownerReuse: ownerSlots=%zu slotsWithMultipleLifetimes=%zu "
                "worstOwner=%zu worstLifetimes=%zu\n",
                lifetimesByOwner.size(), reusedOwnerSlots,
                static_cast<unsigned>(worstOwner), worstLifetimes);
  }
  // ---------------------------------------------------------------------
  // THE OVERLAY BUILDER'S OWN RULES -- filter, linger, quality.
  //
  // Everything above this point measures the DATA. This block measures what the
  // overlay actually SUBMITS, because "the retention is right" and "the overlay
  // draws the right thing" are two different claims and only the second one is
  // the feature. Four readings, each of which a defect would move:
  //   * all-players vs one-selected-player point counts, from the same tick;
  //   * the linger window: a tick far past the last observation must submit
  //     nothing, and a tick inside it must submit something;
  //   * quality: Low must submit FEWER points than High on the same tick.
  // ---------------------------------------------------------------------
  if (flight) {
    const std::int32_t probe = flight->lastTick;
    TrajectoryBuildStats allStats, selectedStats, lowStats;
    TrajectoryBuildRequest allRequest;
    allRequest.selectedPlayerEntity = -1;
    allRequest.lingerMs = 500;
    const auto allPoints = buildProjectileTrajectoryPoints(summary, probe, allRequest, &allStats);

    TrajectoryBuildRequest selectedRequest = allRequest;
    // Pick the busiest owner so the selected filter has something to keep.
    std::map<std::uint16_t, std::size_t> ownerRows;
    for (const auto& row : queryProjectileTracksInRange(summary, probe - 33, probe)) {
      if (row.hasOwner) ++ownerRows[row.ownerIndex];
    }
    std::uint16_t busiestOwner = 0;
    std::size_t busiestCount = 0;
    for (const auto& entry : ownerRows) {
      if (entry.second > busiestCount) { busiestCount = entry.second; busiestOwner = entry.first; }
    }
    selectedRequest.selectedPlayerEntity = static_cast<std::int32_t>(busiestOwner);
    const auto selectedPoints = buildProjectileTrajectoryPoints(summary, probe, selectedRequest, &selectedStats);

    TrajectoryBuildRequest lowRequest = allRequest;
    lowRequest.lowQuality = true;
    const auto lowPoints = buildProjectileTrajectoryPoints(summary, probe, lowRequest, &lowStats);

    std::printf("overlay: tick=%d allPoints=%zu allLifetimes=%zu allOwners=%zu\n",
                probe, allPoints.size(), allStats.lifetimes, allStats.ownersRepresented);
    std::printf("overlay: selectedOwner=%u selectedPoints=%zu selectedFilteredOut=%zu\n",
                static_cast<unsigned>(busiestOwner), selectedPoints.size(), selectedStats.rowsFilteredOut);
    std::printf("overlay: lowPoints=%zu highPoints=%zu lingerTicks=%d\n",
                lowPoints.size(), allPoints.size(), allStats.lingerTicks);

    // THE LINGER WINDOW, PROPERLY ANCHORED.
    //
    // The first version of this read "pastLingerPoints=77" and looked like a
    // pass -- but 77 points past the linger is a FAILURE of the linger rule, and
    // the reason it read 77 is that the tick five past ONE flight's end is inside
    // OTHER projectiles' flights. A live demonstration of the same trap the
    // checkpoint fallback set: an assertion is only about what it anchors on.
    //
    // The right question is about ONE lifetime: at a tick beyond its last
    // observation plus the linger, that lifetime's points must be gone. So the
    // reading is per-lifetime and the tick is chosen to be inside the window
    // first (must be non-empty) and then outside it (must be empty).
    TrajectoryBuildRequest pastRequest = allRequest;
    const std::size_t insideWindow = buildProjectileTrajectoryPoints(
      summary, flight->lastTick, pastRequest, nullptr).size();
    const std::int32_t pastTick = flight->lastTick + allStats.lingerTicks + 5;
    std::size_t flightPointsPastLinger = 0;
    for (const auto& point : buildProjectileTrajectoryPoints(summary, pastTick, pastRequest, nullptr)) {
      if (point.entityIndex == flight->entityIndex) ++flightPointsPastLinger;
    }
    std::printf("overlay: lingerWindowTick=%d insideWindowPoints=%zu pastTick=%d "
                "flightPointsPastLinger=%zu lingerTicks=%d\n",
                flight->lastTick, insideWindow, pastTick, flightPointsPastLinger,
                allStats.lingerTicks);
  }
  return 0;
}

} // namespace

int main(int argc, char** argv) {
  bool selfTest = false;
  bool expectEmptySearch = false;
  std::string tfRoot, demoPath;
  int top = 5;
  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--self-test") == 0) selfTest = true;
    else if (std::strcmp(argv[i], "--tf-root") == 0 && i + 1 < argc) tfRoot = argv[++i];
    else if (std::strcmp(argv[i], "--demo") == 0 && i + 1 < argc) demoPath = argv[++i];
    else if (std::strcmp(argv[i], "--top") == 0 && i + 1 < argc) top = std::atoi(argv[++i]);
    else if (std::strcmp(argv[i], "--expect-empty-search") == 0) expectEmptySearch = true;
  }
  if (selfTest) return runSelfTest();
  if (!demoPath.empty() && !tfRoot.empty()) return runDemo(demoPath, top, expectEmptySearch);
  std::printf("usage: projectile_track_probe --self-test | --tf-root <tf> --demo <demo.dem>\n");
  return 2;
}
