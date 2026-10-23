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

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
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
  series.classId = 264;
  series.className = "CTFProjectile_Rocket";
  series.firstTick = 100;
  series.lastTick = 100 + static_cast<std::int32_t>(count) - 1;
  for (std::size_t i = 0; i < count; ++i) {
    const float t = static_cast<float>(i);
    tf2::native::ProjectileTrackSample sample;
    sample.tick = 100 + static_cast<std::int32_t>(i);
    sample.entityIndex = 0;
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
  const auto push = [&rows](std::uint16_t entity, std::int32_t classId, std::int32_t tick, float z) {
    ProjectileTrackSample sample;
    sample.entityIndex = entity; sample.classId = classId; sample.tick = tick;
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
    bouncing.classId = 217;
    bouncing.className = "CTFGrenadePipebombProjectile";
    const float kZ[] = {431.0f, 423.0f, 416.0f, 409.0f, 402.0f, 394.0f, 392.0f, 397.0f, 399.0f};
    for (std::size_t i = 0; i < 9; ++i) {
      ProjectileTrackSample sample;
      sample.tick = 101 + static_cast<std::int32_t>(i);
      sample.entityIndex = 1;
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
    monotonic.classId = 217;
    monotonic.className = "CTFGrenadePipebombProjectile";
    for (std::size_t i = 0; i < 9; ++i) {
      ProjectileTrackSample sample;
      sample.tick = 101 + static_cast<std::int32_t>(i);
      sample.entityIndex = 2; sample.classId = 217;
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

} // namespace

int main(int argc, char** argv) {
  bool selfTest = false;
  std::string tfRoot, demoPath;
  int top = 5;
  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--self-test") == 0) selfTest = true;
    else if (std::strcmp(argv[i], "--tf-root") == 0 && i + 1 < argc) tfRoot = argv[++i];
    else if (std::strcmp(argv[i], "--demo") == 0 && i + 1 < argc) demoPath = argv[++i];
    else if (std::strcmp(argv[i], "--top") == 0 && i + 1 < argc) top = std::atoi(argv[++i]);
  }
  if (selfTest) return runSelfTest();
  std::printf("usage: projectile_track_probe --self-test | --tf-root <tf> --demo <demo.dem>\n");
  return 2;
}
