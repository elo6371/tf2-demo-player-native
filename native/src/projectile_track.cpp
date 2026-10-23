#include "projectile_track.h"

#include <algorithm>
#include <cmath>
#include <map>

namespace tf2::native {
namespace {

float distance(const float a[3], const float b[3]) {
  const float dx = a[0] - b[0];
  const float dy = a[1] - b[1];
  const float dz = a[2] - b[2];
  return std::sqrt(dx * dx + dy * dy + dz * dz);
}

} // namespace

// Splits the retained rows into one series per entity.
//
// The rows arrive in the order the packets were decoded, which puts every entity
// of one tick next to each other but interleaves entities across ticks. Grouping
// is by entity index; ordering inside a series is by tick, because a consumer
// that walked the rows in arrival order would draw a trail that jumps between
// projectiles.
//
// Rows whose class is not on the tracked list are dropped, and so is a row whose
// class id is not in this recording's table. That is not defensive padding: the
// retention writes rows only for tracked classes, so a row failing the test means
// the class list and the retained rows have disagreed -- and a series built from
// a disagreement would be a trail for something that is not a projectile.
std::vector<ProjectileSeries> buildProjectileSeries(
    const std::vector<ProjectileTrackSample>& samples,
    const std::vector<ServerClassSchema>& classSchemas) {
  std::vector<ProjectileSeries> series;
  std::map<std::uint16_t, std::size_t> byEntity;
  for (const auto& sample : samples) {
    std::string className;
    for (const auto& schema : classSchemas) {
      if (schema.id == static_cast<std::uint32_t>(sample.classId)) { className = schema.name; break; }
    }
    if (!isProjectileTrackClass(className)) continue;
    auto found = byEntity.find(sample.entityIndex);
    if (found == byEntity.end()) {
      ProjectileSeries fresh;
      fresh.entityIndex = sample.entityIndex;
      fresh.classId = sample.classId;
      fresh.className = className;
      fresh.firstTick = sample.tick;
      fresh.lastTick = sample.tick;
      found = byEntity.emplace(sample.entityIndex, series.size()).first;
      series.push_back(std::move(fresh));
    }
    auto& target = series[found->second];
    target.lastTick = std::max(target.lastTick, sample.tick);
    target.firstTick = std::min(target.firstTick, sample.tick);
    target.samples.push_back(sample);
  }

  for (auto& entry : series) {
    std::sort(entry.samples.begin(), entry.samples.end(),
              [](const ProjectileTrackSample& left, const ProjectileTrackSample& right) {
                return left.tick < right.tick;
              });
    // A duplicated tick would make "missingTicks" arithmetic wrong and would
    // draw the same point twice; keep the first of each tick.
    entry.samples.erase(
      std::unique(entry.samples.begin(), entry.samples.end(),
                  [](const ProjectileTrackSample& left, const ProjectileTrackSample& right) {
                    return left.tick == right.tick;
                  }),
      entry.samples.end());
    if (entry.samples.empty()) { entry.contiguous = false; continue; }
    entry.zMin = entry.samples.front().origin[2];
    entry.zMax = entry.samples.front().origin[2];
    bool anyReversal = false;
    float previousStep = 0.0f;
    bool haveStep = false;
    for (std::size_t i = 0; i < entry.samples.size(); ++i) {
      entry.zMin = std::min(entry.zMin, entry.samples[i].origin[2]);
      entry.zMax = std::max(entry.zMax, entry.samples[i].origin[2]);
      if (i == 0) continue;
      const float step = entry.samples[i].origin[2] - entry.samples[i - 1].origin[2];
      // A sign flip between consecutive vertical steps is the bounce/deflection
      // signature. Zero steps are ignored: a projectile at rest on the ground
      // would otherwise count as reversing every time it stopped moving.
      if (haveStep && step != 0.0f && previousStep != 0.0f
          && ((step > 0.0f) != (previousStep > 0.0f))) {
        anyReversal = true;
      }
      if (step != 0.0f) { previousStep = step; haveStep = true; }
    }
    entry.verticalDirectionReversed = anyReversal;
    const std::int32_t span = entry.lastTick - entry.firstTick;
    const std::size_t expected = span >= 0 ? static_cast<std::size_t>(span) + 1u : 0u;
    entry.missingTicks = expected > entry.samples.size() ? expected - entry.samples.size() : 0u;
    entry.contiguous = entry.missingTicks == 0 && !entry.samples.empty();
  }
  return series;
}

// SHORTCUT 1: constant velocity from the first two observed samples.
//
// The velocity comes from samples 0 and 1 rather than from a least-squares fit
// over the whole series, deliberately. A fit would be a BETTER constant-velocity
// model, and the claim under test is about the shortcut an implementation
// actually takes -- two points, a direction, extrapolate. Making the shortcut as
// good as it can be would understate the error, and the error is the assertion.
ConstantVelocityError constantVelocityError(const ProjectileSeries& series) {
  ConstantVelocityError result;
  if (series.samples.size() < 3) return result;
  const auto& first = series.samples.front();
  const auto& second = series.samples[1];
  const std::int32_t step = second.tick - first.tick;
  if (step <= 0) return result;
  for (int axis = 0; axis < 3; ++axis) {
    result.velocity[axis] = (second.origin[axis] - first.origin[axis]) / static_cast<float>(step);
  }
  result.ok = true;
  result.samples = series.samples.size();
  double sum = 0.0;
  for (const auto& sample : series.samples) {
    if (sample.tick == first.tick) continue;
    const float elapsed = static_cast<float>(sample.tick - first.tick);
    float predicted[3];
    for (int axis = 0; axis < 3; ++axis) {
      predicted[axis] = first.origin[axis] + result.velocity[axis] * elapsed;
    }
    const float error = distance(sample.origin, predicted);
    sum += error;
    if (error > result.maxError) {
      result.maxError = error;
      result.worstTick = sample.tick;
      for (int axis = 0; axis < 3; ++axis) {
        result.worstObserved[axis] = sample.origin[axis];
        result.worstPredicted[axis] = predicted[axis];
      }
    }
  }
  result.meanError = static_cast<float>(sum / static_cast<double>(result.samples));
  return result;
}

// SHORTCUT 2: the straight line through the first and last observed samples.
//
// The sibling shortcut, and the one that would survive a gate that only rejected
// "constant velocity" by name. An implementation that drew a polyline through the
// endpoints would pass such a gate while telling exactly the same lie.
TwoPointLineError twoPointLineError(const ProjectileSeries& series) {
  TwoPointLineError result;
  if (series.samples.size() < 3) return result;
  const auto& first = series.samples.front();
  const auto& last = series.samples.back();
  const std::int32_t span = last.tick - first.tick;
  if (span <= 0) return result;
  result.ok = true;
  result.samples = series.samples.size();
  for (const auto& sample : series.samples) {
    if (sample.tick == first.tick || sample.tick == last.tick) continue;
    const float fraction = static_cast<float>(sample.tick - first.tick) / static_cast<float>(span);
    float predicted[3];
    for (int axis = 0; axis < 3; ++axis) {
      predicted[axis] = first.origin[axis] + (last.origin[axis] - first.origin[axis]) * fraction;
    }
    const float error = distance(sample.origin, predicted);
    if (error > result.maxError) {
      result.maxError = error;
      result.worstTick = sample.tick;
      for (int axis = 0; axis < 3; ++axis) {
        result.worstObserved[axis] = sample.origin[axis];
        result.worstPredicted[axis] = predicted[axis];
      }
    }
  }
  return result;
}

} // namespace tf2::native
