#include "timeline_hud.h"

#include <algorithm>
#include <cmath>
#include <sstream>

namespace tf2::native {
namespace {

// Section 9 puts the strip at the bottom of the workspace; these are the strip's own
// proportions, not the design's type scale. The strip is 56 px tall: 18 for the round
// labels, 6 for the exception track, 3 for the winner stripe, 8 for the track and the
// rest for the RED/BLU ticks, which need to read at a glance without being taller than
// the track itself.
constexpr float kMargin = 12.0f;
constexpr float kStripHeight = 56.0f;
constexpr float kAnomalyLaneTop = 20.0f;
constexpr float kAnomalyLaneHeight = 6.0f;
constexpr float kAnomalyHintHeight = 2.0f;
constexpr float kAnomalyBlockMinWidth = 2.0f;
constexpr float kTrackTop = 38.0f;
constexpr float kTrackHeight = 8.0f;
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
                                      const std::vector<TimelineHudAnomaly>& anomalies) {
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
        << " laneH=" << std::lround(strip.anomalyLaneH);
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
  return lines;
}

} // namespace tf2::native
