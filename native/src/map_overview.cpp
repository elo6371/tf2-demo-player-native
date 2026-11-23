#include "map_overview.h"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "asset_lookup.h"
#include "hud_font.h"
#include "main_internal.h"
#include "map_basemap.h"
#include "vmt_material.h"
#include "vpk_archive.h"
#include "vtf_texture.h"

namespace tf2::native {
namespace {

// ---------------------------------------------------------------- the screen
// Every number here is a reading a human can check against the trace, not an
// aesthetic knob: the gate recomputes the map rect from these constants.
constexpr int kMargin = 12;          // window edge -> minimap / team panels
// Height of the live readout block in the top-left corner (three lines of 22px
// plus the 16px glyph cell, main_hud.cpp:494-498). The RED roster starts below it.
constexpr int kRosterTopBelowReadout = 84;
constexpr int kPanelSide = 250;      // USER DECISION m08430: 250x250, fixed size
constexpr int kMinPanel = 60;        // below this the panel is not drawn at all
constexpr int kBorder = 2;           // panel border thickness
constexpr int kMapInset = 2;         // border -> image/mapping area
constexpr int kTeamWidth = 220;      // one team panel, default width
constexpr int kTeamHeader = 26;
constexpr int kTeamRow = 30;
constexpr int kIcon = 16;            // class chip, pixels
constexpr int kIconGrid = 8;         // VTF downsample grid (2 px cells)
constexpr int kBadgeH = 9;           // LEGACY badge strip above the map rect

struct Rgb { float r, g, b; };
constexpr Rgb kTeamRed{0.72f, 0.23f, 0.25f};
constexpr Rgb kTeamBlu{0.33f, 0.52f, 0.66f};
constexpr Rgb kText{0.93f, 0.93f, 0.95f};
constexpr Rgb kMuted{0.55f, 0.55f, 0.58f};
constexpr Rgb kDead{0.42f, 0.42f, 0.45f};
constexpr Rgb kOverheal{0.36f, 0.86f, 0.44f};
constexpr Rgb kWarn{0.92f, 0.42f, 0.40f};
constexpr Rgb kScrim{0.02f, 0.02f, 0.03f};
constexpr Rgb kFrame{0.55f, 0.56f, 0.60f};
constexpr Rgb kPanelBg{0.09f, 0.09f, 0.11f};
constexpr Rgb kBarBg{0.18f, 0.18f, 0.20f};
constexpr Rgb kImageBg{0.13f, 0.13f, 0.15f};

// ------------------------------------------------------------- process state
bool g_open = false;
std::string g_tfRoot;
std::int32_t g_hiddenEntity = -1;

// ------------------------------------------------------------- class table
// The class name/letter and the class's BASE max health. Max health is a class
// property, not a demo reading: TF2 does not replicate it, and deriving it from
// observed health would make overheal invisible on every frame below max.
struct ClassRow {
  std::int32_t id;
  const char* name;
  const char* letter;
  std::int32_t maxHealth;
};
constexpr ClassRow kClasses[] = {
  {1, "scout", "sc", 125},
  {2, "sniper", "sn", 125},
  {3, "soldier", "so", 200},
  {4, "demoman", "de", 175},
  {5, "medic", "me", 150},
  {6, "engineer", "en", 125},
  {7, "heavy", "he", 300},
  {8, "pyro", "py", 175},
  {9, "spy", "sp", 125},
};

const ClassRow* classRow(std::int32_t id) {
  for (const ClassRow& row : kClasses) if (row.id == id) return &row;
  return nullptr;
}

// ------------------------------------------------------------- primitive aid
void appendRect(std::vector<Renderer::HudPrimitive>& out, float x, float y,
    float w, float h, const Rgb& colour, float alpha = 1.0f) {
  if (w <= 0.0f || h <= 0.0f) return;
  Renderer::HudPrimitive primitive;
  primitive.kind = Renderer::HudPrimitiveKind::Rect;
  primitive.x = x;
  primitive.y = y;
  primitive.w = w;
  primitive.h = h;
  primitive.colour[0] = colour.r;
  primitive.colour[1] = colour.g;
  primitive.colour[2] = colour.b;
  primitive.colour[3] = alpha;
  primitive.filled = true;
  primitive.scale = 1.0f;
  out.push_back(std::move(primitive));
}

// Text is ALWAYS ASCII here and always scale 1 (the 8x8 font cell), because the
// panel is a fixed-width table: a proportional path would make the HP column
// move with the name, and the gate reads the column, not the name.
void appendText(std::vector<Renderer::HudPrimitive>& out, float x, float y,
    const std::string& text, const Rgb& colour, float alpha = 1.0f) {
  if (text.empty()) return;
  Renderer::HudPrimitive primitive;
  primitive.kind = Renderer::HudPrimitiveKind::Text;
  primitive.x = x;
  primitive.y = y;
  primitive.w = static_cast<float>(text.size()) * 8.0f;
  primitive.h = 8.0f;
  primitive.colour[0] = colour.r;
  primitive.colour[1] = colour.g;
  primitive.colour[2] = colour.b;
  primitive.colour[3] = alpha;
  primitive.filled = false;
  primitive.text = text;
  primitive.scale = 1.0f;
  out.push_back(std::move(primitive));
}

std::string asciiProject(const std::string& text, std::size_t maxChars) {
  std::string out;
  for (unsigned char c : text) {
    if (c < 0x20 || c > 0x7E) {
      out.push_back('?');
    } else {
      out.push_back(static_cast<char>(c));
    }
    if (out.size() >= maxChars) break;
  }
  return out;
}

// ---------------------------------------------------- class-icon mosaic cells
// The class portraits are a VTF decode into an 8x8 grid of cells, so this type
// survives the basemap move: the MAP is a texture now (map_basemap.h), the tiny
// class chips are still box runs (the HUD has no per-primitive texture atlas).
struct MosaicCell {
  int x = 0;
  int y = 0;
  int w = 1;
  int h = 1;
  std::uint32_t rgb = 0;
};

// ------------------------------------------------------------ class icons
// The chain is the product's own AssetStoreChain over the same *_dir.vpk files,
// not a second lookup: material_slot_resolution.h:119-151 is the pattern (VMT ->
// baseTexture -> VTF -> decode). The candidate stems are the VGUI class-icon
// families Source HUDs use; whichever instance this install has wins, and the
// trace names the one that resolved so a reader can tell 'no icon' from
// 'looked in the wrong place'.
struct ClassIcon {
  bool tried = false;
  bool resolved = false;
  std::string reason = "not-tried";
  std::string stem;
  std::string vtfPath;
  std::vector<MosaicCell> cells;
};
std::array<ClassIcon, 10> g_classIcons;
std::unique_ptr<AssetStoreChain> g_chain;
std::string g_chainReason = "not-built";

bool ensureChain() {
  if (g_chain) return true;
  if (g_tfRoot.empty()) { g_chainReason = "no-tf-root"; return false; }
  std::error_code error;
  const std::filesystem::path root = std::filesystem::u8path(g_tfRoot);
  if (!std::filesystem::is_directory(root, error)) { g_chainReason = "tf-root-missing"; return false; }
  auto chain = std::make_unique<AssetStoreChain>(root);
  std::vector<std::filesystem::path> archives;
  for (std::filesystem::directory_iterator it(root, error), end; !error && it != end; it.increment(error)) {
    if (!it->is_regular_file(error)) continue;
    const std::string name = it->path().filename().string();
    if (name.size() < 8 || name.compare(name.size() - 8, 8, "_dir.vpk") != 0) continue;
    archives.push_back(it->path());
  }
  std::sort(archives.begin(), archives.end());
  for (const std::filesystem::path& archivePath : archives) {
    auto archive = std::make_shared<VpkArchive>();
    std::string openError;
    if (!archive->open(archivePath, &openError)) continue;
    chain->addArchive(archive, archivePath);
  }
  if (chain->archiveCount() == 0) { g_chainReason = "no-archives"; return false; }
  g_chain = std::move(chain);
  g_chainReason = "ok";
  return true;
}

const char* classStem(std::int32_t cls) {
  const ClassRow* row = classRow(cls);
  return row ? row->name : "";
}

void resolveClassIcon(std::int32_t cls) {
  if (cls < 1 || cls > 9) return;
  ClassIcon& icon = g_classIcons[static_cast<std::size_t>(cls)];
  if (icon.tried) return;
  icon.tried = true;
  if (!ensureChain()) { icon.reason = g_chainReason; return; }
  const std::string name = classStem(cls);
  if (name.empty()) { icon.reason = "no-class-name"; return; }
  const std::array<std::string, 5> stems = {
    "materials/vgui/hud/class_icons/" + name,
    "materials/vgui/hud/class_icons_32/" + name,
    "materials/vgui/hud/class_portraits/" + name,
    "materials/vgui/hud/classicon_" + name,
    "materials/vgui/hud/class_icons/class_" + name,
  };
  for (const std::string& stem : stems) {
    const std::string vmtPath = VmtParser::resourcePath(stem, ".vmt");
    const AssetLookup vmt = g_chain->lookup(vmtPath);
    if (!vmt.found() || vmt.bytes.empty()) continue;
    VmtMaterial material;
    if (!VmtParser::parse(std::string(vmt.bytes.begin(), vmt.bytes.end()), material,
          [&](const std::string& resource) { return g_chain->lookup(resource).bytes; })) continue;
    const std::string& base = material.baseTexture.empty() ? material.fallbackBaseTexture
        : material.baseTexture;
    if (base.empty()) continue;
    const std::string vtfPath = VmtParser::resourcePath(base, ".vtf");
    const AssetLookup vtf = g_chain->lookup(vtfPath);
    if (!vtf.found() || vtf.bytes.empty()) continue;
    VtfTexture texture;
    if (!texture.parse(vtf.bytes)) continue;
    std::string decodeError;
    const std::vector<std::uint8_t> rgba = texture.decodeRgba(vtf.bytes, &decodeError);
    const int width = static_cast<int>(texture.header().width);
    const int height = static_cast<int>(texture.header().height);
    if (rgba.size() < static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4
        || width <= 0 || height <= 0) continue;
    // Downsample to an 8x8 alpha-weighted average: the chip is 16 px, so this is
    // 2 px per cell and the icon reads as the same silhouette at panel scale.
    std::vector<std::uint32_t> grid(static_cast<std::size_t>(kIconGrid) * kIconGrid, 0);
    std::vector<std::uint8_t> alpha(static_cast<std::size_t>(kIconGrid) * kIconGrid, 0);
    for (int gy = 0; gy < kIconGrid; ++gy) {
      for (int gx = 0; gx < kIconGrid; ++gx) {
        const int x0 = gx * width / kIconGrid;
        const int x1 = std::max(x0 + 1, (gx + 1) * width / kIconGrid);
        const int y0 = gy * height / kIconGrid;
        const int y1 = std::max(y0 + 1, (gy + 1) * height / kIconGrid);
        double r = 0.0, g = 0.0, b = 0.0, a = 0.0;
        int count = 0;
        for (int y = y0; y < y1; ++y) {
          for (int x = x0; x < x1; ++x) {
            const std::size_t at = (static_cast<std::size_t>(y) * static_cast<std::size_t>(width)
                + static_cast<std::size_t>(x)) * 4;
            const double pxAlpha = rgba[at + 3] / 255.0;
            r += rgba[at + 0] * pxAlpha;
            g += rgba[at + 1] * pxAlpha;
            b += rgba[at + 2] * pxAlpha;
            a += pxAlpha;
            ++count;
          }
        }
        if (count == 0 || a <= 0.0) continue;
        const int gr = static_cast<int>(r / a + 0.5);
        const int gg = static_cast<int>(g / a + 0.5);
        const int gb = static_cast<int>(b / a + 0.5);
        const std::size_t at = static_cast<std::size_t>(gy) * kIconGrid + static_cast<std::size_t>(gx);
        grid[at] = (static_cast<std::uint32_t>(std::min(gr, 255)) << 16)
            | (static_cast<std::uint32_t>(std::min(gg, 255)) << 8)
            | static_cast<std::uint32_t>(std::min(gb, 255));
        alpha[at] = static_cast<std::uint8_t>(std::min(255.0, a / count * 255.0));
      }
    }
    std::vector<MosaicCell> cells;
    for (int gy = 0; gy < kIconGrid; ++gy) {
      for (int gx = 0; gx < kIconGrid; ++gx) {
        const std::size_t at = static_cast<std::size_t>(gy) * kIconGrid + static_cast<std::size_t>(gx);
        if (alpha[at] < 128) continue;
        MosaicCell cell;
        cell.x = gx;
        cell.y = gy;
        cell.w = 1;
        cell.h = 1;
        cell.rgb = grid[at];
        cells.push_back(cell);
      }
    }
    if (cells.empty()) continue;
    icon.resolved = true;
    icon.reason = "ok";
    icon.stem = stem;
    icon.vtfPath = vtfPath;
    icon.cells = std::move(cells);
    return;
  }
  icon.reason = "no-candidate-vmt";
}

// --------------------------------------------------------------- death table
// player_death carries userid; the userinfo table carries userid -> entity. The
// countdown therefore starts from the demo's own event, never from 'the health
// went to zero at some tick', and a player with no event gets no countdown
// (deadSince=-1) instead of a made-up one.
struct DeathTable {
  const DemoNetworkSummary* summary = nullptr;
  std::size_t scanned = 0;
  std::int32_t serverNow = std::numeric_limits<std::int32_t>::min();
  std::unordered_map<std::uint32_t, std::int32_t> deathTick;
  std::unordered_map<std::uint32_t, std::int32_t> spawnTick;
};
DeathTable g_deaths;

std::string eventField(const DemoGameEvent& event, const char* name) {
  for (const auto& field : event.fields) if (field.first == name) return field.second;
  return std::string();
}

std::int32_t eventIntField(const DemoGameEvent& event, const char* name) {
  const std::string text = eventField(event, name);
  if (text.empty()) return -1;
  char* end = nullptr;
  const long long value = std::strtoll(text.c_str(), &end, 10);
  if (!end || *end != '\0' || value < 0 || value > 0x7FFFFFFF) return -1;
  return static_cast<std::int32_t>(value);
}

void updateDeathTable(const DemoNetworkSummary& summary, std::int32_t serverNow) {
  if (g_deaths.summary != &summary || serverNow < g_deaths.serverNow) {
    g_deaths = DeathTable{};
    g_deaths.summary = &summary;
  }
  if (serverNow <= g_deaths.serverNow) {
    g_deaths.serverNow = serverNow;
    return;
  }
  while (g_deaths.scanned < summary.gameEvents.size()) {
    const DemoGameEvent& event = summary.gameEvents[g_deaths.scanned];
    if (event.tick > serverNow) break;
    if (event.name == "player_death") {
      const std::int32_t victim = eventIntField(event, "userid");
      const std::int32_t userid = victim >= 0 ? victim : eventIntField(event, "victim");
      if (userid >= 0) g_deaths.deathTick[static_cast<std::uint32_t>(userid)] = event.tick;
    } else if (event.name == "player_spawn") {
      const std::int32_t userid = eventIntField(event, "userid");
      if (userid >= 0) g_deaths.spawnTick[static_cast<std::uint32_t>(userid)] = event.tick;
    }
    ++g_deaths.scanned;
  }
  g_deaths.serverNow = serverNow;
}

// ------------------------------------------------------------- entity reads
const EntityPropertyValue* findNamed(const EntityState& state, const char* key) {
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
  return exact ? exact : dotted;
}

bool readInt(const EntityState& state, const char* key, std::int32_t& out) {
  const EntityPropertyValue* value = findNamed(state, key);
  if (!value || value->type != SendPropType::Int) return false;
  if (value->intValue < std::numeric_limits<std::int32_t>::min()
      || value->intValue > std::numeric_limits<std::int32_t>::max()) return false;
  out = static_cast<std::int32_t>(value->intValue);
  return true;
}

// A player carries m_vecOrigin in more than one slot and the slots are not
// equally fresh (demo_header.h:92-108 explains the stamp). The freshest slot
// wins, which is the same ranking entity_model.cpp's preferCandidate applies.
bool readPosition(const EntityState& state, float& x, float& y) {
  const EntityPropertyValue* best = nullptr;
  for (const auto& entry : state.properties) {
    // ".m_vecOrigin" is ELEVEN characters (dot + 10). Comparing only the last ten
    // matched nothing for the dotted keys the snapshot actually carries
    // ("DT_BasePlayer.m_vecOrigin"), so every marker lost its origin while the
    // roster beside it kept drawing -- exactly the 0-icons / 12-skipped reading
    // this line used to produce. Same suffix rule as findNamed.
    const std::string& key = entry.first;
    const std::string suffix = ".m_vecOrigin";
    const bool matches = key == "m_vecOrigin"
        || (key.size() > suffix.size()
            && key.compare(key.size() - suffix.size(), suffix.size(), suffix) == 0);
    if (!matches) continue;
    const EntityPropertyValue& value = entry.second;
    if (value.type != SendPropType::Vector && value.type != SendPropType::VectorXY) continue;
    if (!std::isfinite(value.x) || !std::isfinite(value.y)) continue;
    if (!best || value.lastWriteTick > best->lastWriteTick) best = &value;
  }
  if (!best) return false;
  x = best->x;
  y = best->y;
  return true;
}

bool readYaw(const EntityState& state, float& yawDegrees) {
  const EntityPropertyValue* angles = findNamed(state, "m_angEyeAngles");
  if (angles && (angles->type == SendPropType::Vector || angles->type == SendPropType::VectorXY)
      && std::isfinite(angles->y)) {
    yawDegrees = angles->y;
    return true;
  }
  const EntityPropertyValue* yaw = findNamed(state, "m_angEyeAngles[1]");
  if (yaw && (yaw->type == SendPropType::Float || yaw->type == SendPropType::Int)) {
    yawDegrees = yaw->type == SendPropType::Int ? static_cast<float>(yaw->intValue) : yaw->x;
    return std::isfinite(yawDegrees);
  }
  const EntityPropertyValue* rotation = findNamed(state, "m_angRotation");
  if (rotation && (rotation->type == SendPropType::Vector || rotation->type == SendPropType::VectorXY)
      && std::isfinite(rotation->y)) {
    yawDegrees = rotation->y;
    return true;
  }
  return false;
}

}  // namespace

namespace {

// ------------------------------------------------------------------ layout
// The panel is 250 x 250 pinned to the top-right corner with a CONSTANT pixel
// margin (user decision m08430: on resize the margin is kept and the size stays
// 250). The margin is the one the old 160px panel used (12 px, main_hud.cpp:978
// kHudPanelSafeInset), so replacing it does not move the panel.
struct LayoutPanel {
  int x = 0;
  int y = 0;
  int side = 0;
  int mapX = 0;
  int mapY = 0;
  int mapSide = 0;
  bool valid = false;
  std::string reason = "not-placed";
};

LayoutPanel placeMinimap(int clientW, int clientH) {
  LayoutPanel panel;
  if (clientW <= 0 || clientH <= 0) { panel.reason = "no-client"; return panel; }
  // Clamp, never crash: a window narrower than the panel shrinks the panel to
  // what is left, and below kMinPanel the panel is not drawn (the team panels
  // still are -- a tiny window keeps its HP readings).
  int side = kPanelSide;
  if (clientW - 2 * kMargin < side) side = clientW - 2 * kMargin;
  if (clientH - 2 * kMargin < side) side = clientH - 2 * kMargin;
  if (side < kMinPanel) { panel.reason = "panel-too-small"; return panel; }
  panel.side = side;
  // BOTTOM-right since the 2026-11-17 alignment change: the top-right corner
  // belongs to the BLU roster now (both rosters share y). Old pin: y = kMargin
  // at the top-right; new pin: y = clientH - kMargin - side. Same 12px margin.
  panel.x = clientW - kMargin - side;
  panel.y = clientH - kMargin - side;
  panel.mapX = panel.x + kBorder + kMapInset;
  panel.mapY = panel.y + kBorder + kMapInset;
  panel.mapSide = side - 2 * (kBorder + kMapInset);
  panel.valid = panel.mapSide > 0;
  panel.reason = panel.valid ? "ok" : "map-area-empty";
  return panel;
}

struct TeamRect {
  int x = 0;
  int y = 0;
  int w = 0;
  int h = 0;
  int rows = 0;
};

// THE ALIGNMENT RULE (user decision 2026-11-17: the red and blue sides must
// line up, with time and score at the top centre). OLD LAYOUT: RED at y=96
// under the live readout, BLU at y=274 under the top-right 250px minimap -- a
// 178px difference the user read as the two sides not being aligned. NEW
// LAYOUT: both rosters share y, width and height and are mirrored about the
// client centre: RED.x = kMargin, BLU.x = clientW - kMargin - width, and both
// start at y = kMargin + kRosterTopBelowReadout (96, the readout's bottom edge
// 84 plus kMargin; the readout still owns the band above them). The caller
// passes a COMMON row count (the larger team) so the panels are identical even
// when one team has more players; each panel draws only its own rows and the
// difference is background. Height still clamps to the window, and compose
// drops the map when it would intersect either panel -- never an overlap.
TeamRect placeTeam(bool blu, int clientW, int clientH, int rowCount) {
  TeamRect rect;
  if (clientW <= 0 || clientH <= 0) return rect;
  int width = kTeamWidth;
  const int maxWidth = (clientW - 3 * kMargin) / 2;
  if (maxWidth < width) width = maxWidth;
  if (width < 80) return rect;
  rect.w = width;
  rect.x = blu ? (clientW - kMargin - width) : kMargin;
  rect.y = kMargin + kRosterTopBelowReadout;
  const int available = clientH - rect.y - kMargin;
  int rows = rowCount;
  const int maxRows = (available - kTeamHeader) / kTeamRow;
  if (rows > maxRows) rows = maxRows;
  if (rows < 0) rows = 0;
  rect.rows = rows;
  rect.h = kTeamHeader + rows * kTeamRow;
  if (rect.h > available) rect.h = available < 0 ? 0 : available;
  return rect;
}

// Strict overlap: touching edges do NOT count, matching the gate's overlaps
// helper. A panel that ends exactly where another rectangle begins is legal.
bool rectsOverlap(int ax, int ay, int aw, int ah, int bx, int by, int bw, int bh) {
  if (aw <= 0 || ah <= 0 || bw <= 0 || bh <= 0) return false;
  return ax < bx + bw && bx < ax + aw && ay < by + bh && by < ay + ah;
}

struct RowView {
  std::int32_t entity = -1;
  std::int32_t cls = 0;
  std::int32_t team = 0;
  std::string name;
  bool nameKnown = false;
  std::int32_t health = 0;
  std::int32_t maxHealth = 0;
  std::int32_t overheal = 0;
  bool alive = true;
  int deadSeconds = -1;
  int slot = 0;
};

std::string numberText(std::int32_t value) {
  char buffer[24]{};
  std::snprintf(buffer, sizeof(buffer), "%d", value);
  return std::string(buffer);
}

}  // namespace

// ------------------------------------------------------------ build input
OverviewInput mapOverviewBuildInput(const std::vector<EntityState>& states,
    const DemoNetworkSummary& summary, const std::string& mapName,
    int clientWidth, int clientHeight, std::int32_t tick, double tickRate) {
  OverviewInput input;
  input.mapName = mapName;
  input.clientWidth = clientWidth;
  input.clientHeight = clientHeight;
  input.tick = tick;
  input.tickRate = tickRate > 0.0 ? tickRate : 66.6666667;
  input.snapshotEntities = states.size();
  input.serverTick = summary.firstDataEntryTick >= 0
      ? summary.firstDataEntryTick + tick : tick;
  // The userinfo string table is the only place a demo carries a name
  // (demo_header.h:587-611). One entry per client slot; entity == slot + 1.
  std::unordered_map<std::uint32_t, const DemoUserInfoEntry*> byEntity;
  for (const DemoUserInfoEntry& entry : summary.userInfos) {
    byEntity[entry.entityIndex] = &entry;
  }
  updateDeathTable(summary, input.serverTick);
  for (std::size_t index = 1; index < states.size(); ++index) {
    const EntityState& state = states[index];
    std::int32_t team = 0;
    if (!readInt(state, "m_iTeamNum", team)) continue;
    if (team != 2 && team != 3) continue;
    std::int32_t cls = 0;
    if (!readInt(state, "m_iClass", cls)) continue;
    if (cls < 1 || cls > 9) continue;
    OverviewPlayer player;
    player.entity = static_cast<std::int32_t>(index);
    player.team = team;
    player.cls = cls;
    const ClassRow* row = classRow(cls);
    player.maxHealth = row ? row->maxHealth : 125;
    std::int32_t health = 0;
    player.healthKnown = readInt(state, "m_iHealth", health);
    if (player.healthKnown) player.health = health;
    std::int32_t lifeState = 0;
    const bool lifeKnown = readInt(state, "m_lifeState", lifeState);
    player.alive = (!player.healthKnown || player.health > 0) && (!lifeKnown || lifeState != 2);
    player.positionKnown = readPosition(state, player.x, player.y);
    player.yawKnown = readYaw(state, player.yawDegrees);
    const auto nameFound = byEntity.find(static_cast<std::uint32_t>(index));
    if (nameFound != byEntity.end() && nameFound->second && !nameFound->second->name.empty()) {
      player.name = asciiProject(nameFound->second->name, 18);
      player.nameKnown = true;
      const auto death = g_deaths.deathTick.find(nameFound->second->userId);
      if (death != g_deaths.deathTick.end()) {
        const auto spawn = g_deaths.spawnTick.find(nameFound->second->userId);
        if (spawn == g_deaths.spawnTick.end() || spawn->second <= death->second) {
          player.deathKnown = true;
          player.deathServerTick = death->second;
        }
      }
    }
    input.players.push_back(std::move(player));
  }
  return input;
}

// --------------------------------------------------------------- compose
OverviewDraw mapOverviewCompose(HWND window, const OverviewInput& input,
    Renderer* renderer) {
  OverviewDraw draw;
  draw.open = g_open;
  draw.mapName = input.mapName;
  if (!g_open) {
    draw.reason = "closed";
    // Non-empty on purpose: hudApplyLive folds this signature into the publish
    // key, and an empty one on the closing frame would leave the last painted
    // panel on screen with nothing scheduled to remove it.
    draw.signature = "overview-closed";
    return draw;
  }
  int clientW = input.clientWidth;
  int clientH = input.clientHeight;
  if ((clientW <= 0 || clientH <= 0) && window) {
    RECT client{};
    if (GetClientRect(window, &client)) {
      clientW = static_cast<int>(client.right - client.left);
      clientH = static_cast<int>(client.bottom - client.top);
    }
  }
  draw.screenX = 0;
  draw.screenY = 0;
  draw.screenSide = 0;
  draw.reason = "no-client";
  if (clientW <= 0 || clientH <= 0) return draw;
  const LayoutPanel panel = placeMinimap(clientW, clientH);
  draw.screenX = panel.x;
  draw.screenY = panel.y;
  draw.screenSide = panel.side;
  draw.border = kBorder;
  draw.mapX = panel.mapX;
  draw.mapY = panel.mapY;
  draw.mapSide = panel.mapSide;
  draw.panelW = kTeamWidth;
  draw.panelHeader = kTeamHeader;
  draw.panelRow = kTeamRow;
  // The readout block, straight from the caller: this is the rectangle the
  // collision rule above tests, and publishing it lets the gate assert the
  // pairwise non-intersection without re-deriving main_hud's constants.
  draw.readoutX = input.readoutX;
  draw.readoutY = input.readoutY;
  draw.readoutW = input.readoutW;
  draw.readoutH = input.readoutH;
  // 'no-client' is the placeholder set above; past the client check the compose
  // either placed the panel (ok) or has a reason of its own. Leaving the label at
  // no-client would make every trace claim the window had no client area.
  draw.reason = panel.valid ? "ok" : panel.reason;
  // Counts that a gate compares instead of recounting: a hidden player is not
  // part of any panel or map, and saying so once is cheaper than making a reader
  // wonder why the row count changed.
  std::vector<const OverviewPlayer*> visible;
  draw.playersTotal = static_cast<int>(input.players.size());
  for (const OverviewPlayer& player : input.players) {
    if (player.entity == g_hiddenEntity) { ++draw.playersHidden; continue; }
    visible.push_back(&player);
  }
  draw.playersShown = static_cast<int>(visible.size());
  // Rows: RED left, BLU right, each sorted class-then-name so the same roster
  // draws the same order on every frame (a stable sort on the pair, not the tick).
  std::vector<const OverviewPlayer*> red;
  std::vector<const OverviewPlayer*> blu;
  for (const OverviewPlayer* player : visible) {
    if (player->team == 2) red.push_back(player);
    else if (player->team == 3) blu.push_back(player);
    else ++draw.rowsSkipped;
  }
  const auto rowOrder = [](const OverviewPlayer* left, const OverviewPlayer* right) {
    if (left->cls != right->cls) return left->cls < right->cls;
    return left->name < right->name;
  };
  std::stable_sort(red.begin(), red.end(), rowOrder);
  std::stable_sort(blu.begin(), blu.end(), rowOrder);
  // A COMMON row count keeps the two panels exactly the same height even when
  // one team has fewer players (the user asked for the two sides to line up).
  // Each panel still draws only its own rows; the extra slots are background.
  const int commonRows = static_cast<int>(std::max(red.size(), blu.size()));
  const TeamRect redRect = placeTeam(false, clientW, clientH, commonRows);
  const TeamRect bluRect = placeTeam(true, clientW, clientH, commonRows);
  draw.redX = redRect.x;
  draw.redY = redRect.y;
  draw.redW = redRect.w;
  draw.redH = redRect.h;
  draw.bluX = bluRect.x;
  draw.bluY = bluRect.y;
  draw.bluW = bluRect.w;
  draw.bluH = bluRect.h;
  draw.rowsRed = redRect.rows;
  draw.rowsBlu = bluRect.rows;
  std::vector<Renderer::HudPrimitive>& out = draw.primitives;
  // ------------------------------------------------ map: overlap rule (general)
  // The map is drawn only when its square does not intersect the live readout
  // block or either roster. This is not a 320x240 special case: the same test
  // runs at every size, and a small window fails it because a 177px square at
  // the bottom-right corner cannot clear rosters that start at y=96. When it
  // fails the map is not drawn AT ALL (no half state): panelPrimitives stays 0
  // and the reason is 'no-room'. The readout rect is the caller's (0,0,0,0
  // means it did not say, and then the header takes no part in the test).
  const int headerX = input.readoutX;
  const int headerY = input.readoutY;
  const int headerW = input.readoutW;
  const int headerH = input.readoutH;
  const bool mapVisible = panel.valid
      && !rectsOverlap(panel.x, panel.y, panel.side, panel.side, headerX, headerY, headerW, headerH)
      && !rectsOverlap(panel.x, panel.y, panel.side, panel.side, redRect.x, redRect.y, redRect.w, redRect.h)
      && !rectsOverlap(panel.x, panel.y, panel.side, panel.side, bluRect.x, bluRect.y, bluRect.w, bluRect.h);
  const std::string dropReason = panel.valid ? std::string("no-room") : panel.reason;
  draw.panelPrimitives = 0;
  // ------------------------------------------------------------- map area
  if (mapVisible) {
    draw.panelPrimitives = 2;
    appendRect(out, static_cast<float>(panel.x), static_cast<float>(panel.y),
      static_cast<float>(panel.side), static_cast<float>(panel.side), kFrame);
    appendRect(out, static_cast<float>(panel.x + kBorder), static_cast<float>(panel.y + kBorder),
      static_cast<float>(panel.side - 2 * kBorder),
      static_cast<float>(panel.side - 2 * kBorder), kImageBg);
    // The basemap view is the only source of the image AND of the mapping, so
    // 'the picture' and 'where the players are' can never disagree: both come
    // out of the same boundary extents (map_basemap.h:12-22, mapview.js).
    const BasemapView view = basemapLoad(input.mapName, renderer);
    draw.basemapReady = view.ready;
    draw.basemapReason = view.reason;
    draw.basemapSource = view.sourceKey;
    draw.basemapPath = view.imagePath;
    draw.basemapFormat = view.format;
    draw.basemapRule = view.resolveRule;
    draw.basemapExact = view.exact;
    draw.basemapW = view.imageWidth;
    draw.basemapH = view.imageHeight;
    draw.basemapAspectDeviation = view.aspectDeviationPercent;
    draw.basemapSampler = view.sampler;
    draw.basemapMips = view.mipLevels;
    draw.basemapDecodeMs = view.decodeMs;
    draw.basemapUploadMs = view.uploadMs;
    if (view.bounds.valid) {
      const double worldW = view.bounds.maxX - view.bounds.minX;
      const double worldH = view.bounds.maxY - view.bounds.minY;
      const double areaSide = static_cast<double>(panel.mapSide);
      draw.boundsMinX = static_cast<float>(view.bounds.minX);
      draw.boundsMinY = static_cast<float>(view.bounds.minY);
      draw.boundsMaxX = static_cast<float>(view.bounds.maxX);
      draw.boundsMaxY = static_cast<float>(view.bounds.maxY);
      if (worldW > 0.0 && worldH > 0.0 && areaSide > 0.0) {
        // Two rectangles, on purpose:
        //   the BOUNDARY rect (imageX/Y/W/H) comes from the world extents, so its
        //     aspect equals the world's by construction -- the gate asserts it;
        //   the IMAGE quad (textureX/Y/W/H) contains the PNG by the PNG's OWN
        //     aspect inside that rect, so a package image whose aspect disagrees
        //     with its boundary (<=1.1% on the shipped set) is letterboxed by a
        //     pixel or two instead of being stretched.
        const double fit = std::min(areaSide / worldW, areaSide / worldH);
        draw.mappingValid = true;
        draw.mappingReason = "ok";
        draw.scale = static_cast<float>(fit);
        draw.imageW = static_cast<int>(std::lround(worldW * fit));
        draw.imageH = static_cast<int>(std::lround(worldH * fit));
        draw.imageX = panel.mapX + (panel.mapSide - draw.imageW) / 2;
        draw.imageY = panel.mapY + (panel.mapSide - draw.imageH) / 2;
        // Published so a reader (or a gate) can recompute: px = x*scale+offsetX,
        // py = offsetY - y*scale.
        draw.offsetX = static_cast<float>(draw.imageX - view.bounds.minX * fit);
        draw.offsetY = static_cast<float>(draw.imageY + view.bounds.maxY * fit);
        draw.textureX = draw.imageX;
        draw.textureY = draw.imageY;
        draw.textureW = draw.imageW;
        draw.textureH = draw.imageH;
        if (view.imageWidth > 0 && view.imageHeight > 0 && draw.imageW > 0 && draw.imageH > 0) {
          const double imageAspect = static_cast<double>(view.imageWidth)
              / static_cast<double>(view.imageHeight);
          const double rectAspect = static_cast<double>(draw.imageW)
              / static_cast<double>(draw.imageH);
          if (imageAspect > rectAspect) {
            draw.textureH = static_cast<int>(std::lround(draw.imageW / imageAspect));
          } else if (imageAspect < rectAspect) {
            draw.textureW = static_cast<int>(std::lround(draw.imageH * imageAspect));
          }
          draw.textureX = draw.imageX + (draw.imageW - draw.textureW) / 2;
          draw.textureY = draw.imageY + (draw.imageH - draw.textureH) / 2;
        }
        if (view.ready && renderer) {
          Renderer::HudPrimitive image;
          image.kind = Renderer::HudPrimitiveKind::Image;
          image.x = static_cast<float>(draw.textureX);
          image.y = static_cast<float>(draw.textureY);
          image.w = static_cast<float>(draw.textureW);
          image.h = static_cast<float>(draw.textureH);
          image.colour[0] = 1.0f;
          image.colour[1] = 1.0f;
          image.colour[2] = 1.0f;
          image.colour[3] = 1.0f;
          image.uv[0] = 0.0f;
          image.uv[1] = 0.0f;
          image.uv[2] = 1.0f;
          image.uv[3] = 1.0f;
          image.filled = true;
          image.scale = 1.0f;
          out.push_back(std::move(image));
          ++draw.imagePrimitives;
        } else if (!view.ready) {
          // No art is not the same answer as no map: say which one happened.
          appendText(out, static_cast<float>(panel.mapX + 8),
            static_cast<float>(panel.mapY + 8), "NO BASEMAP", kWarn);
          appendText(out, static_cast<float>(panel.mapX + 8),
            static_cast<float>(panel.mapY + 20), view.reason, kMuted);
        }
        if (view.ready && !view.exact) {
          // A family basemap on a versioned map: a human must be able to see
          // that the rectangle is a best fit, not a surveyed layout. The corner
          // text is "LEGACY MAP" (the gate greps for it in the pixel probe and
          // in this trace flag), drawn on a scrim so it survives a light map.
          draw.basemapLegacyBadge = true;
          appendRect(out, static_cast<float>(draw.imageX), static_cast<float>(draw.imageY),
            static_cast<float>(10 * 8 + 8), static_cast<float>(kBadgeH), kScrim, 0.85f);
          appendText(out, static_cast<float>(draw.imageX + 4),
            static_cast<float>(draw.imageY + 1), "LEGACY MAP", kWarn);
        }
        // ------------------------------------------------------- player icons
        for (const OverviewPlayer* player : visible) {
          if (!player->alive) { ++draw.iconsSkipped; ++draw.iconsDead; continue; }
          if (!player->positionKnown) { ++draw.iconsSkipped; ++draw.iconsNoPosition; continue; }
          const double px = static_cast<double>(player->x) * fit + draw.offsetX;
          const double py = draw.offsetY - static_cast<double>(player->y) * fit;
          const double localX = px - static_cast<double>(panel.mapX);
          const double localY = py - static_cast<double>(panel.mapY);
          if (localX < 0.0 || localY < 0.0 || localX > areaSide || localY > areaSide) {
            ++draw.iconsSkipped;
            ++draw.iconsOffMap;
            continue;
          }
          const int centreX = panel.mapX + static_cast<int>(localX + 0.5);
          const int centreY = panel.mapY + static_cast<int>(localY + 0.5);
          const Rgb& team = player->team == 2 ? kTeamRed : kTeamBlu;
          appendRect(out, static_cast<float>(centreX - kIcon / 2),
            static_cast<float>(centreY - kIcon / 2),
            static_cast<float>(kIcon), static_cast<float>(kIcon), team);
          resolveClassIcon(player->cls);
          const ClassIcon& icon = g_classIcons[static_cast<std::size_t>(player->cls)];
          if (icon.resolved) {
            ++draw.classIconsResolved;
            for (const MosaicCell& cell : icon.cells) {
              const int cellSize = kIcon / kIconGrid;
              Rgb colour;
              colour.r = static_cast<float>((cell.rgb >> 16) & 0xFFu) / 255.0f;
              colour.g = static_cast<float>((cell.rgb >> 8) & 0xFFu) / 255.0f;
              colour.b = static_cast<float>(cell.rgb & 0xFFu) / 255.0f;
              appendRect(out, static_cast<float>(centreX - kIcon / 2 + cell.x * cellSize),
                static_cast<float>(centreY - kIcon / 2 + cell.y * cellSize),
                static_cast<float>(cellSize * cell.w), static_cast<float>(cellSize * cell.h),
                colour);
            }
          } else {
            // Documented fallback: a team-coloured chip carrying the class's two
            // letter code, so the marker is still a class marker without art.
            ++draw.chipsUsed;
            const ClassRow* row = classRow(player->cls);
            if (row) {
              appendText(out, static_cast<float>(centreX - kIcon / 2),
                static_cast<float>(centreY - 4), row->letter, kText);
            }
          }
          if (player->yawKnown) {
            const double radians = static_cast<double>(player->yawDegrees) * 3.14159265358979 / 180.0;
            const float dx = static_cast<float>(std::cos(radians) * 12.0);
            const float dy = static_cast<float>(-std::sin(radians) * 12.0);
            appendRect(out, static_cast<float>(centreX) + dx - 2.0f,
              static_cast<float>(centreY) + dy - 2.0f, 4.0f, 4.0f, kText);
            ++draw.arrowsDrawn;
          }
          ++draw.iconsDrawn;
        }
      } else {
        draw.mappingReason = "degenerate-bounds";
      }
    } else {
      draw.mappingReason = view.reason;
    }
    if (!view.ready) {
      appendText(out, static_cast<float>(panel.x + 8), static_cast<float>(panel.y + panel.side - 18),
        asciiProject(view.reason, 28), kMuted);
    }
  } else {
    draw.mappingReason = dropReason;
    draw.basemapReason = dropReason;
  }
  // ---------------------------------------------------------- team panels
  const auto drawTeam = [&](const TeamRect& rect, std::int32_t team,
                            const std::vector<const OverviewPlayer*>& rows) {
    if (rect.w <= 0 || rect.h <= 0) return;
    const Rgb& teamColour = team == 2 ? kTeamRed : kTeamBlu;
    appendRect(out, static_cast<float>(rect.x), static_cast<float>(rect.y),
      static_cast<float>(rect.w), static_cast<float>(rect.h), kPanelBg);
    appendRect(out, static_cast<float>(rect.x), static_cast<float>(rect.y),
      static_cast<float>(rect.w), static_cast<float>(kTeamHeader), teamColour, 0.55f);
    appendText(out, static_cast<float>(rect.x + 6),
      static_cast<float>(rect.y + (kTeamHeader - 8) / 2),
      team == 2 ? "RED" : "BLU", kText);
    appendText(out, static_cast<float>(rect.x + rect.w - 6 - 8 * 6),
      static_cast<float>(rect.y + (kTeamHeader - 8) / 2),
      "PLAYER", kMuted);
    const int drawn = std::min(rect.rows, static_cast<int>(rows.size()));
    for (int slot = 0; slot < drawn; ++slot) {
      const OverviewPlayer& player = *rows[static_cast<std::size_t>(slot)];
      const int rowY = rect.y + kTeamHeader + slot * kTeamRow;
      RowView view;
      view.entity = player.entity;
      view.cls = player.cls;
      view.team = player.team;
      view.name = player.name;
      view.nameKnown = player.nameKnown;
      view.health = player.health;
      view.maxHealth = player.maxHealth;
      view.overheal = std::max(0, player.health - player.maxHealth);
      view.alive = player.alive;
      view.slot = slot;
      if (!player.alive && player.deathKnown) {
        const double seconds = static_cast<double>(input.serverTick - player.deathServerTick) / input.tickRate;
        view.deadSeconds = seconds >= 0.0 ? static_cast<int>(seconds + 0.5) : 0;
      }
      const Rgb& chip = view.alive ? teamColour : kDead;
      appendRect(out, static_cast<float>(rect.x + 6), static_cast<float>(rowY + 7),
        static_cast<float>(kIcon), static_cast<float>(kIcon), chip);
      const ClassRow* classInfo = classRow(player.cls);
      const std::string label = view.nameKnown ? view.name
          : (classInfo ? std::string(classInfo->name) : std::string("?"));
      appendText(out, static_cast<float>(rect.x + 28), static_cast<float>(rowY + 3),
        label, view.alive ? kText : kMuted);
      const std::string classText = classInfo ? classInfo->name : "?";
      appendText(out, static_cast<float>(rect.x + 28), static_cast<float>(rowY + 13),
        classText, kMuted);
      if (!view.alive) {
        ++draw.deadRows;
        std::string countdown = "DEAD";
        if (view.deadSeconds >= 0) countdown += " " + numberText(view.deadSeconds) + "s";
        appendText(out, static_cast<float>(rect.x + rect.w - 8
            - static_cast<int>(countdown.size()) * 8),
          static_cast<float>(rowY + 4), countdown, kWarn);
        if (classInfo) {
          appendText(out, static_cast<float>(rect.x + 8), static_cast<float>(rowY + 18),
            classInfo->letter, kDead);
        }
      } else {
        const std::string hp = numberText(view.health);
        appendText(out, static_cast<float>(rect.x + rect.w - 8 - static_cast<int>(hp.size()) * 8),
          static_cast<float>(rowY + 4), hp, kText);
        if (view.overheal > 0) {
          const std::string over = "+" + numberText(view.overheal);
          appendText(out, static_cast<float>(rect.x + rect.w - 12
              - static_cast<int>(hp.size()) * 8 - static_cast<int>(over.size()) * 8),
            static_cast<float>(rowY + 4), over, kOverheal);
        }
      }
      // The bar: full width under the two text lines. Overheal keeps the bar at
      // full and only the green number says by how much -- a bar longer than its
      // own background would read as a second, wrong denominator.
      const int barW = rect.w - 36;
      const int barY = rowY + 23;
      appendRect(out, static_cast<float>(rect.x + 28), static_cast<float>(barY),
        static_cast<float>(barW), 4.0f, kBarBg);
      if (view.alive && view.maxHealth > 0) {
        double fraction = static_cast<double>(view.health) / static_cast<double>(view.maxHealth);
        if (fraction < 0.0) fraction = 0.0;
        if (fraction > 1.0) fraction = 1.0;
        appendRect(out, static_cast<float>(rect.x + 28), static_cast<float>(barY),
          static_cast<float>(static_cast<int>(barW * fraction)), 4.0f, teamColour);
      }
    }
    for (int slot = drawn; slot < static_cast<int>(rows.size()); ++slot) ++draw.rowsSkipped;
  };
  drawTeam(redRect, 2, red);
  drawTeam(bluRect, 3, blu);
  // --------------------------------------------------------------- counts
  draw.rectPrimitives = 0;
  draw.textPrimitives = 0;
  draw.circlePrimitives = 0;
  for (const Renderer::HudPrimitive& primitive : out) {
    switch (primitive.kind) {
      case Renderer::HudPrimitiveKind::Rect: ++draw.rectPrimitives; break;
      case Renderer::HudPrimitiveKind::Text: ++draw.textPrimitives; break;
      case Renderer::HudPrimitiveKind::Circle: ++draw.circlePrimitives; break;
      // Image is counted separately (draw.imagePrimitives) because the gate
      // checks 'the basemap is exactly one texture quad', not a rect tally.
      case Renderer::HudPrimitiveKind::Image: break;
    }
  }
  draw.ready = draw.primitives.size() > 0;
  if (draw.reason == "no-client") {
  } else if (!panel.valid) {
  } else if (!mapVisible) {
    draw.reason = "no-room";
  } else {
    draw.reason = "ok";
  }
  // Region: what this compose actually painted on screen. With the map drawn it
  // is the panel square; with the map dropped it is the union of the two roster
  // panels, so a capture can still be cropped to the HUD without knowing this
  // file's constants. (The map gate reads its own rectangles from the trace.)
  if (mapVisible) {
    draw.regionX = panel.x;
    draw.regionY = panel.y;
    draw.regionW = panel.side;
    draw.regionH = panel.side;
  } else {
    const int left = std::min(redRect.x, bluRect.x);
    const int right = std::max(redRect.x + redRect.w, bluRect.x + bluRect.w);
    draw.regionX = left;
    draw.regionY = redRect.y;
    draw.regionW = right - left;
    draw.regionH = std::max(redRect.h, bluRect.h);
  }
  // ------------------------------------------------------------- signature
  draw.signature = "overview";
  for (int value : {panel.x, panel.y, panel.side, redRect.x, redRect.y, redRect.w,
        redRect.rows, bluRect.x, bluRect.y, bluRect.w, bluRect.rows,
        draw.panelPrimitives, input.readoutX, input.readoutY, input.readoutW,
        draw.imageX, draw.imageY, draw.imageW,
        draw.imageH, draw.imagePrimitives, static_cast<int>(out.size())}) {
    draw.signature += "," + std::to_string(value);
  }
  draw.signature += "," + input.mapName;
  for (const OverviewPlayer& player : input.players) {
    draw.signature += ";" + std::to_string(player.entity) + ":" + std::to_string(player.team)
        + ":" + std::to_string(player.cls) + ":" + std::to_string(player.health)
        + ":" + (player.alive ? "1" : "0") + ":"
        + std::to_string(static_cast<int>(player.x)) + ":"
        + std::to_string(static_cast<int>(player.y));
  }
  return draw;
}

// ---------------------------------------------------------------- trace
void mapOverviewTrace(const OverviewDraw& draw) {
  if (!draw.open) {
    tf2::native::main_internal::traceFmt("overview: frame open=0 reason=%s", draw.reason.c_str());
    return;
  }
  // 'minimap:' lines are the basemap contract (map_basemap.h): source key, whether
  // it is the same layout as the demo's map name (exact=0 -> LEGACY badge), and
  // the rectangle + published affine. A gate recomputes the affine, it does not
  // trust these numbers.
  tf2::native::main_internal::traceFmt(
    "minimap: source=%s exact=%d rule=%s path=%s loaded=%d reason=%s w=%d h=%d"
    " format=%s sampler=%s mip=%u aspectDev=%.3f decodeMs=%.3f uploadMs=%.3f",
    draw.basemapSource.empty() ? "-" : draw.basemapSource.c_str(),
    draw.basemapExact ? 1 : 0,
    draw.basemapRule.c_str(),
    draw.basemapPath.empty() ? "-" : draw.basemapPath.c_str(),
    draw.basemapReady ? 1 : 0,
    draw.basemapReason.c_str(),
    draw.basemapW, draw.basemapH, draw.basemapFormat.c_str(),
    draw.basemapSampler.c_str(), draw.basemapMips,
    draw.basemapAspectDeviation, draw.basemapDecodeMs, draw.basemapUploadMs);
  tf2::native::main_internal::traceFmt(
    "minimap: rect=%d,%d,%d,%d texture=%d,%d,%d,%d bounds=%.1f,%.1f,%.1f,%.1f scale=%.6f"
    " offsetX=%.6f offsetY=%.6f legacy=%d",
    draw.imageX, draw.imageY, draw.imageW, draw.imageH,
    draw.textureX, draw.textureY, draw.textureW, draw.textureH,
    draw.boundsMinX, draw.boundsMinY, draw.boundsMaxX, draw.boundsMaxY,
    draw.scale, draw.offsetX, draw.offsetY, draw.basemapLegacyBadge ? 1 : 0);
  tf2::native::main_internal::traceFmt(
    "overview: frame open=1 ready=%d reason=%s map=%s panel=%d,%d,%d border=%d area=%d,%d,%d"
    " red=%d,%d,%d,%d blu=%d,%d,%d,%d rowsRed=%d rowsBlu=%d"
    " panelPrimitives=%d header=%d,%d,%d,%d",
    draw.ready ? 1 : 0, draw.reason.c_str(),
    draw.mapName.empty() ? "-" : draw.mapName.c_str(),
    draw.screenX, draw.screenY, draw.screenSide, draw.border,
    draw.mapX, draw.mapY, draw.mapSide,
    draw.redX, draw.redY, draw.redW, draw.redH,
    draw.bluX, draw.bluY, draw.bluW, draw.bluH,
    draw.rowsRed, draw.rowsBlu, draw.panelPrimitives,
    draw.readoutX, draw.readoutY, draw.readoutW, draw.readoutH);
  tf2::native::main_internal::traceFmt(
    "overview: mapping ok=%d reason=%s bounds=%.1f,%.1f,%.1f,%.1f scale=%.6f"
    " offsetX=%.6f offsetY=%.6f area=%d,%d,%d",
    draw.mappingValid ? 1 : 0, draw.mappingReason.c_str(),
    draw.boundsMinX, draw.boundsMinY, draw.boundsMaxX, draw.boundsMaxY,
    draw.scale, draw.offsetX, draw.offsetY, draw.mapX, draw.mapY, draw.mapSide);
  tf2::native::main_internal::traceFmt(
    "overview: summary players=%d shown=%d hidden=%d rowsRed=%d rowsBlu=%d skipped=%d"
    " icons=%d iconsSkipped=%d iconsDead=%d iconsNoPos=%d iconsOffMap=%d arrows=%d classIcons=%d chips=%d deadRows=%d"
    " images=%d rects=%zu text=%zu circles=%zu hiddenEntity=%d",
    draw.playersTotal, draw.playersShown, draw.playersHidden, draw.rowsRed, draw.rowsBlu,
    draw.rowsSkipped, draw.iconsDrawn, draw.iconsSkipped, draw.iconsDead,
    draw.iconsNoPosition, draw.iconsOffMap, draw.arrowsDrawn,
    draw.classIconsResolved, draw.chipsUsed, draw.deadRows,
    draw.imagePrimitives, draw.rectPrimitives, draw.textPrimitives,
    draw.circlePrimitives, g_hiddenEntity);
}

// ---------------------------------------------------------------- class chip
// The decode itself is resolveClassIcon's (:211-305); this only copies its cells
// out in a type a header can name. No chain is built here that the overview would
// not have built anyway: the same g_classIcons cache answers both screens.
bool mapOverviewClassChip(std::int32_t cls, std::vector<OverviewClassChip>& out) {
  out.clear();
  if (cls < 1 || cls > 9) return false;
  resolveClassIcon(cls);
  const ClassIcon& icon = g_classIcons[static_cast<std::size_t>(cls)];
  if (!icon.resolved) return false;
  out.reserve(icon.cells.size());
  for (const MosaicCell& cell : icon.cells) {
    OverviewClassChip chip;
    chip.x = cell.x;
    chip.y = cell.y;
    chip.w = cell.w;
    chip.h = cell.h;
    chip.rgb = cell.rgb;
    out.push_back(chip);
  }
  return !out.empty();
}

const char* mapOverviewClassChipReason(std::int32_t cls) {
  if (cls < 1 || cls > 9) return "no-such-class";
  return g_classIcons[static_cast<std::size_t>(cls)].reason.c_str();
}

// ---------------------------------------------------------------- state
bool mapOverviewOpen() { return g_open; }
void mapOverviewSetOpen(bool open) { g_open = open; }
void mapOverviewToggle() { g_open = !g_open; }
void mapOverviewSetImageOverride(const std::string& path) { basemapSetImageOverride(path); }
const std::string& mapOverviewImageOverride() { return basemapImageOverride(); }
void mapOverviewSetTfRoot(const std::string& root) {
  if (g_tfRoot == root) return;
  g_tfRoot = root;
  // A different install is a different archive set, must be re-resolved rather
  // than silently served from the previous install's decoded icons.
  g_chain.reset();
  g_chainReason = "not-built";
  for (ClassIcon& icon : g_classIcons) icon = ClassIcon{};
}
const std::string& mapOverviewTfRoot() { return g_tfRoot; }
void mapOverviewSetHiddenEntity(std::int32_t entity) { g_hiddenEntity = entity; }
std::int32_t mapOverviewHiddenEntity() { return g_hiddenEntity; }

}  // namespace tf2::native
