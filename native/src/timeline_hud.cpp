#include "timeline_hud.h"

#include <algorithm>
#include <cmath>
#include <sstream>

namespace tf2::native {
namespace {

// Section 9 puts the strip at the bottom of the workspace; these are the strip's own
// proportions, not the design's type scale. The strip is 76 px tall, top to bottom: 16
// for the round labels, 4 for A4's presence bar, 6 for the exception track, 12 for A4's
// turn-rate curve, 5 of air, 7 for the RED kill ticks, 8 for the track (the winner stripe
// covers its top 3), 7 for the BLU ticks, 6 for A3b's per-tick row under a 3 px gap, and
// 2 px of slack at the bottom edge. Both of the last two layers grew the strip instead of
// sharing rows: the row could not use the old bottom slack because the BLU kills already
// draw there, and the curve cannot use the 12 px the exception lane left free because the
// RED ticks draw through those rows -- a bar over the threshold and a RED kill would be the
// same red in the same band.
constexpr float kMargin = 12.0f;
constexpr float kStripHeight = 76.0f;
constexpr float kAnomalyLaneTop = 20.0f;
constexpr float kAnomalyLaneHeight = 6.0f;
constexpr float kAnomalyHintHeight = 2.0f;
constexpr float kAnomalyBlockMinWidth = 2.0f;
constexpr float kTrackTop = 50.0f;
constexpr float kTrackHeight = 8.0f;
constexpr float kCellsTop = 68.0f;
constexpr float kCellsHeight = 6.0f;
constexpr float kCellGap = 1.0f;
// A focus window wider than this is drawn as its first ticks rather than as sub-pixel
// cells: the point of the row is reading a tick, and a cell narrower than a pixel cannot
// show one. 400 ticks is six seconds at 66.67 tick/s, far wider than any anomaly.
constexpr std::int32_t kMaxCells = 400;
constexpr std::size_t kMaxCellLines = 64;
// A4 traces: presence runs are few (one per write span, a couple per player), turns can be
// one per written tick, so the turn rows are capped as well and the totals are on the
// main line.
constexpr std::size_t kMaxPresenceLines = 64;
constexpr std::size_t kMaxTurnLines = 200;
// A4 (section 9.2 items 1 and 3): the follow-only layers. The presence bar takes the 4 px
// between the round labels and the exception lane, which A3 left empty. The curve takes a
// 12 px band of its own; that band is part of the strip's growth, not of the gap A3 left,
// because the 5 rows free above the RED ticks were not a band anyone could read a spike in.
constexpr float kPresenceTop = 16.0f;
constexpr float kPresenceHeight = 4.0f;
constexpr float kCurveTop = 26.0f;
constexpr float kCurveHeight = 12.0f;
// The curve's scale, not the threshold: the band's top is 220 degrees per tick (~1.6x the
// 135-degree sharp-turn threshold), which puts the threshold line at 61% and lets a flick
// cross it while a 20-degree drift stays a couple of pixels tall.
constexpr float kCurveTopDegrees = 220.0f;
constexpr float kCurveThresholdDegrees = 135.0f;
// A turn is one tick wide on the axis; drawing it 1 px keeps a flick a spike and not a
// smear, and two flicks a few ticks apart stay two spikes.
constexpr float kCurveBarWidth = 1.0f;
constexpr float kStripeHeight = 3.0f;
constexpr float kKillTickHeight = 7.0f;
constexpr float kPlayheadOvershoot = 5.0f;
constexpr float kLabelTop = 2.0f;
constexpr float kLabelPixelSize = 11.0f;
constexpr float kMinBandLabelWidth = 26.0f;
constexpr float kMinStripWidth = 240.0f;
constexpr float kAvoidGap = 8.0f;
// A recording that spends its whole length on one tick has no axis to map onto; the
// strip is hidden instead of dividing by zero.
constexpr std::int32_t kMinTickSpan = 16;
constexpr std::size_t kMaxBandLines = 64;
constexpr std::size_t kMaxAnomalyLines = 64;

constexpr float kBandColour[4] = {0.09f, 0.10f, 0.13f, 0.76f};
constexpr float kBandAltColour[4] = {0.14f, 0.15f, 0.19f, 0.76f};
constexpr float kTrackBaseColour[4] = {0.05f, 0.06f, 0.08f, 0.82f};
constexpr float kWinGreyColour[4] = {0.55f, 0.56f, 0.58f, 1.0f};
constexpr float kUnknownTeamTickColour[4] = {0.72f, 0.72f, 0.70f, 0.85f};
constexpr float kPlayheadColour[4] = {0.98f, 0.98f, 0.96f, 1.0f};
constexpr float kLabelColour[4] = {0.86f, 0.86f, 0.84f, 0.95f};
// The exception track is red like a warning, not red like RED: a team colour here would
// read as "this team did it" and the anomaly is about the recording, not a side.
constexpr float kAnomalyLaneColour[4] = {0.05f, 0.06f, 0.08f, 0.55f};
constexpr float kAnomalyBlockColour[4] = {0.94f, 0.26f, 0.22f, 1.0f};
constexpr float kAnomalyHintColour[4] = {0.72f, 0.58f, 0.34f, 0.90f};
// A3b: an ordinary tick of the per-tick row. Lighter than the lane's base so the row
// reads as a ruler under the strip, not as a second lane.
constexpr float kCellColour[4] = {0.16f, 0.17f, 0.21f, 0.92f};
// A4: green is "the recording says he was alive", grey "it says he was dead". Neither is
// a team colour: this bar is about the data, not about a side, the same rule the
// exception lane keeps.
constexpr float kPresenceAliveColour[4] = {0.30f, 0.72f, 0.40f, 0.95f};
constexpr float kPresenceDeadColour[4] = {0.44f, 0.45f, 0.49f, 0.95f};
// Opaque, unlike the band and lane bases: the panel under the strip is translucent, so a
// translucent mark takes its colour from whatever game pixel sits behind it. Measured on
// the first version, the 0.55-alpha threshold line matched a single colour on 23 of its
// 1,240 columns -- a reference line that changes colour along its own length reads as
// noise and cannot be gated. Both of these marks are information drawn over the view.
constexpr float kCurveColour[4] = {0.42f, 0.52f, 0.68f, 1.0f};
constexpr float kCurveThresholdColour[4] = {0.60f, 0.58f, 0.52f, 1.0f};

const float* teamColour(std::int32_t team) {
  if (team == 2) return kTeamRedColour;
  if (team == 3) return kTeamBluColour;
  return kTeamNeutralColour;
}

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

void addRect(TimelineHudStrip& strip, float x, float y, float w, float h, const float colour[4]) {
  if (w < 0.5f || h < 0.5f) return;
  TimelineHudShape shape;
  shape.kind = TimelineHudShape::Kind::Rect;
  shape.x = x;
  shape.y = y;
  shape.w = w;
  shape.h = h;
  for (int index = 0; index < 4; ++index) shape.colour[index] = colour[index];
  strip.primitives.push_back(std::move(shape));
}

void addText(TimelineHudStrip& strip, float x, float y, float w, const std::string& text) {
  if (text.empty()) return;
  TimelineHudShape shape;
  shape.kind = TimelineHudShape::Kind::Text;
  shape.x = x;
  shape.y = y;
  shape.w = w;
  shape.h = kLabelPixelSize * 1.4f;
  shape.pixelSize = kLabelPixelSize;
  for (int index = 0; index < 4; ++index) shape.colour[index] = kLabelColour[index];
  shape.text = text;
  strip.primitives.push_back(std::move(shape));
}

} // namespace

TimelineHudStrip composeTimelineStrip(const MatchTimelineReport& report,
                                      int clientW, int clientH,
                                      std::int32_t playheadPlaybackTick,
                                      const TimelineHudAvoidRect& avoid,
                                      float bottomInset,
                                      const std::vector<TimelineHudAnomaly>& anomalies,
                                      const TimelineHudFocus& focus,
                                      const TimelineHudView& view) {
  TimelineHudStrip strip;
  if (!report.valid || report.rounds.empty()) return strip;
  const std::int32_t first = report.firstServerTick;
  const std::int32_t last = report.lastServerTick;
  if (last - first < kMinTickSpan) return strip;
  const float width = static_cast<float>(clientW);
  const float height = static_cast<float>(clientH);
  if (width <= 0.0f || height <= 0.0f) return strip;

  // The strip claims the bottom band, left to right, but gives way to a panel that is
  // already there (the minimap is docked bottom-left). It shrinks to the free width
  // and hides rather than becoming a tick-less sliver.
  float x = kMargin;
  float w = width - 2.0f * kMargin;
  const float y = height - bottomInset - kMargin - kStripHeight;
  if (avoid.placed) {
    const bool overlaps = x < avoid.x + avoid.w && avoid.x < x + w
        && y < avoid.y + avoid.h && avoid.y < y + kStripHeight;
    if (overlaps) {
      const float left = avoid.x - kAvoidGap;
      const float right = x + w;
      if (avoid.x + avoid.w * 0.5f < x + w * 0.5f) {
        // The panel sits on the left: keep the strip to its right.
        const float newX = std::max(x, avoid.x + avoid.w + kAvoidGap);
        w = right - newX;
        x = newX;
      } else {
        w = left - x;
      }
    }
  }
  if (w < kMinStripWidth || y < 0.0f) return strip;

  strip.visible = true;
  strip.x = x;
  strip.y = y;
  strip.w = w;
  strip.h = kStripHeight;
  strip.firstServerTick = first;
  strip.lastServerTick = last;
  strip.trackX0 = x + 6.0f;
  strip.trackX1 = x + w - 6.0f;
  strip.trackY = y + kTrackTop;
  strip.trackH = kTrackHeight;
  strip.anomalyLaneY = y + kAnomalyLaneTop;
  strip.anomalyLaneH = kAnomalyLaneHeight;
  strip.cellsY = y + kCellsTop;
  strip.cellsH = kCellsHeight;
  strip.presenceY = y + kPresenceTop;
  strip.presenceH = kPresenceHeight;
  strip.curveY = y + kCurveTop;
  strip.curveH = kCurveHeight;
  // The threshold line's own y: the reference the bars are read against, so a gate can
  // assert it without recomputing the scale.
  strip.curveThresholdY =
      strip.curveY + kCurveHeight * (1.0f - kCurveThresholdDegrees / kCurveTopDegrees);

  const float span = static_cast<float>(last - first);
  const float trackW = strip.trackX1 - strip.trackX0;
  const auto tickToX = [&](std::int32_t tick) {
    const std::int32_t clamped = std::clamp(tick, first, last);
    return strip.trackX0 + (static_cast<float>(clamped - first) / span) * trackW;
  };

  // The track itself, then one band per round. A band runs to the next round's start
  // (that is what endServerTick already is), so the axis has no gaps between rounds.
  addRect(strip, strip.trackX0, strip.trackY, trackW, kTrackHeight, kTrackBaseColour);
  // A3: the exception track. Its base is drawn even when there is nothing on it, so the
  // lane reads as "scanned, nothing found" rather than as a missing feature.
  addRect(strip, strip.trackX0, strip.anomalyLaneY, trackW, kAnomalyLaneHeight,
          kAnomalyLaneColour);
  for (const TimelineHudAnomaly& anomaly : anomalies) {
    if (anomaly.endTick < first) {
      ++strip.marksBefore;
      continue;
    }
    if (anomaly.startTick > last) {
      ++strip.marksAfter;
      continue;
    }
    const std::int32_t start = std::max(anomaly.startTick, first);
    const std::int32_t end = std::max(anomaly.endTick, start);
    // A mark exactly on the last tick maps to trackX1, where any width would be clipped
    // to zero and the mark would silently disappear -- measured on cp_snakewater, six
    // of the report's 32 in-round anomalies sat on the round's final tick and the lane
    // read 26. Pull such a mark back by its own minimum width so the last tick is still
    // drawn and the lane's count always equals the list it came from.
    float markX = tickToX(start);
    if (markX > strip.trackX1 - kAnomalyBlockMinWidth) {
      markX = strip.trackX1 - kAnomalyBlockMinWidth;
    }
    // Width is the duration, so a 40-tick stall is visibly 40 ticks wide; a one-tick
    // teleport still gets a mark narrow enough to read (2 px) but not a fake duration.
    const float markW = std::max(kAnomalyBlockMinWidth, tickToX(end) - markX);
    const float clampedW = std::min(markW, strip.trackX1 - markX);
    if (clampedW <= 0.0f) continue;
    TimelineHudAnomalyMark mark;
    mark.kind = anomaly.kind;
    mark.startTick = start;
    mark.endTick = end;
    mark.x = markX;
    mark.w = clampedW;
    mark.hint = anomaly.kind == 2;
    if (mark.hint) {
      ++strip.anomalyHints;
      addRect(strip, markX, strip.anomalyLaneY + kAnomalyLaneHeight - kAnomalyHintHeight,
              clampedW, kAnomalyHintHeight, kAnomalyHintColour);
    } else {
      if (anomaly.kind == 1) {
        ++strip.teleports;
      } else {
        ++strip.stalls;
      }
      ++strip.anomalyBlocks;
      addRect(strip, markX, strip.anomalyLaneY, clampedW, kAnomalyLaneHeight,
              kAnomalyBlockColour);
    }
    strip.marks.push_back(mark);
  }
  for (const MatchTimelineRound& round : report.rounds) {
    const float bandX0 = tickToX(round.startServerTick);
    const float bandX1 = tickToX(round.endServerTick > round.startServerTick
        ? round.endServerTick : round.startServerTick + 1);
    const float bandW = bandX1 - bandX0;
    if (bandW < 0.5f) continue;
    const bool even = (round.index % 2) == 0;
    addRect(strip, bandX0, strip.trackY, bandW, kTrackHeight,
            even ? kBandColour : kBandAltColour);
    // The winner's stripe on the top edge; a round without a reported win keeps grey
    // (section 9: 进行中的局用灰色), including one whose recording simply stopped.
    addRect(strip, bandX0, strip.trackY - kStripeHeight, bandW, kStripeHeight,
            round.winTeam == 2 || round.winTeam == 3 ? teamColour(round.winTeam) : kWinGreyColour);
    TimelineHudBand band;
    band.index = round.index;
    band.winTeam = round.winTeam;
    band.x = bandX0;
    band.w = bandW;
    if (bandW >= kMinBandLabelWidth) {
      // "R1" above the band: the smallest label that answers "which round is this",
      // and it is ASCII so it survives any text face.
      std::ostringstream label;
      label << "R" << (round.index + 1);
      addText(strip, bandX0 + 2.0f, strip.y + kLabelTop, bandW - 4.0f, label.str());
      band.labelPlaced = true;
    }
    strip.bands.push_back(band);
  }

  // Kills: RED above the midline, BLU below, unknown team as a short centred tick so
  // it reads as "there was a kill, side not recorded" instead of borrowing a side.
  for (const MatchTimelineKill& kill : report.kills) {
    const float killX = tickToX(kill.serverTick);
    if (kill.attackerTeam == 2) {
      addRect(strip, killX, strip.trackY - kKillTickHeight, 1.0f, kKillTickHeight, kTeamRedColour);
      ++strip.ticksRed;
    } else if (kill.attackerTeam == 3) {
      addRect(strip, killX, strip.trackY + kTrackHeight, 1.0f, kKillTickHeight, kTeamBluColour);
      ++strip.ticksBlu;
    } else {
      addRect(strip, killX, strip.trackY + kTrackHeight * 0.5f - 1.5f, 1.0f, 3.0f,
              kUnknownTeamTickColour);
      ++strip.ticksUnknown;
    }
  }

  // A3b: the per-tick row (section 9 line 238). Only when an anomaly is focused: the row
  // is a window onto one exception, not a second axis, so a strip nobody focused keeps
  // exactly the shapes it drew in A3.
  if (focus.valid && focus.endTick >= first && focus.startTick <= last) {
    const std::int32_t half = std::max<std::int32_t>(0, focus.halfWindow);
    std::int32_t windowStart = std::max(first, focus.startTick - half);
    std::int32_t windowEnd = std::min(last, focus.endTick + half);
    if (windowEnd - windowStart + 1 > kMaxCells) {
      windowEnd = windowStart + kMaxCells - 1;
    }
    if (windowEnd >= windowStart) {
      strip.focusValid = true;
      strip.focusKind = focus.kind;
      strip.focusStartTick = focus.startTick;
      strip.focusEndTick = focus.endTick;
      strip.focusWindowStart = windowStart;
      strip.focusWindowEnd = windowEnd;
      const std::int32_t count = windowEnd - windowStart + 1;
      const float cellW = trackW / static_cast<float>(count);
      strip.cellW = cellW;
      // One pixel of gap between cells so a run of anomalous ticks reads as cells rather
      // than as one wider block; a cell never draws narrower than a pixel.
      const float cellDrawW = std::max(1.0f, cellW - kCellGap);
      for (std::int32_t tick = windowStart; tick <= windowEnd; ++tick) {
        const bool inspan = tick >= focus.startTick && tick <= focus.endTick;
        TimelineHudCell cell;
        cell.serverTick = tick;
        // The row is a ZOOMED ruler, not a second axis mark: the window's ticks are spread
        // across the whole track, so each cell is legible even when the window is a few
        // hundredths of a pixel wide on the axis (41 ticks of snakewater are 0.8 px).
        cell.x = strip.trackX0 + static_cast<float>(tick - windowStart) * cellW;
        cell.w = cellW;
        cell.hint = inspan && focus.kind == 2;
        cell.anomaly = inspan && !cell.hint;
        if (cell.anomaly) ++strip.anomalyCells;
        if (cell.hint) ++strip.hintCells;
        ++strip.cells;
        addRect(strip, cell.x, strip.cellsY, cellDrawW, kCellsHeight,
                cell.anomaly ? kAnomalyBlockColour
                             : (cell.hint ? kAnomalyHintColour : kCellColour));
        strip.cellMarks.push_back(cell);
      }
    }
  }

  // A4 (section 9.2 items 1 and 3): the two follow-only layers. Both are drawn from the
  // followed player's own data, and both are skipped wholesale when nobody is followed:
  // an empty band would claim "no sharp turns / not in the recording", which is not what
  // a spectator view with no follow says.
  if (view.valid && span > 0.0f) {
    strip.viewValid = true;
    strip.viewEntity = view.entity;

    // 3) the presence bar: the player's write spans tinted by m_lifeState at the time. A gap between
    // spans is real information in a POV recording -- the engine does not write an entity
    // nobody can see -- so gaps stay empty rather than guessed at.
    for (const TimelineHudPresenceRun& run : view.presence) {
      if (run.state == 0) continue;
      if (run.endTick < first || run.startTick > last) continue;
      const std::int32_t start = std::max(run.startTick, first);
      const std::int32_t end = std::min(std::max(run.endTick, start), last);
      const float runX = tickToX(start);
      // A run covers its ticks inclusively: take the next tick's edge so a one-tick run is
      // still a visible sliver instead of a zero-width rect, and let the axis end cap it.
      const float runEndX = end < last ? tickToX(end + 1) : strip.trackX1;
      float runW = runEndX - runX;
      if (runW < 1.0f) runW = 1.0f;
      if (runX + runW > strip.trackX1) runW = strip.trackX1 - runX;
      if (runW <= 0.0f) continue;
      addRect(strip, runX, strip.presenceY, runW, kPresenceHeight,
              run.state == 1 ? kPresenceAliveColour : kPresenceDeadColour);
      strip.presenceMarks.push_back(run);
      ++strip.presenceRuns;
      if (run.state == 1) {
        ++strip.presenceAlive;
      } else {
        ++strip.presenceDead;
      }
    }

    // 1) the view turn-rate curve. The threshold line is drawn first so the bars sit over
    // it, and a bar over the threshold borrows the exception lane's red: the same colour
    // means the same word across the strip.
    addRect(strip, strip.trackX0, strip.curveThresholdY, trackW, 1.0f, kCurveThresholdColour);
    const float curveBase = strip.curveY + kCurveHeight;
    // The axis is ~1200 px wide but a recording has tens of ticks per pixel, so the curve
    // is a per-column MAXIMUM and not one bar per tick: the tallest tick in a column is
    // what the eye reads, and one primitive per tick would be ~64k quads for 1200 visible
    // pixels. Counted totals stay per tick, so the trace still says how much data there is.
    const int columns = static_cast<int>(trackW);
    if (columns > 0) {
      std::vector<float> columnDegrees(static_cast<std::size_t>(columns), -1.0f);
      std::vector<unsigned char> columnOver(static_cast<std::size_t>(columns), 0);
      for (const TimelineHudTurnPoint& turn : view.turns) {
        if (turn.tick < first || turn.tick > last) continue;
        const int column = std::clamp(
            static_cast<int>(tickToX(turn.tick) - strip.trackX0), 0, columns - 1);
        float& peak = columnDegrees[static_cast<std::size_t>(column)];
        if (static_cast<float>(turn.degrees) > peak) peak = static_cast<float>(turn.degrees);
        // Red follows the detector's own flag, not a second threshold: the curve must not
        // disagree with the hints the same scan produced.
        if (turn.over) columnOver[static_cast<std::size_t>(column)] = 1;
        // Only the first rows are kept for the trace; the totals below are the real count.
        if (strip.turnMarks.size() < kMaxTurnLines) strip.turnMarks.push_back(turn);
        ++strip.viewTurns;
        if (turn.over) ++strip.viewTurnsOver;
      }
      for (int column = 0; column < columns; ++column) {
        const float degrees = columnDegrees[static_cast<std::size_t>(column)];
        if (degrees < 0.0f) continue;
        const float barH = std::max(1.0f,
            std::clamp(degrees / kCurveTopDegrees, 0.0f, 1.0f) * kCurveHeight);
        addRect(strip, strip.trackX0 + static_cast<float>(column), curveBase - barH,
                kCurveBarWidth, barH,
                columnOver[static_cast<std::size_t>(column)] ? kAnomalyBlockColour
                                                             : kCurveColour);
      }
    }
    strip.viewMaxDegrees = static_cast<float>(view.maxDegrees);
  }

  // The playhead is the one thing that moves every frame; it is drawn last so it sits
  // over every tick it crosses.
  if (playheadPlaybackTick >= 0) {
    const std::int32_t serverTick = playheadPlaybackTick + report.serverToEntryOffset;
    strip.playheadServerTick = serverTick;
    strip.playheadX = tickToX(serverTick);
    addRect(strip, strip.playheadX - 1.0f, strip.trackY - kStripeHeight - kPlayheadOvershoot,
            2.0f, kStripeHeight + kTrackHeight + 2.0f * kPlayheadOvershoot, kPlayheadColour);
  }

  strip.shapes = strip.primitives.size();
  return strip;
}

std::vector<std::string> formatTimelineHudLines(const TimelineHudStrip& strip) {
  std::vector<std::string> lines;
  {
    std::ostringstream out;
    out << "timeline-hud visible=" << (strip.visible ? 1 : 0);
    if (!strip.visible) {
      lines.push_back(out.str() + " reason=no-axis");
      return lines;
    }
    out << " x=" << std::lround(strip.x) << " y=" << std::lround(strip.y)
        << " w=" << std::lround(strip.w) << " h=" << std::lround(strip.h)
        << " trackX0=" << std::lround(strip.trackX0) << " trackX1=" << std::lround(strip.trackX1)
        << " trackY=" << std::lround(strip.trackY) << " trackH=" << std::lround(strip.trackH)
        << " first=" << strip.firstServerTick << " last=" << strip.lastServerTick
        << " bands=" << strip.bands.size()
        << " shapes=" << strip.shapes
        << " ticksRed=" << strip.ticksRed << " ticksBlu=" << strip.ticksBlu
        << " ticksUnknown=" << strip.ticksUnknown
        << " playhead=" << strip.playheadServerTick
        << " playheadVisible=" << (strip.playheadServerTick >= 0 ? 1 : 0)
        << " playheadX=" << std::lround(strip.playheadX)
        << " anomalies=" << strip.anomalyBlocks << " stalls=" << strip.stalls
        << " teleports=" << strip.teleports << " hints=" << strip.anomalyHints
        << " before=" << strip.marksBefore << " after=" << strip.marksAfter
        << " laneY=" << std::lround(strip.anomalyLaneY)
        << " laneH=" << std::lround(strip.anomalyLaneH)
        << " focus=" << (strip.focusValid ? 1 : 0) << " focusKind=" << strip.focusKind
        << " windowStart=" << strip.focusWindowStart << " windowEnd=" << strip.focusWindowEnd
        << " cells=" << strip.cells << " anomalyCells=" << strip.anomalyCells
        << " hintCells=" << strip.hintCells << " cellW=" << std::lround(strip.cellW)
        << " cellsY=" << std::lround(strip.cellsY) << " cellsH=" << std::lround(strip.cellsH)
        << " view=" << (strip.viewValid ? 1 : 0) << " viewEntity=" << strip.viewEntity
        << " presenceY=" << std::lround(strip.presenceY)
        << " presenceH=" << std::lround(strip.presenceH)
        << " curveY=" << std::lround(strip.curveY) << " curveH=" << std::lround(strip.curveH)
        << " curveThresholdY=" << std::lround(strip.curveThresholdY)
        << " presenceRuns=" << strip.presenceRuns << " presenceAlive=" << strip.presenceAlive
        << " presenceDead=" << strip.presenceDead << " turns=" << strip.viewTurns
        << " turnsOver=" << strip.viewTurnsOver
        << " maxDeg=" << std::lround(strip.viewMaxDegrees);
    lines.push_back(out.str());
  }
  std::size_t printed = 0;
  for (const TimelineHudBand& band : strip.bands) {
    if (printed >= kMaxBandLines) break;
    std::ostringstream out;
    out << "timeline-band index=" << band.index << " winTeam=" << band.winTeam
        << " x=" << std::lround(band.x) << " w=" << std::lround(band.w)
        << " label=" << (band.labelPlaced ? 1 : 0);
    lines.push_back(out.str());
    ++printed;
  }
  printed = 0;
  for (const TimelineHudAnomalyMark& mark : strip.marks) {
    if (printed >= kMaxAnomalyLines) break;
    std::ostringstream out;
    out << "timeline-anomaly kind=" << mark.kind << " hint=" << (mark.hint ? 1 : 0)
        << " start=" << mark.startTick << " end=" << mark.endTick
        << " x=" << std::lround(mark.x) << " w=" << std::lround(mark.w);
    lines.push_back(out.str());
    ++printed;
  }
  printed = 0;
  for (const TimelineHudCell& cell : strip.cellMarks) {
    if (printed >= kMaxCellLines) break;
    std::ostringstream out;
    out << "timeline-cell tick=" << cell.serverTick
        << " kind=" << (cell.anomaly ? "anomaly" : (cell.hint ? "hint" : "base"))
        << " x=" << std::lround(cell.x) << " w=" << std::lround(cell.w);
    lines.push_back(out.str());
    ++printed;
  }
  printed = 0;
  for (const TimelineHudPresenceRun& run : strip.presenceMarks) {
    if (printed >= kMaxPresenceLines) break;
    std::ostringstream out;
    out << "timeline-presence run=" << (printed + 1) << " state=" << run.state
        << " start=" << run.startTick << " end=" << run.endTick;
    lines.push_back(out.str());
    ++printed;
  }
  printed = 0;
  for (const TimelineHudTurnPoint& turn : strip.turnMarks) {
    if (printed >= kMaxTurnLines) break;
    std::ostringstream out;
    out << "timeline-turn tick=" << turn.tick << " deg=" << std::lround(turn.degrees)
        << " over=" << (turn.over ? 1 : 0);
    lines.push_back(out.str());
    ++printed;
  }
  return lines;
}

TimelineHudView timelineHudViewFromHistory(const MotionViewHistory& history) {
  TimelineHudView view;
  if (!history.valid) return view;
  view.valid = true;
  view.entity = history.entity;
  view.presence.reserve(history.presence.size());
  for (const MotionViewPresence& run : history.presence) {
    TimelineHudPresenceRun out;
    out.startTick = run.startTick;
    out.endTick = run.endTick;
    out.state = static_cast<int>(run.state);
    view.presence.push_back(out);
  }
  view.turns.reserve(history.turns.size());
  for (const MotionViewTurn& turn : history.turns) {
    TimelineHudTurnPoint out;
    out.tick = turn.tick;
    out.degrees = turn.degrees;
    out.over = turn.overThreshold;
    view.turns.push_back(out);
  }
  view.turnsOver = history.turnsOverThreshold;
  view.maxDegrees = history.maxDegrees;
  view.aliveTicks = history.aliveTicks;
  view.deadTicks = history.deadTicks;
  view.noDataTicks = history.noDataTicks;
  return view;
}

bool timelineStripTickAt(const TimelineHudStrip& strip, float clientX, float clientY,
                         std::int32_t& serverTick) {
  if (!strip.visible) return false;
  if (clientX < strip.trackX0 || clientX > strip.trackX1) return false;
  if (clientY < strip.y || clientY > strip.y + strip.h) return false;
  // The per-tick row answers with the cell under the point: one cell is one tick, so this
  // is the tick the user pointed at, not a proportional estimate of one.
  if (strip.focusValid && clientY >= strip.cellsY && clientY <= strip.cellsY + strip.cellsH) {
    for (const TimelineHudCell& cell : strip.cellMarks) {
      if (clientX >= cell.x && clientX < cell.x + cell.w) {
        serverTick = cell.serverTick;
        return true;
      }
    }
    // Past the last cell's right edge, still inside the row: the window's last tick.
    serverTick = strip.focusWindowEnd;
    return true;
  }
  // Everywhere else the axis answers, with the mapping the marks were drawn by.
  const float trackW = strip.trackX1 - strip.trackX0;
  const float span = static_cast<float>(strip.lastServerTick - strip.firstServerTick);
  if (trackW <= 0.0f || span <= 0.0f) return false;
  const float ratio = (clientX - strip.trackX0) / trackW;
  serverTick = strip.firstServerTick + static_cast<std::int32_t>(std::lround(ratio * span));
  serverTick = std::clamp(serverTick, strip.firstServerTick, strip.lastServerTick);
  return true;
}

} // namespace tf2::native
