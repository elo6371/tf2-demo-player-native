#pragma once

// I6 (2026-11-23, docs/UI-SHELL-DESIGN-2026-11-22.md section 9): the per-round
// timeline the review workspace draws.
//
// Section 9 asks for two readings on one axis: a per-round timeline whose tick
// marks are that round's kills, and a red track of the three anomaly classes the
// A2 detector finds. All three inputs already exist, but they are stamped in
// different units:
//   * game events (player_death, teamplay_round_start/win) carry the server tick
//     of the packet that delivered them;
//   * the A2 anomalies come from the entity stream and are also on the server
//     tick (DemoTickAuditCapture::observeMotionProperty takes serverTick);
//   * the player seeks by PLAYBACK tick, a different counter:
//     server = playback + (firstDataNetTick - firstDataEntryTick).
// This model puts the three on the server-tick axis and publishes that offset so
// a caller can jump to a mark without re-deriving the arithmetic. It decides
// nothing about layout or colour: drawing is the shell's job.

#include "demo_header.h"
#include "demo_motion_detect.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace tf2::native {

// One kill. weapon is the death event's weapon_logclassname, which is the name
// the game itself logs (a rocket reads "tf_projectile_rocket", a world kill reads
// "world"), never an inference from the damage bits.
struct MatchTimelineKill {
  std::int32_t serverTick = 0;
  std::int32_t playbackTick = 0;  // serverTick - serverToEntryOffset (may be negative)
  std::int32_t roundIndex = -1;   // -1 = the kill happened outside any round
  std::int32_t victimEntity = -1;
  std::int32_t victimUserid = -1;
  std::int32_t attackerUserid = -1;
  bool attackerKnown = false;     // false for a world/self kill
  // The killer's team as the demo's own m_iTeamNum read it at the kill tick: 2 = RED,
  // 3 = BLU, -1 = not resolved (the recording does not carry that player's entity, or
  // the retention answered with no snapshot). The timeline draws RED kills above the
  // midline and BLU kills below, so an unknown team must stay visibly unknown rather
  // than defaulting to one side.
  std::int32_t attackerTeam = -1;
  std::string victimName;
  std::string attackerName;
  std::string weapon;
};

// One round. A round's drawn segment runs from its start to the NEXT round's
// start (endServerTick), not to the win panel tick: kills keep happening while
// the panel is up and they belong to the round that just ended. winServerTick is
// kept beside it (0 = the round never reported a win) so the screen can mark both.
struct MatchTimelineRound {
  std::int32_t index = 0;
  std::int32_t startServerTick = 0;
  std::int32_t winServerTick = 0;
  std::int32_t endServerTick = 0;
  bool closed = false;            // a following round started
  std::size_t kills = 0;
  std::size_t stalls = 0;
  std::size_t teleports = 0;
  std::size_t sharpTurns = 0;
};

struct MatchTimelineReport {
  bool valid = false;             // false: the demo carried no game events at all
  std::int32_t serverToEntryOffset = 0;
  std::int32_t firstServerTick = 0;
  std::int32_t lastServerTick = 0;
  std::size_t roundStarts = 0;
  std::size_t roundWins = 0;
  std::size_t killCount = 0;
  std::size_t killsOutsideRounds = 0;
  // anomalies are the detector's real findings (stalls + teleports, already capped by
  // A2's teleportMaxReported); hints are sharp turns, which A2 deliberately reports as
  // hints rather than anomalies. They are counted apart so the two numbers always match
  // their own trace lines instead of silently summing into one ambiguous total.
  std::size_t anomalyCount = 0;
  std::size_t anomaliesOutsideRounds = 0;
  std::size_t hintCount = 0;
  std::size_t hintsOutsideRounds = 0;
  // Team resolution is a per-kill entity snapshot query, which is not free on a long
  // recording; these two record how much of the list is actually coloured.
  std::size_t killsWithTeam = 0;
  std::size_t teamResolveQueries = 0;
  std::vector<MatchTimelineRound> rounds;
  std::vector<MatchTimelineKill> kills;
};

// motion may be null: the timeline is still valid, it simply carries no anomaly
// counts, which is what a run without the census produces.
MatchTimelineReport buildMatchTimeline(const DemoNetworkSummary& summary,
                                       const MotionDetectReport* motion);

// Line formats (all fields space-separated, names percent-encoded so one line
// stays one record):
//   match-timeline valid=<0|1> offset=<n> first=<tick> last=<tick> rounds=<n>
//     starts=<n> wins=<n> kills=<n> outsideKills=<n> anomalies=<n>
//     outsideAnomalies=<n> killLines=<n> killLinesOmitted=<n>
//   match-round index=<n> start=<tick> win=<tick> end=<tick> closed=<0|1>
//     kills=<n> stalls=<n> teleports=<n> sharpTurns=<n>
//   match-kill tick=<serverTick> playback=<tick> round=<index| -1> victim=<entity>
//     victimUserid=<id> victimName=<token> attacker=<userid|-1>
//     attackerName=<token> weapon=<token>
// includeKills=false prints only the header and the rounds; the product uses it
// so a demo with a thousand kills does not put a thousand lines in its trace.
std::vector<std::string> formatMatchTimelineLines(const MatchTimelineReport& report,
                                                  bool includeKills = true);

} // namespace tf2::native
