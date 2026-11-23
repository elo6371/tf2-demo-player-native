// H1 (2026-11-23, UI shell design section 7): the broadcast HUD composer.
//
// The overlay is drawn from the same primitives the readout, the minimap, the
// overview and the timeline strip already use, so this file is pure geometry and
// text: it asks no window for a size, touches no renderer and reads no file. The
// class chips are the ONE thing it does not compute itself -- the overview already
// decodes the VGUI portraits into 8x8 cell runs (map_overview.cpp resolveClassIcon)
// and mapOverviewClassChip hands those cells over, so both screens draw the same
// icon instead of decoding it twice.

#include "broadcast_hud.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "timeline_hud.h"

namespace tf2::native {
namespace {

// Design units are 1080p: every number below was chosen at 1920x1080 and is
// multiplied by clientHeight / 1080, so a 720p window is the same picture smaller
// rather than a different layout. The clamp keeps a tiny window readable and a
// huge one from growing the bar past the frame.
constexpr double kDesignHeight = 1080.0;
constexpr double kScaleMin = 0.55;
constexpr double kScaleMax = 2.0;

constexpr double kBarWidth = 560.0;
constexpr double kBarHeight = 44.0;
constexpr double kBarTop = 14.0;
constexpr double kHintGap = 4.0;
constexpr double kHintHeight = 16.0;
constexpr double kColumnWidth = 280.0;
constexpr double kRowHeight = 32.0;
constexpr double kRowGap = 2.0;
constexpr int kRowsMax = 9;
constexpr double kSideMargin = 12.0;

// The overlay's own palette. Team colours are NOT here: they are the timeline
// module's kTeamRedColour / kTeamBluColour, because the strip and this overlay are
// in the same frame (see the header comment).
constexpr float kPanelColour[4] = {0.055f, 0.065f, 0.085f, 0.78f};
constexpr float kPanelDeadColour[4] = {0.045f, 0.048f, 0.058f, 0.62f};
constexpr float kEdgeColour[4] = {0.38f, 0.42f, 0.50f, 0.85f};
constexpr float kTextColour[4] = {0.94f, 0.95f, 0.97f, 1.0f};
constexpr float kMutedColour[4] = {0.62f, 0.66f, 0.72f, 0.95f};
constexpr float kDeadColour[4] = {0.50f, 0.52f, 0.56f, 0.85f};
constexpr float kHealthGoodColour[4] = {0.34f, 0.78f, 0.42f, 1.0f};
constexpr float kHealthLowColour[4] = {0.95f, 0.62f, 0.20f, 1.0f};
constexpr float kHealthOverColour[4] = {0.45f, 0.88f, 0.72f, 1.0f};
constexpr float kChargeColour[4] = {0.95f, 0.78f, 0.26f, 1.0f};
constexpr float kChargeReadyColour[4] = {1.0f, 0.42f, 0.38f, 1.0f};
constexpr float kBarTrackColour[4] = {0.10f, 0.11f, 0.14f, 0.90f};

float clampf(double value, double low, double high) {
  if (value < low) return static_cast<float>(low);
  if (value > high) return static_cast<float>(high);
  return static_cast<float>(value);
}

void pushRect(std::vector<Renderer::HudPrimitive>& out, float x, float y, float w,
    float h, const float colour[4]) {
  if (w <= 0.0f || h <= 0.0f) return;
  Renderer::HudPrimitive primitive;
  primitive.kind = Renderer::HudPrimitiveKind::Rect;
  primitive.x = x;
  primitive.y = y;
  primitive.w = w;
  primitive.h = h;
  for (int channel = 0; channel < 4; ++channel) primitive.colour[channel] = colour[channel];
  primitive.filled = true;
  out.push_back(std::move(primitive));
}

void pushText(std::vector<Renderer::HudPrimitive>& out, float x, float y, float pixelSize,
    const std::string& text, const float colour[4], HudTextAlign align = HudTextAlign::Left,
    float maxWidth = 0.0f, HudTextFont font = HudTextFont::UiSemiBold) {
  if (text.empty()) return;
  Renderer::HudPrimitive primitive;
  primitive.kind = Renderer::HudPrimitiveKind::Text;
  primitive.x = x;
  primitive.y = y;
  primitive.w = maxWidth;
  primitive.h = pixelSize;
  for (int channel = 0; channel < 4; ++channel) primitive.colour[channel] = colour[channel];
  primitive.text = text;
  primitive.font = font;
  primitive.pixelSize = pixelSize;
  primitive.align = align;
  primitive.maxWidth = maxWidth;
  out.push_back(std::move(primitive));
}

const char* teamName(std::int32_t team) {
  if (team == 2) return "RED";
  if (team == 3) return "BLU";
  return "?";
}

const float* teamColour(std::int32_t team) {
  if (team == 2) return kTeamRedColour;
  if (team == 3) return kTeamBluColour;
  return kTeamNeutralColour;
}

// The class letter is the fallback for a VGUI portrait this install does not have:
// a row must still say WHICH class it is, and the letter is already the overview's
// own answer (map_overview.cpp classRow(cls)->letter).
char classLetter(std::int32_t cls) {
  switch (cls) {
    case 1: return 'S';   // Scout
    case 2: return 'N';   // Sniper
    case 3: return 'Y';   // Soldier
    case 4: return 'D';   // Demoman
    case 5: return 'M';   // Medic
    case 6: return 'H';   // Heavy
    case 7: return 'P';   // Pyro
    case 8: return 'S';   // Spy
    case 9: return 'E';   // Engineer
    default: return '?';
  }
}

// The clock: m:ss with a leading minus while it counts down, or the paused token.
std::string clockText(const BroadcastHudLive& live) {
  if (!live.roundTimerKnown) return "--:--";
  const double bounded = live.roundTimerSeconds < 0.0 ? 0.0 : live.roundTimerSeconds;
  const int total = static_cast<int>(std::floor(bounded));
  const int minutes = total / 60;
  const int seconds = total % 60;
  char buffer[32];
  std::snprintf(buffer, sizeof(buffer), "%d:%02d", minutes, seconds);
  return buffer;
}

// One row's source: the player and the Medic Gun he owns (if any). Kept apart from
// BroadcastHudRow because the row carries geometry, which is decided after the sort.
struct RowSource {
  const OverviewPlayer* player = nullptr;
  const BroadcastHudMedigun* gun = nullptr;
  bool medic = false;
};

const BroadcastHudMedigun* findGun(const BroadcastHudInput& input, std::int32_t entity) {
  const BroadcastHudMedigun* first = nullptr;
  for (const BroadcastHudMedigun& gun : input.mediguns) {
    if (gun.owner != entity) continue;
    // A medic can carry one gun, but a demo can hold two of this owner's weapon
    // entities across time; prefer the one that is actually charged/healing, and
    // otherwise keep the first, so the row never flickers between them.
    if (gun.chargeKnown || gun.chargeRelease) return &gun;
    if (first == nullptr) first = &gun;
  }
  return first;
}

}  // namespace

BroadcastHudDraw broadcastHudCompose(const BroadcastHudInput& input) {
  BroadcastHudDraw draw;
  draw.active = true;
  draw.clientW = input.clientWidth;
  draw.clientH = input.clientHeight;
  draw.playersTotal = static_cast<int>(input.players.size());
  if (input.clientWidth <= 0 || input.clientHeight <= 0) {
    draw.reason = "no-client";
    return draw;
  }
  draw.scale = input.clientHeight / kDesignHeight;
  if (draw.scale < kScaleMin) draw.scale = kScaleMin;
  if (draw.scale > kScaleMax) draw.scale = kScaleMax;
  const double s = draw.scale;
  const int clientW = input.clientWidth;
  const int clientH = input.clientHeight;

  // ---- top bar: BLU on the left, the clock in the middle, RED on the right,
  // which is the way a broadcast reads a score, and the two rosters below follow
  // the same order (blue column left, red column right).
  draw.barW = static_cast<int>(std::lround(kBarWidth * s));
  draw.barH = static_cast<int>(std::lround(kBarHeight * s));
  draw.barX = (clientW - draw.barW) / 2;
  draw.barY = static_cast<int>(std::lround(kBarTop * s));
  if (draw.barX < 0) {
    // A window narrower than the bar: the bar shrinks rather than spilling off the
    // left edge, so the trace's x/w stay a rectangle the caller can crop.
    draw.barW = clientW - 2 * static_cast<int>(std::lround(kSideMargin * s));
    draw.barX = static_cast<int>(std::lround(kSideMargin * s));
    if (draw.barW <= 0) {
      draw.reason = "no-client";
      return draw;
    }
  }
  const float barX = static_cast<float>(draw.barX);
  const float barY = static_cast<float>(draw.barY);
  const float barW = static_cast<float>(draw.barW);
  const float barH = static_cast<float>(draw.barH);

  pushRect(draw.primitives, barX, barY, barW, barH, kPanelColour);
  // The 2px top edge is the one bit of chrome that makes the bar read as a panel
  // instead of a hole in the picture (the same device the review banner uses).
  pushRect(draw.primitives, barX, barY, barW, std::max(1.0f, static_cast<float>(2.0 * s)),
    kEdgeColour);

  const float scoreSize = static_cast<float>(std::lround(26.0 * s));
  const float labelSize = static_cast<float>(std::lround(14.0 * s));
  const float cellY = barY + (barH - scoreSize) * 0.5f;
  // Left third: team chip, team name, score.
  const float chipSize = static_cast<float>(std::lround(14.0 * s));
  const float chipY = barY + (barH - chipSize) * 0.5f;
  pushRect(draw.primitives, barX + 14.0f * static_cast<float>(s), chipY, chipSize, chipSize,
    kTeamBluColour);
  pushText(draw.primitives, barX + 34.0f * static_cast<float>(s), barY + (barH - labelSize) * 0.5f,
    labelSize, "BLU", kMutedColour);
  pushText(draw.primitives, barX + barW * 0.5f - 24.0f * static_cast<float>(s), cellY, scoreSize,
    input.live.bluKnown ? std::to_string(input.live.blu) : std::string("?"), kTextColour,
    HudTextAlign::Right, 60.0f * static_cast<float>(s), HudTextFont::UiBold);
  // Right third: mirrored.
  pushRect(draw.primitives, barX + barW - 14.0f * static_cast<float>(s) - chipSize, chipY,
    chipSize, chipSize, kTeamRedColour);
  pushText(draw.primitives, barX + barW - 34.0f * static_cast<float>(s),
    barY + (barH - labelSize) * 0.5f, labelSize, "RED", kMutedColour, HudTextAlign::Right,
    60.0f * static_cast<float>(s));
  pushText(draw.primitives, barX + barW * 0.5f + 24.0f * static_cast<float>(s), cellY, scoreSize,
    input.live.redKnown ? std::to_string(input.live.red) : std::string("?"), kTextColour,
    HudTextAlign::Left, 60.0f * static_cast<float>(s), HudTextFont::UiBold);
  // The clock, centred: the one field a broadcast never hides.
  pushText(draw.primitives, barX + barW * 0.5f, cellY, scoreSize, clockText(input.live),
    input.live.roundTimerPaused ? kMutedColour : kTextColour, HudTextAlign::Center,
    140.0f * static_cast<float>(s), HudTextFont::UiBold);
  if (input.live.roundTimerPaused) {
    // A paused clock says so, beside itself, rather than by drawing a different
    // glyph: the number above is still the number the server holds.
    pushText(draw.primitives, barX + barW * 0.5f + 74.0f * static_cast<float>(s),
      barY + (barH - labelSize) * 0.5f, labelSize, "PAUSED", kMutedColour);
  }

  // The hint line under the bar: what is being watched. Not a reading from disk --
  // the caller passes the demo's own map name and, when it has one, the mode.
  {
    const std::string hint = input.modeHint.empty() ? input.mapName : input.modeHint;
    draw.hintW = static_cast<int>(std::lround(420.0 * s));
    draw.hintH = static_cast<int>(std::lround(kHintHeight * s));
    draw.hintX = (clientW - draw.hintW) / 2;
    draw.hintY = draw.barY + draw.barH + static_cast<int>(std::lround(kHintGap * s));
    if (!hint.empty()) {
      const float hintSize = static_cast<float>(std::lround(13.0 * s));
      pushText(draw.primitives, barX + barW * 0.5f,
        static_cast<float>(draw.hintY) + (static_cast<float>(draw.hintH) - hintSize) * 0.5f,
        hintSize, hint, kMutedColour, HudTextAlign::Center, static_cast<float>(draw.hintW));
    }
  }

  // ---- the two rosters. A player is a row; the team's Medic Gun row comes first,
  // because the charge is the thing a spectator watches that nobody else's row
  // carries.
  std::vector<RowSource> blu;
  std::vector<RowSource> red;
  for (const OverviewPlayer& player : input.players) {
    if (player.entity < 1) continue;
    if (player.team != 2 && player.team != 3) continue;
    RowSource source;
    source.player = &player;
    source.gun = findGun(input, player.entity);
    source.medic = (source.gun != nullptr && source.gun->owner == player.entity)
      || player.cls == 5;
    (player.team == 3 ? blu : red).push_back(source);
  }
  // Stable: within a team the decoder's entity order is the roster order, so the
  // rows do not shuffle when a health tick changes nothing.
  const auto byMedic = [](const RowSource& a, const RowSource& b) {
    if (a.medic != b.medic) return a.medic;
    return false;
  };
  std::stable_sort(blu.begin(), blu.end(), byMedic);
  std::stable_sort(red.begin(), red.end(), byMedic);

  draw.columnW = static_cast<int>(std::lround(kColumnWidth * s));
  draw.rowH = static_cast<int>(std::lround(kRowHeight * s));
  draw.rowGap = static_cast<int>(std::lround(kRowGap * s));
  // Up to nine rows fit under the bar; the rest are published as clipped rather
  // than dropped silently, which is the difference between "a 6v6 demo" and "the
  // roster is wrong".
  int capacity = static_cast<int>(std::floor((clientH - (draw.barY + draw.barH) - 40.0 * s)
    / static_cast<double>(draw.rowH + draw.rowGap)));
  if (capacity > kRowsMax) capacity = kRowsMax;
  if (capacity < 1) capacity = 1;

  const int rowPitch = draw.rowH + draw.rowGap;
  draw.bluX = static_cast<int>(std::lround(kSideMargin * s));
  draw.redX = clientW - static_cast<int>(std::lround(kSideMargin * s)) - draw.columnW;
  if (draw.redX < draw.bluX + draw.columnW + 8) {
    // The columns would touch: keep the left one where it is and let the right one
    // meet it, so the trace shows the squeeze instead of an overlap.
    draw.redX = draw.bluX + draw.columnW + 8;
  }
  // Vertically centred in the space under the hint, but never above it.
  const int topLimit = draw.hintY + draw.hintH + static_cast<int>(std::lround(10.0 * s));
  const int bluCount = static_cast<int>(std::min<std::size_t>(blu.size(), static_cast<std::size_t>(capacity)));
  const int redCount = static_cast<int>(std::min<std::size_t>(red.size(), static_cast<std::size_t>(capacity)));
  const int tallest = std::max(bluCount, redCount);
  int startY = (clientH - tallest * rowPitch) / 2;
  if (startY < topLimit) startY = topLimit;
  draw.bluY = startY;
  draw.redY = startY;

  const float nameSize = static_cast<float>(std::lround(15.0 * s));
  const float smallSize = static_cast<float>(std::lround(12.0 * s));
  const float chipBox = static_cast<float>(std::lround(24.0 * s));

  const auto placeRows = [&](const std::vector<RowSource>& source, std::int32_t team, int x,
                             int y) {
    for (std::size_t index = 0; index < source.size(); ++index) {
      const RowSource& entry = source[index];
      const OverviewPlayer& player = *entry.player;
      BroadcastHudRow row;
      row.entity = player.entity;
      row.team = team;
      row.cls = player.cls;
      row.medic = entry.medic;
      row.alive = player.alive;
      row.name = player.name;
      row.health = player.health;
      row.maxHealth = player.maxHealth;
      row.healthKnown = player.healthKnown;
      if (player.maxHealth > 0) {
        row.healthFraction = static_cast<double>(player.health)
          / static_cast<double>(player.maxHealth);
      }
      row.overheal = row.healthKnown && player.maxHealth > 0 && player.health > player.maxHealth;
      row.low = row.healthKnown && row.healthFraction < 0.5;
      row.deathKnown = player.deathKnown && !player.alive;
      if (player.deathKnown && !player.alive && input.tickRate > 1.0) {
        const double seconds = static_cast<double>(input.serverTick - player.deathServerTick)
          / input.tickRate;
        row.deadSeconds = seconds > 0.0 ? seconds : 0.0;
      }
      if (entry.gun != nullptr) {
        row.chargeKnown = entry.gun->chargeKnown;
        row.chargeFraction = entry.gun->chargeKnown ? entry.gun->chargeLevel : 0.0;
        row.chargeRelease = entry.gun->chargeRelease;
        row.chargePercent = static_cast<int>(std::lround(row.chargeFraction * 100.0));
      }
      const bool placed = static_cast<int>(index) < capacity;
      row.placed = placed;
      if (placed) {
        row.x = x;
        row.y = y + static_cast<int>(index) * rowPitch;
        row.w = draw.columnW;
        row.h = draw.rowH;
        if (row.medic) ++draw.medicRows;
        if (row.chargeKnown) ++draw.chargeRows;
        if (!row.alive) ++draw.deadRows;
        if (row.low) ++draw.lowRows;
        if (row.overheal) ++draw.overRows;
        if (!row.healthKnown) ++draw.unknownRows;

        const float rx = static_cast<float>(row.x);
        const float ry = static_cast<float>(row.y);
        const float rw = static_cast<float>(row.w);
        const float rh = static_cast<float>(row.h);
        pushRect(draw.primitives, rx, ry, rw, rh, row.alive ? kPanelColour : kPanelDeadColour);
        pushRect(draw.primitives, rx, ry, std::max(1.0f, static_cast<float>(3.0 * s)), rh,
          teamColour(team));
        // Class chip: the decoded VGUI portrait as cell runs, else the class letter.
        float iconX = rx + 9.0f * static_cast<float>(s);
        const float iconY = ry + (rh - chipBox) * 0.5f;
        pushRect(draw.primitives, iconX, iconY, chipBox, chipBox, kBarTrackColour);
        std::vector<OverviewClassChip> cells;
        if (player.cls >= 1 && player.cls <= 9 && mapOverviewClassChip(player.cls, cells)) {
          const float unit = chipBox / 8.0f;
          for (const OverviewClassChip& cell : cells) {
            const float cellW = unit * static_cast<float>(cell.w);
            const float cellH = unit * static_cast<float>(cell.h);
            Renderer::HudPrimitive chip;
            chip.kind = Renderer::HudPrimitiveKind::Rect;
            chip.x = iconX + unit * static_cast<float>(cell.x);
            chip.y = iconY + unit * static_cast<float>(cell.y);
            chip.w = cellW > 0.5f ? cellW : unit;
            chip.h = cellH > 0.5f ? cellH : unit;
            chip.colour[0] = static_cast<float>((cell.rgb >> 16) & 0xffu) / 255.0f;
            chip.colour[1] = static_cast<float>((cell.rgb >> 8) & 0xffu) / 255.0f;
            chip.colour[2] = static_cast<float>(cell.rgb & 0xffu) / 255.0f;
            chip.colour[3] = row.alive ? 1.0f : 0.55f;
            chip.filled = true;
            draw.primitives.push_back(std::move(chip));
          }
          ++draw.classChips;
        } else {
          ++draw.classChipFallbacks;
          pushText(draw.primitives, iconX + chipBox * 0.5f, iconY + chipBox * 0.5f - smallSize * 0.5f,
            smallSize, std::string(1, classLetter(player.cls)), kMutedColour, HudTextAlign::Center,
            chipBox);
        }
        const float textX = iconX + chipBox + 7.0f * static_cast<float>(s);
        const float nameW = rw - (textX - rx) - 62.0f * static_cast<float>(s);
        const std::string label = player.nameKnown && !player.name.empty() ? player.name : "?";
        pushText(draw.primitives, textX, ry + 4.0f * static_cast<float>(s), nameSize, label,
          row.alive ? kTextColour : kDeadColour, HudTextAlign::Left, nameW);
        // Right column of the row: health, or for a medic the charge, or the
        // respawn countdown when the player is dead.
        const float rightX = rx + rw - 8.0f * static_cast<float>(s);
        if (!row.alive) {
          char buffer[24];
          if (row.deathKnown) {
            std::snprintf(buffer, sizeof(buffer), "%.0fs", row.deadSeconds);
          } else {
            std::snprintf(buffer, sizeof(buffer), "dead");
          }
          pushText(draw.primitives, rightX, ry + 4.0f * static_cast<float>(s), nameSize, buffer,
            kDeadColour, HudTextAlign::Right, 52.0f * static_cast<float>(s));
        } else if (row.medic && row.chargeKnown) {
          char buffer[24];
          std::snprintf(buffer, sizeof(buffer), "%d%%", row.chargePercent);
          pushText(draw.primitives, rightX, ry + 4.0f * static_cast<float>(s), nameSize, buffer,
            row.chargeRelease ? kChargeReadyColour : kChargeColour, HudTextAlign::Right,
            52.0f * static_cast<float>(s), HudTextFont::UiBold);
        } else if (row.healthKnown) {
          pushText(draw.primitives, rightX, ry + 4.0f * static_cast<float>(s), nameSize,
            std::to_string(row.health),
            row.overheal ? kHealthOverColour : (row.low ? kHealthLowColour : kTextColour),
            HudTextAlign::Right, 52.0f * static_cast<float>(s));
        } else {
          pushText(draw.primitives, rightX, ry + 4.0f * static_cast<float>(s), nameSize, "?",
            kMutedColour, HudTextAlign::Right, 52.0f * static_cast<float>(s));
        }
        // The bar under the name: charge for a medic's row, health for everybody
        // else. Three pixels is enough to read a fraction at broadcast distance and
        // small enough that it is not mistaken for the HP bar of the game's HUD.
        const float trackY = ry + rh - 6.0f * static_cast<float>(s);
        const float trackW = rw - 18.0f * static_cast<float>(s);
        pushRect(draw.primitives, rx + 9.0f * static_cast<float>(s), trackY, trackW,
          std::max(2.0f, static_cast<float>(3.0 * s)), kBarTrackColour);
        if (row.medic && row.chargeKnown) {
          pushRect(draw.primitives, rx + 9.0f * static_cast<float>(s), trackY,
            trackW * clampf(row.chargeFraction, 0.0, 1.0),
            std::max(2.0f, static_cast<float>(3.0 * s)),
            row.chargeRelease ? kChargeReadyColour : kChargeColour);
        } else if (row.healthKnown && row.maxHealth > 0) {
          pushRect(draw.primitives, rx + 9.0f * static_cast<float>(s), trackY,
            trackW * clampf(row.healthFraction, 0.0, 1.0),
            std::max(2.0f, static_cast<float>(3.0 * s)),
            row.overheal ? kHealthOverColour : (row.low ? kHealthLowColour : kHealthGoodColour));
        }
        ++draw.rowsDrawn;
      } else {
        ++draw.rowsClipped;
      }
      draw.rows.push_back(std::move(row));
    }
  };
  placeRows(blu, 3, draw.bluX, draw.bluY);
  placeRows(red, 2, draw.redX, draw.redY);
  draw.bluRows = bluCount;
  draw.redRows = redCount;

  // ---- the followed player's box, bottom centre. It is the one piece of the
  // overlay that names a player on screen: the relay's answer, not a guess.
  if (input.observedEntity >= 1) {
    draw.observedW = static_cast<int>(std::lround(260.0 * s));
    draw.observedH = static_cast<int>(std::lround(56.0 * s));
    draw.observedX = (clientW - draw.observedW) / 2;
    draw.observedY = clientH - draw.observedH - static_cast<int>(std::lround(96.0 * s));
    const OverviewPlayer* observed = nullptr;
    for (const OverviewPlayer& player : input.players) {
      if (player.entity == input.observedEntity) {
        observed = &player;
        break;
      }
    }
    const std::int32_t team = observed != nullptr ? observed->team : 0;
    const float ox = static_cast<float>(draw.observedX);
    const float oy = static_cast<float>(draw.observedY);
    const float ow = static_cast<float>(draw.observedW);
    const float oh = static_cast<float>(draw.observedH);
    pushRect(draw.primitives, ox, oy, ow, oh, kPanelColour);
    pushRect(draw.primitives, ox, oy, ow, std::max(1.0f, static_cast<float>(2.0 * s)),
      teamColour(team));
    const std::string label = input.observedNameKnown && !input.observedName.empty()
      ? input.observedName : (observed != nullptr && observed->nameKnown ? observed->name : "?");
    const float boxName = static_cast<float>(std::lround(17.0 * s));
    pushText(draw.primitives, ox + 12.0f * static_cast<float>(s),
      oy + 8.0f * static_cast<float>(s), boxName, label,
      observed != nullptr && !observed->alive ? kDeadColour : kTextColour, HudTextAlign::Left,
      ow - 24.0f * static_cast<float>(s), HudTextFont::UiBold);
    if (observed != nullptr) {
      const float healthSize = static_cast<float>(std::lround(20.0 * s));
      const std::string health = observed->healthKnown ? std::to_string(observed->health) : "?";
      pushText(draw.primitives, ox + 12.0f * static_cast<float>(s),
        oy + 32.0f * static_cast<float>(s), healthSize, health,
        (observed->healthKnown && observed->maxHealth > 0
          && observed->health * 2 < observed->maxHealth)
          ? kHealthLowColour : kTextColour, HudTextAlign::Left,
        60.0f * static_cast<float>(s), HudTextFont::UiBold);
      // The observed player's own Medic Gun charge, when he is a medic: the same
      // number the roster row carries, on the box a director watches.
      if (observed->cls == 5) {
        const BroadcastHudMedigun* gun = findGun(input, observed->entity);
        if (gun != nullptr && gun->chargeKnown) {
          char buffer[32];
          std::snprintf(buffer, sizeof(buffer), "UBER %d%%",
            static_cast<int>(std::lround(gun->chargeLevel * 100.0)));
          pushText(draw.primitives, ox + ow - 12.0f * static_cast<float>(s),
            oy + 34.0f * static_cast<float>(s), smallSize, buffer,
            gun->chargeRelease ? kChargeReadyColour : kChargeColour, HudTextAlign::Right,
            120.0f * static_cast<float>(s));
        }
      }
      if (observed->team == 2 || observed->team == 3) {
        pushText(draw.primitives, ox + ow - 12.0f * static_cast<float>(s),
          oy + 10.0f * static_cast<float>(s), smallSize, teamName(observed->team),
          kMutedColour, HudTextAlign::Right, 40.0f * static_cast<float>(s));
      }
    }
    ++draw.observedDrawn;
  }

  // ---- signature and counters. The signature carries every number the picture
  // depends on, so the caller's publish key changes exactly when the overlay does.
  draw.rectPrimitives = 0;
  draw.textPrimitives = 0;
  for (const Renderer::HudPrimitive& primitive : draw.primitives) {
    if (primitive.kind == Renderer::HudPrimitiveKind::Text) {
      ++draw.textPrimitives;
    } else {
      ++draw.rectPrimitives;
    }
  }
  char head[192];
  std::snprintf(head, sizeof(head), "b:%dx%d:%.3f:%d,%d,%d,%d:%d,%d:%d,%d:%d,%d:%d",
    clientW, clientH, s, draw.barX, draw.barY, draw.barW, draw.barH,
    input.live.redKnown ? input.live.red : -1, input.live.bluKnown ? input.live.blu : -1,
    input.live.roundTimerKnown ? static_cast<int>(std::lround(input.live.roundTimerSeconds)) : -1,
    input.live.roundTimerPaused ? 1 : 0, draw.rowsDrawn, draw.rowsClipped,
    input.observedEntity);
  draw.signature = head;
  for (const BroadcastHudRow& row : draw.rows) {
    if (!row.placed) continue;
    char line[128];
    std::snprintf(line, sizeof(line), "|%d,%d,%d,%d,%d,%d,%d,%d,%d",
      row.entity, row.team, row.cls, row.alive ? 1 : 0, row.healthKnown ? row.health : -1,
      row.medic ? 1 : 0, row.chargeKnown ? row.chargePercent : -1,
      row.deathKnown ? static_cast<int>(std::lround(row.deadSeconds)) : -1,
      row.overheal ? 1 : 0);
    draw.signature += line;
  }
  draw.visible = !draw.primitives.empty();
  draw.reason = draw.visible ? "ready" : "no-primitives";
  return draw;
}

std::vector<std::string> formatBroadcastHudLines(const BroadcastHudDraw& draw) {
  std::vector<std::string> lines;
  char buffer[512];
  std::snprintf(buffer, sizeof(buffer),
    "broadcast: hud active=%d visible=%d reason=%s client=%dx%d scale=%.3f"
    " bar=%d,%d,%d,%d hint=%d,%d,%d,%d columns=%d,%d row=%d,%d"
    " players=%d rows=%d clipped=%d blu=%d red=%d medic=%d charge=%d dead=%d low=%d"
    " over=%d unknown=%d observed=%d chips=%d chipFallbacks=%d rects=%zu texts=%zu",
    draw.active ? 1 : 0, draw.visible ? 1 : 0, draw.reason.c_str(), draw.clientW,
    draw.clientH, draw.scale, draw.barX, draw.barY, draw.barW, draw.barH, draw.hintX,
    draw.hintY, draw.hintW, draw.hintH, draw.bluX, draw.redX, draw.rowH, draw.rowGap,
    draw.playersTotal, draw.rowsDrawn, draw.rowsClipped, draw.bluRows, draw.redRows,
    draw.medicRows, draw.chargeRows, draw.deadRows, draw.lowRows, draw.overRows,
    draw.unknownRows, draw.observedDrawn, draw.classChips, draw.classChipFallbacks,
    draw.rectPrimitives, draw.textPrimitives);
  lines.emplace_back(buffer);
  if (draw.active && draw.visible) {
    std::snprintf(buffer, sizeof(buffer),
      "broadcast: observe entity=%d x=%d y=%d w=%d h=%d", draw.observedDrawn,
      draw.observedX, draw.observedY, draw.observedW, draw.observedH);
    lines.emplace_back(buffer);
  }
  for (const BroadcastHudRow& row : draw.rows) {
    std::snprintf(buffer, sizeof(buffer),
      "broadcast: row entity=%d team=%d cls=%d medic=%d alive=%d placed=%d"
      " x=%d y=%d w=%d h=%d health=%d max=%d healthKnown=%d fraction=%.3f"
      " low=%d over=%d chargeKnown=%d charge=%d%% release=%d deadKnown=%d deadSec=%.1f"
      " name=%s",
      row.entity, row.team, row.cls, row.medic ? 1 : 0, row.alive ? 1 : 0,
      row.placed ? 1 : 0, row.x, row.y, row.w, row.h, row.health, row.maxHealth,
      row.healthKnown ? 1 : 0, row.healthFraction, row.low ? 1 : 0, row.overheal ? 1 : 0,
      row.chargeKnown ? 1 : 0, row.chargePercent, row.chargeRelease ? 1 : 0,
      row.deathKnown ? 1 : 0, row.deadSeconds, row.name.empty() ? "-" : row.name.c_str());
    lines.emplace_back(buffer);
  }
  return lines;
}

}  // namespace tf2::native
