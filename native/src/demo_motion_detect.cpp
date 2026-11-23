// A2 (2026-11-23): see demo_motion_detect.h for why each threshold has the value
// it has. This file is only the bookkeeping: three passes over the rows the census
// already kept, no new scanning and no state that outlives one call.
#include "demo_motion_detect.h"

#include <algorithm>
#include <cstdio>
#include <map>

namespace tf2::native {

namespace {

constexpr std::size_t kMaxHintsPerTick = 8;  // one tick cannot plausibly hold more; keeps a
                                             // pathological demo's hint list bounded

struct TeleportStep {
  std::int32_t tick = 0;
  double perTick = 0.0;
};

}  // namespace

const char* motionAnomalyKindName(MotionAnomalyKind kind) {
  switch (kind) {
  case MotionAnomalyKind::Stall: return "stall";
  case MotionAnomalyKind::Teleport: return "teleport";
  case MotionAnomalyKind::SharpTurn: return "sharpTurn";
  }
  return "unknown";
}

MotionDetectReport detectMotionAnomalies(const std::vector<DemoTickAuditPacketRow>& packets,
    const DemoMotionCensus& census, const MotionDetectThresholds& thresholds) {
  MotionDetectReport report;
  report.thresholds = thresholds;

  // ---- stall: the gap between two data packets, not the distance moved in it.
  // A run of consecutive above-floor gaps is ONE stall: a recording that loses six
  // packets in a row is one stop of ~24 ticks, not six entries.
  std::int32_t prevTick = -1;
  bool open = false;
  std::int32_t openStart = 0;
  std::int32_t openEnd = 0;
  const auto closeStall = [&]() {
    if (!open) return;
    open = false;
    const std::int32_t duration = openEnd - openStart;
    if (duration < thresholds.stallMinTicks) return;
    MotionAnomaly anomaly;
    anomaly.kind = MotionAnomalyKind::Stall;
    anomaly.startTick = openStart;
    anomaly.endTick = openEnd;
    anomaly.durationTicks = duration;
    anomaly.entity = -1;
    anomaly.metric = static_cast<double>(duration);
    report.anomalies.push_back(anomaly);
    ++report.stalls;
  };
  for (const DemoTickAuditPacketRow& row : packets) {
    if (row.command != 2) continue;  // signon packets carry no playback tick of their own
    ++report.dataPackets;
    if (prevTick >= 0) {
      const std::int32_t gap = row.serverTick - prevTick;
      if (gap > thresholds.stallGapTicks) {
        if (!open) {
          open = true;
          openStart = prevTick;
        }
        openEnd = row.serverTick;
      } else if (open) {
        closeStall();
      }
    }
    prevTick = row.serverTick;
  }
  closeStall();

  // ---- teleport: per-tick distance, after demoting scene-level resets.
  std::map<std::int32_t, std::size_t> jumpersPerTick;
  for (const DemoMotionRate& rate : census.moveRates) {
    if (static_cast<double>(rate.perTick) <= thresholds.teleportUnitsPerTick) continue;
    ++jumpersPerTick[rate.tick];
  }
  for (const auto& entry : jumpersPerTick) {
    if (entry.second > thresholds.teleportSceneResetEntities) ++report.sceneResets;
  }

  std::vector<MotionAnomaly> teleports;
  std::map<std::int32_t, std::vector<TeleportStep>> perEntity;
  for (const DemoMotionRate& rate : census.moveRates) {
    if (static_cast<double>(rate.perTick) <= thresholds.teleportUnitsPerTick) continue;
    ++report.teleportCandidates;
    const auto jumpers = jumpersPerTick.find(rate.tick);
    if (jumpers != jumpersPerTick.end() && jumpers->second > thresholds.teleportSceneResetEntities) continue;
    TeleportStep step;
    step.tick = rate.tick;
    step.perTick = static_cast<double>(rate.perTick);
    perEntity[rate.entity].push_back(step);
  }

  // The census keeps every counted step of at most rateMaxDt ticks; a run is
  // consecutive steps no further apart than that. The accumulated metric is the sum
  // of the per-tick rates, i.e. the distance the entity covered while every one of
  // its steps was above the floor -- conservative, because a step of dt > 1 is one
  // entry carrying an average, not dt entries.
  const std::int32_t maxStepGap = std::max<std::int32_t>(1, census.rateMaxDt);
  for (auto& entitySteps : perEntity) {
    std::vector<TeleportStep>& steps = entitySteps.second;
    std::sort(steps.begin(), steps.end(), [](const TeleportStep& a, const TeleportStep& b) {
      return a.tick != b.tick ? a.tick < b.tick : a.perTick > b.perTick;
    });
    std::size_t at = 0;
    while (at < steps.size()) {
      const std::int32_t startTick = steps[at].tick;
      std::int32_t lastTick = steps[at].tick;
      double sum = steps[at].perTick;
      ++at;
      while (at < steps.size() && steps[at].tick - lastTick <= maxStepGap) {
        sum += steps[at].perTick;
        lastTick = steps[at].tick;
        ++at;
      }
      if (sum < thresholds.teleportMinSumUnits) continue;
      MotionAnomaly anomaly;
      anomaly.kind = MotionAnomalyKind::Teleport;
      anomaly.startTick = startTick;
      anomaly.endTick = lastTick;
      anomaly.durationTicks = lastTick - startTick;
      anomaly.entity = entitySteps.first;
      anomaly.metric = sum;
      teleports.push_back(anomaly);
    }
  }

  // Keep the largest jumps, then hand them to the timeline in tick order: the list is
  // what a person reviews, the counts are what the analysis screen reports.
  report.teleports = teleports.size();
  if (teleports.size() > thresholds.teleportMaxReported) {
    std::sort(teleports.begin(), teleports.end(),
        [](const MotionAnomaly& a, const MotionAnomaly& b) { return a.metric > b.metric; });
    report.teleportsOmitted = teleports.size() - thresholds.teleportMaxReported;
    teleports.resize(thresholds.teleportMaxReported);
  }
  for (const MotionAnomaly& anomaly : teleports) report.anomalies.push_back(anomaly);

  // ---- sharp turn: a hint on the curve, one entry per tick.
  std::map<std::int32_t, std::size_t> hintsThisTick;
  for (const DemoMotionRate& rate : census.turnRates) {
    if (static_cast<double>(rate.perTick) < thresholds.sharpTurnDegPerTick) continue;
    std::size_t& used = hintsThisTick[rate.tick];
    if (used >= kMaxHintsPerTick) continue;
    ++used;
    MotionAnomaly hint;
    hint.kind = MotionAnomalyKind::SharpTurn;
    hint.startTick = rate.tick;
    hint.endTick = rate.tick;
    hint.durationTicks = 0;
    hint.entity = rate.entity;
    hint.metric = static_cast<double>(rate.perTick);
    report.hints.push_back(hint);
    ++report.sharpTurns;
  }

  std::sort(report.anomalies.begin(), report.anomalies.end(),
      [](const MotionAnomaly& a, const MotionAnomaly& b) {
        if (a.startTick != b.startTick) return a.startTick < b.startTick;
        return static_cast<int>(a.kind) < static_cast<int>(b.kind);
      });
  std::sort(report.hints.begin(), report.hints.end(),
      [](const MotionAnomaly& a, const MotionAnomaly& b) {
        if (a.startTick != b.startTick) return a.startTick < b.startTick;
        return a.entity < b.entity;
      });
  return report;
}

std::vector<std::string> formatMotionDetectLines(const MotionDetectReport& report) {
  std::vector<std::string> lines;
  char buffer[512];
  const auto num = [](double value) {
    char text[64];
    std::snprintf(text, sizeof(text), "%.2f", value);
    return std::string(text);
  };
  std::snprintf(buffer, sizeof(buffer),
      "motion-detect thresholds stallGap=%d stallMin=%d teleportPerTick=%s teleportSum=%s sceneReset=%zu sharpTurn=%s",
      report.thresholds.stallGapTicks, report.thresholds.stallMinTicks,
      num(report.thresholds.teleportUnitsPerTick).c_str(),
      num(report.thresholds.teleportMinSumUnits).c_str(),
      report.thresholds.teleportSceneResetEntities,
      num(report.thresholds.sharpTurnDegPerTick).c_str());
  lines.emplace_back(buffer);
  std::snprintf(buffer, sizeof(buffer),
      "motion-detect dataPackets=%zu stalls=%zu teleports=%zu sharpTurns=%zu sceneResets=%zu candidates=%zu omitted=%zu",
      report.dataPackets, report.stalls, report.teleports, report.sharpTurns,
      report.sceneResets, report.teleportCandidates, report.teleportsOmitted);
  lines.emplace_back(buffer);
  for (const MotionAnomaly& anomaly : report.anomalies) {
    std::snprintf(buffer, sizeof(buffer),
        "motion-anomaly class=%s start=%d end=%d duration=%d entity=%d metric=%s",
        motionAnomalyKindName(anomaly.kind), anomaly.startTick, anomaly.endTick,
        anomaly.durationTicks, anomaly.entity, num(anomaly.metric).c_str());
    lines.emplace_back(buffer);
  }
  for (const MotionAnomaly& hint : report.hints) {
    std::snprintf(buffer, sizeof(buffer),
        "motion-hint class=%s tick=%d entity=%d metric=%s",
        motionAnomalyKindName(hint.kind), hint.startTick, hint.entity, num(hint.metric).c_str());
    lines.emplace_back(buffer);
  }
  return lines;
}

MotionViewHistory buildMotionViewHistory(const DemoMotionCensus& census,
    std::int32_t entity, std::int32_t firstTick, std::int32_t lastTick,
    const MotionDetectThresholds& thresholds) {
  MotionViewHistory history;
  history.entity = entity;
  history.firstTick = firstTick;
  history.lastTick = lastTick;
  if (entity < 0 || lastTick < firstTick) return history;
  // A span walk is linear, but the walk below is per tick because the presence bar needs
  // a state for every tick of the axis. A recording that spans more than this is not a
  // match the timeline draws anyway (the strip hides under kMinTickSpan only at the other
  // end), so the guard keeps a corrupt header from turning into a 4-billion-iteration
  // loop.
  const std::int64_t span = static_cast<std::int64_t>(lastTick) - static_cast<std::int64_t>(firstTick);
  if (span > 8000000) return history;
  history.valid = true;

  const auto found = census.playerSpans.find(entity);
  const std::vector<DemoTickSpan> empty;
  const std::vector<DemoTickSpan>& spans = found == census.playerSpans.end() ? empty : found->second;
  history.writeSpans = spans.size();
  if (!spans.empty()) history.lastWriteTick = spans.back().last;
  // The bar must not stripe: a player standing still for one tick writes nothing, and TF2
  // omits unchanged props, so raw spans fragment into thousands of slivers. Writes closer
  // together than this are one stretch of presence; a longer hole means the recording lost
  // the player (out of view, or dead).
  constexpr std::int32_t kPresenceWindowGap = 66;
  std::vector<DemoTickSpan> windows;
  for (const DemoTickSpan& write : spans) {
    if (!windows.empty() && write.first - windows.back().last <= kPresenceWindowGap) {
      if (write.last > windows.back().last) windows.back().last = write.last;
      continue;
    }
    windows.push_back(write);
  }

  // The entity's life edges, in tick order. The census keeps them interleaved across
  // entities, so this gathers the ones that belong to the followed player; a match has
  // a few hundred of them at most.
  std::vector<const DemoMotionLifeEdge*> edges;
  for (const DemoMotionLifeEdge& edge : census.lifeEdges) {
    if (edge.entity == entity) edges.push_back(&edge);
  }

  std::size_t spanIndex = 0;
  std::size_t edgeIndex = 0;
  bool dead = false;
  // Which span the last data-bearing tick belonged to: a new span means a new instance.
  std::size_t lastDataSpan = static_cast<std::size_t>(-1);
  MotionViewPresenceState state = MotionViewPresenceState::NoData;
  const auto push = [&history](MotionViewPresenceState runState, std::int32_t start,
                               std::int32_t end) {
    if (runState == MotionViewPresenceState::NoData) {
      history.noDataTicks += static_cast<std::size_t>(end - start + 1);
      return;  // a no-data run is a hole: nothing is drawn, so nothing is kept
    }
    if (runState == MotionViewPresenceState::Alive) {
      history.aliveTicks += static_cast<std::size_t>(end - start + 1);
    } else {
      history.deadTicks += static_cast<std::size_t>(end - start + 1);
    }
    if (!history.presence.empty() && history.presence.back().state == runState
        && history.presence.back().endTick + 1 == start) {
      history.presence.back().endTick = end;
      return;
    }
    MotionViewPresence run;
    run.startTick = start;
    run.endTick = end;
    run.state = runState;
    history.presence.push_back(run);
  };

  std::int32_t runStart = firstTick;
  for (std::int32_t tick = firstTick; tick <= lastTick; ++tick) {
    while (spanIndex < windows.size() && windows[spanIndex].last < tick) ++spanIndex;
    const bool haveData = spanIndex < windows.size() && windows[spanIndex].first <= tick;
    if (haveData && spanIndex != lastDataSpan) {
      // A fresh write burst after a hole is a new entity instance. A life edge only ends the
      // instance it happened in, and TF2 sends the respawn as a create packet whose baseline
      // never reaches this hook, so without this reset one death would paint the rest of the
      // match grey.
      lastDataSpan = spanIndex;
      dead = false;
    }
    while (edgeIndex < edges.size() && edges[edgeIndex]->tick <= tick) {
      dead = edges[edgeIndex]->life != 0;
      ++edgeIndex;
    }
    MotionViewPresenceState next = MotionViewPresenceState::NoData;
    if (haveData) {
      next = dead ? MotionViewPresenceState::Dead : MotionViewPresenceState::Alive;
    } else if (dead) {
      // No writes after an observed death: the instance is gone and no respawn has shown up
      // yet, so the hole is dead time rather than missing data. The next write burst starts
      // a new instance and clears this.
      next = MotionViewPresenceState::Dead;
    }
    if (next != state) {
      if (tick > runStart) push(state, runStart, tick - 1);
      state = next;
      runStart = tick;
    }
  }
  push(state, runStart, lastTick);

  // The turns: the census kept every counted turn step (dt <= rateMaxDt, not within the
  // respawn guard), each stamped with the tick it ended on. A tick with no entry had no
  // recorded turn, which draws as the curve's baseline rather than as missing data --
  // the presence bar above it is what says whether the recording was looking.
  history.turns.reserve(census.turnRates.size() / 8);
  for (const DemoMotionRate& rate : census.turnRates) {
    if (rate.entity != entity) continue;
    if (rate.tick < firstTick || rate.tick > lastTick) continue;
    MotionViewTurn turn;
    turn.tick = rate.tick;
    turn.degrees = static_cast<double>(rate.perTick);
    turn.overThreshold = turn.degrees > thresholds.sharpTurnDegPerTick;
    if (turn.overThreshold) ++history.turnsOverThreshold;
    if (turn.degrees > history.maxDegrees) history.maxDegrees = turn.degrees;
    history.turns.push_back(turn);
  }
  return history;
}

}  // namespace tf2::native