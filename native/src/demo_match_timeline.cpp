#include "demo_match_timeline.h"

#include <algorithm>
#include <cstdlib>
#include <limits>
#include <map>
#include <sstream>
#include <utility>

namespace tf2::native {
namespace {

// Same cap and the same reason as the A2 detector: a long recording must not turn
// a reading into megabytes of trace. The header reports how many were left out.
constexpr std::size_t kMaxKillLines = 512;

std::string eventFieldText(const DemoGameEvent& event, const char* name) {
  for (const auto& field : event.fields) if (field.first == name) return field.second;
  return std::string();
}

// Returns -1 for a missing or non-numeric field. The unsigned 32-bit spelling of
// Source's sentinels (4294967295) exceeds the int range and therefore also lands
// on -1, which is the value the caller already treats as "unknown".
std::int32_t eventFieldInt(const DemoGameEvent& event, const char* name) {
  const std::string text = eventFieldText(event, name);
  if (text.empty()) return -1;
  char* end = nullptr;
  const long long value = std::strtoll(text.c_str(), &end, 10);
  if (end == text.c_str() || end == nullptr || *end != '\0' || value < 0 || value > 0x7FFFFFFF) return -1;
  return static_cast<std::int32_t>(value);
}

// 65535 is Source's "no client": the attacker of a world kill arrives with it.
bool userIdMissing(std::int32_t userId) { return userId < 0 || userId == 65535; }

// The workspace file encodes names the same way (workspace_types.h); a trace line
// has to stay one record even when a player name contains a space or a semicolon.
std::string traceToken(const std::string& text) {
  static const char* hex = "0123456789ABCDEF";
  std::string out;
  out.reserve(text.size());
  for (const char raw : text) {
    const unsigned char byte = static_cast<unsigned char>(raw);
    const bool plain = (byte >= 'A' && byte <= 'Z') || (byte >= 'a' && byte <= 'z')
        || (byte >= '0' && byte <= '9') || byte == '.' || byte == '_' || byte == '~' || byte == '-';
    if (plain) {
      out.push_back(static_cast<char>(byte));
    } else {
      out.push_back('%');
      out.push_back(hex[(byte >> 4) & 0x0F]);
      out.push_back(hex[byte & 0x0F]);
    }
  }
  return out;
}

// Reads one int property by its exact or dotted name, the same rule entity_model.cpp
// and map_overview.cpp use for m_iTeamNum.
bool readEntityInt(const EntityState& state, const char* key, std::int32_t& out) {
  const std::string token = key;
  const EntityPropertyValue* exact = nullptr;
  const EntityPropertyValue* dotted = nullptr;
  for (const auto& entry : state.properties) {
    if (entry.first == token) {
      exact = &entry.second;
    } else if (entry.first.size() > token.size()
        && entry.first[entry.first.size() - token.size() - 1] == '.'
        && entry.first.compare(entry.first.size() - token.size(), token.size(), token) == 0) {
      dotted = &entry.second;
    }
  }
  const EntityPropertyValue* value = exact ? exact : dotted;
  if (!value || value->type != SendPropType::Int) return false;
  if (value->intValue < std::numeric_limits<std::int32_t>::min()
      || value->intValue > std::numeric_limits<std::int32_t>::max()) {
    return false;
  }
  out = static_cast<std::int32_t>(value->intValue);
  return true;
}

// The OWNING round is the last one that started at or before the tick. A tick
// before the first start has no round (the recording joined a match already in
// progress), and that is reported as "outside" rather than folded into round 0.
std::size_t roundIndexForTick(const std::vector<MatchTimelineRound>& rounds, std::int32_t tick) {
  std::size_t found = rounds.size();
  for (std::size_t index = 0; index < rounds.size(); ++index) {
    if (rounds[index].startServerTick > tick) break;
    found = index;
  }
  return found;
}

} // namespace

MatchTimelineReport buildMatchTimeline(const DemoNetworkSummary& summary,
                                       const MotionDetectReport* motion) {
  MatchTimelineReport report;
  if (summary.gameEvents.empty()) return report;
  report.valid = true;
  if (summary.firstDataNetTick >= 0 && summary.firstDataEntryTick >= 0) {
    report.serverToEntryOffset = summary.firstDataNetTick - summary.firstDataEntryTick;
  }
  // userid -> name. The server re-sends a userinfo entry when a player renames, so
  // the last entry for a userid wins: that is the name the scoreboard shows.
  std::map<std::int32_t, std::string> nameByUserid;
  // userid -> entity slot, for the team lookup below. player_death names the attacker
  // by userid only, and the entity table is indexed by slot, so the userinfo table is
  // the bridge (its entityIndex is the slot userinfo was decoded for).
  std::map<std::int32_t, std::int32_t> entityByUserid;
  for (const DemoUserInfoEntry& info : summary.userInfos) {
    const std::int32_t userId = static_cast<std::int32_t>(info.userId);
    entityByUserid[userId] = static_cast<std::int32_t>(info.entityIndex);
    if (info.name.empty()) continue;
    nameByUserid[userId] = info.name;
  }
  report.firstServerTick = summary.gameEvents.front().tick;
  report.lastServerTick = summary.gameEvents.front().tick;
  for (const DemoGameEvent& event : summary.gameEvents) {
    report.firstServerTick = std::min(report.firstServerTick, event.tick);
    report.lastServerTick = std::max(report.lastServerTick, event.tick);
  }
  for (const DemoGameEvent& event : summary.gameEvents) {
    if (event.name == "teamplay_round_start") {
      MatchTimelineRound round;
      round.index = static_cast<std::int32_t>(report.rounds.size());
      round.startServerTick = event.tick;
      report.rounds.push_back(round);
      ++report.roundStarts;
    } else if (event.name == "teamplay_round_win") {
      ++report.roundWins;
      const std::size_t owner = roundIndexForTick(report.rounds, event.tick);
      // The first win of a round wins: a demo can replay the panel event.
      if (owner < report.rounds.size() && report.rounds[owner].winServerTick == 0) {
        report.rounds[owner].winServerTick = event.tick;
        // The winner's team, straight off the event. "team" is what teamplay_round_win
        // carries; the fallback name is only a guard against a decoder that spells it
        // differently, and an unreadable team leaves the stripe grey (never a side).
        std::int32_t team = eventFieldInt(event, "team");
        if (team != 2 && team != 3) team = eventFieldInt(event, "winteam");
        if (team == 2 || team == 3) {
          report.rounds[owner].winTeam = team;
          ++report.roundsWithWinTeam;
        }
      }
    }
  }
  for (std::size_t index = 0; index < report.rounds.size(); ++index) {
    const bool following = index + 1 < report.rounds.size();
    report.rounds[index].endServerTick =
        following ? report.rounds[index + 1].startServerTick : report.lastServerTick;
    report.rounds[index].closed = following;
  }
  report.kills.reserve(summary.gameEvents.size() / 16u + 1u);
  for (const DemoGameEvent& event : summary.gameEvents) {
    if (event.name != "player_death") continue;
    MatchTimelineKill kill;
    kill.serverTick = event.tick;
    kill.playbackTick = event.tick - report.serverToEntryOffset;
    kill.victimUserid = eventFieldInt(event, "userid");
    kill.victimEntity = eventFieldInt(event, "victim_entindex");
    kill.attackerUserid = eventFieldInt(event, "attacker");
    kill.attackerKnown = !userIdMissing(kill.attackerUserid);
    kill.weapon = eventFieldText(event, "weapon_logclassname");
    if (kill.weapon.empty()) kill.weapon = eventFieldText(event, "weapon");
    const auto victimName = nameByUserid.find(kill.victimUserid);
    if (victimName != nameByUserid.end()) kill.victimName = victimName->second;
    const auto attackerName = nameByUserid.find(kill.attackerUserid);
    if (attackerName != nameByUserid.end()) kill.attackerName = attackerName->second;
    const std::size_t owner = roundIndexForTick(report.rounds, event.tick);
    if (owner < report.rounds.size()) {
      kill.roundIndex = report.rounds[owner].index;
      ++report.rounds[owner].kills;
    } else {
      ++report.killsOutsideRounds;
    }
    ++report.killCount;
    report.kills.push_back(std::move(kill));
  }
  // Team colouring: the timeline draws RED kills above the midline and BLU below, and
  // player_death carries no team, so it is read from the demo's own entity table at the
  // kill's server tick. The query is per distinct tick (a kill wave lands on one tick)
  // and at-or-before, so a stale checkpoint still answers the team: a player does not
  // change team between two kills 100 ticks apart. A recording that does not carry the
  // killer's entity leaves attackerTeam at -1 rather than guessing a side.
  constexpr std::size_t kMaxTeamQueries = 2048;
  std::vector<EntityState> teamStates;
  std::int32_t teamQueryTick = std::numeric_limits<std::int32_t>::min();
  bool teamStatesValid = false;
  for (MatchTimelineKill& kill : report.kills) {
    if (!kill.attackerKnown) continue;
    const auto slot = entityByUserid.find(kill.attackerUserid);
    if (slot == entityByUserid.end() || slot->second < 0) continue;
    if (teamQueryTick != kill.serverTick) {
      teamQueryTick = kill.serverTick;
      teamStatesValid = false;
      if (report.teamResolveQueries >= kMaxTeamQueries) continue;
      teamStates.clear();
      std::int32_t resolvedTick = -1;
      const EntitySnapshotQueryStatus status =
          queryEntitySnapshotAtOrBeforeTick(summary, kill.serverTick, teamStates, &resolvedTick);
      ++report.teamResolveQueries;
      teamStatesValid = status == EntitySnapshotQueryStatus::Available
          || status == EntitySnapshotQueryStatus::Checkpoint;
      if (!teamStatesValid) teamStates.clear();
    }
    if (!teamStatesValid) continue;
    const std::size_t slotIndex = static_cast<std::size_t>(slot->second);
    if (slotIndex >= teamStates.size()) continue;
    std::int32_t team = -1;
    if (readEntityInt(teamStates[slotIndex], "m_iTeamNum", team) && team > 0) {
      kill.attackerTeam = team;
      ++report.killsWithTeam;
    }
  }
  if (motion != nullptr) {
    report.anomalyCount = motion->anomalies.size();
    report.hintCount = motion->hints.size();
    for (const MotionAnomaly& anomaly : motion->anomalies) {
      const std::size_t owner = roundIndexForTick(report.rounds, anomaly.startTick);
      if (owner >= report.rounds.size()) {
        ++report.anomaliesOutsideRounds;
      } else if (anomaly.kind == MotionAnomalyKind::Stall) {
        ++report.rounds[owner].stalls;
      } else if (anomaly.kind == MotionAnomalyKind::Teleport) {
        ++report.rounds[owner].teleports;
      }
    }
    // A sharp turn is a HINT (A2 shipped it as one) and each hint carries its own
    // tick, which the aggregate sharpTurns counter does not.
    for (const MotionAnomaly& hint : motion->hints) {
      const std::size_t owner = roundIndexForTick(report.rounds, hint.startTick);
      if (owner >= report.rounds.size()) {
        ++report.hintsOutsideRounds;
      } else {
        ++report.rounds[owner].sharpTurns;
      }
    }
  }
  return report;
}

std::vector<std::string> formatMatchTimelineLines(const MatchTimelineReport& report,
                                                  bool includeKills) {
  std::vector<std::string> lines;
  if (!report.valid) {
    lines.push_back("match-timeline valid=0 reason=no-game-events");
    return lines;
  }
  const std::size_t printed = includeKills ? std::min(report.kills.size(), kMaxKillLines) : 0u;
  {
    std::ostringstream out;
    out << "match-timeline valid=1 offset=" << report.serverToEntryOffset
        << " first=" << report.firstServerTick << " last=" << report.lastServerTick
        << " rounds=" << report.rounds.size() << " starts=" << report.roundStarts
        << " wins=" << report.roundWins << " winsWithTeam=" << report.roundsWithWinTeam
        << " kills=" << report.killCount
        << " outsideKills=" << report.killsOutsideRounds
        << " anomalies=" << report.anomalyCount
        << " outsideAnomalies=" << report.anomaliesOutsideRounds
        << " hints=" << report.hintCount
        << " outsideHints=" << report.hintsOutsideRounds
        << " killLines=" << printed
        << " killLinesOmitted=" << (report.kills.size() - printed)
        << " killsWithTeam=" << report.killsWithTeam
        << " teamQueries=" << report.teamResolveQueries;
    lines.push_back(out.str());
  }
  for (const MatchTimelineRound& round : report.rounds) {
    std::ostringstream out;
    out << "match-round index=" << round.index << " start=" << round.startServerTick
        << " win=" << round.winServerTick << " end=" << round.endServerTick
        << " winTeam=" << round.winTeam << " closed=" << (round.closed ? 1 : 0)
        << " kills=" << round.kills
        << " stalls=" << round.stalls << " teleports=" << round.teleports
        << " sharpTurns=" << round.sharpTurns;
    lines.push_back(out.str());
  }
  for (std::size_t index = 0; index < printed; ++index) {
    const MatchTimelineKill& kill = report.kills[index];
    std::ostringstream out;
    out << "match-kill tick=" << kill.serverTick << " playback=" << kill.playbackTick
        << " round=" << kill.roundIndex << " victim=" << kill.victimEntity
        << " victimUserid=" << kill.victimUserid
        << " victimName=" << traceToken(kill.victimName)
        << " attacker=" << (kill.attackerKnown ? kill.attackerUserid : -1)
        << " attackerTeam=" << kill.attackerTeam
        << " attackerName=" << traceToken(kill.attackerName)
        << " weapon=" << traceToken(kill.weapon);
    lines.push_back(out.str());
  }
  return lines;
}

} // namespace tf2::native
