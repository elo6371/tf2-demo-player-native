#pragma once

// E11 (2026-11-16, rebased 2026-11-17): the map-overview screen -- a large,
// toggleable map view with the demos.tf basemap, class-portrait player markers
// and the two team HP panels.
//
// WHY THERE IS A TEXTURE NOW
//   Until 2026-11-17 the HUD pass bound exactly one SRV (the R8 font atlas), so
//   the base image travelled as a mosaic of filled Rects. That is gone: the HUD
//   shader has a fifth path ('kind 4', native_renderer_shaders.cpp hudPixelSource)
//   and native_renderer.h exposes a second SRV slot (t1/s1) plus
//   Renderer::setHudImageTexture. The basemap is therefore one Image primitive
//   with a real texture: original-resolution art instead of a 4 px mosaic.
//
// WHAT THIS MODULE OWNS, AND WHAT IT DELIBERATELY DOES NOT
//   It owns the screen geometry, the basemap view fields, the class icon chain and
//   the row/HP composition. It does NOT own the world->image arithmetic: that is
//   demos.tf's mapview.js formula, implemented once in map_basemap.h (:12-22) and
//   re-exported here as the published scale/offset. The container rectangle is
//   fitted to the BOUNDARY extents (not the image's own aspect), so
//   'rectangle aspect == world aspect' is true by construction; the image is
//   scaled into that rectangle and map_basemap reports the resulting deviation.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <Windows.h>

#include "demo_header.h"
#include "native_renderer.h"

namespace tf2::native {

// ---------------------------------------------------------------- run state
// The switch is process-wide state because the toggle arrives from two places
// that do not share a call stack: the hotkey (WM_KEYDOWN in main.cpp) and the
// headless switch every gate uses (--overview-open).
bool mapOverviewOpen();
void mapOverviewSetOpen(bool open);
void mapOverviewToggle();

// --overview-image <path>: one explicit file override, forwarded to
// basemapSetImageOverride. With no override the image is <basemapRoot>/images/
// <resolved key>.png. A map or file that cannot be found is not an error: the
// screen draws the map area empty and publishes the reason (no-basemap /
// no-boundary / no-image / decode-failed:<token>), never a guessed rectangle.
void mapOverviewSetImageOverride(const std::string& path);
const std::string& mapOverviewImageOverride();

// The TF2 install root, needed to resolve VGUI class-icon materials. Set once
// where main.cpp already resolved the asset root; empty means 'no icon chain',
// which is reported as classicons=none rather than as a drawn guess.
void mapOverviewSetTfRoot(const std::string& root);
const std::string& mapOverviewTfRoot();

// TEST HOOK (--overview-hide <entity>): removes one player from the screen.
// It exists so map-overview-check.sh can prove that 'a removed player removes
// exactly one icon and exactly one row' instead of asserting a count twice.
void mapOverviewSetHiddenEntity(std::int32_t entity);
std::int32_t mapOverviewHiddenEntity();

// ---------------------------------------------------------------- readings
// One player row as the screen draws it. Built from EntityState by
// mapOverviewBuildInput, so the entity/property reading lives in exactly one
// place (the same findNamed/readIntProperty idiom playback_hud.cpp:11-53 uses).
struct OverviewPlayer {
  std::int32_t entity = -1;
  std::int32_t cls = 0;            // 1..9, TF_CLASS_* order
  std::int32_t team = 0;           // 2 RED, 3 BLU
  std::string name;                // userinfo table text, ASCII-projected
  bool nameKnown = false;
  std::int32_t health = 0;
  std::int32_t maxHealth = 0;      // class table, never read from the demo
  bool healthKnown = false;
  bool alive = true;               // m_lifeState/m_iHealth, not tick arithmetic
  bool positionKnown = false;
  float x = 0.0f;
  float y = 0.0f;
  bool yawKnown = false;
  float yawDegrees = 0.0f;
  bool deathKnown = false;         // authoritative player_death tick exists
  std::int32_t deathServerTick = -1;
};

struct OverviewInput {
  std::string mapName;
  int clientWidth = 0;
  int clientHeight = 0;
  std::int32_t tick = 0;           // playback tick (demo ticks after the first entry)
  std::int32_t serverTick = 0;     // firstDataEntryTick + tick: the clock game events are stamped on
  double tickRate = 66.6666667;
  std::size_t snapshotEntities = 0;
  std::vector<OverviewPlayer> players;
  // The live readout's block, in client pixels (main_hud.cpp publishes lines 1-2
  // centred and line 3 left-anchored). Compose needs it for one decision only:
  // a bottom-right map that would sit on the readout is NOT drawn (reason
  // "no-room") rather than drawn over a reading. Zero width/height means "the
  // caller did not say", and then the readout is not part of the collision test.
  int readoutX = 0;
  int readoutY = 0;
  int readoutW = 0;
  int readoutH = 0;
};

// Builds the rows from the decoder's own state. Kept beside compose because the
// HUD receives states+summary and nothing else; a caller that had to rebuild the
// entity loop would be a second reader of the same properties.
OverviewInput mapOverviewBuildInput(const std::vector<EntityState>& states,
    const DemoNetworkSummary& summary, const std::string& mapName,
    int clientWidth, int clientHeight, std::int32_t tick, double tickRate);

// ---------------------------------------------------------------- the screen
struct OverviewDraw {
  bool open = false;
  bool ready = false;
  std::string reason = "closed";
  std::vector<Renderer::HudPrimitive> primitives;
  std::string signature;
  // The demo's map name this frame was composed for, as the trace prints it: a
  // reader should not have to infer which map a panel was built from.
  std::string mapName;
  // screen rect, the near-full-screen square this feature owns
  int screenX = 0, screenY = 0, screenSide = 0, border = 0;
  // the map area inside it (square; the base image is stretched to exactly this)
  int mapX = 0, mapY = 0, mapSide = 0;
  // the two team panels
  int panelW = 0, panelHeader = 0, panelRow = 0;
  // the live readout block this frame was composed against (input.readout*), so
  // the trace can publish the rectangle the map had to clear.
  int readoutX = 0, readoutY = 0, readoutW = 0, readoutH = 0;
  int redX = 0, redY = 0, redW = 0, redH = 0;
  int bluX = 0, bluY = 0, bluW = 0, bluH = 0;
  // 2 when the panel frame + map background were appended, 0 when the panel was
  // dropped for want of room: the gate calls that "map primitives count == 0".
  int panelPrimitives = 0;
  // basemap (map_basemap.h) and the rectangle the image occupies inside the area.
  // imageX/Y/W/H is the CONTAIN rect: boundary-aspect, centred in the square area,
  // and the pixels are fitted into it (mapview.js formula, see map_basemap.h).
  bool basemapReady = false;
  std::string basemapReason = "not-loaded";
  std::string basemapSource;              // package key, e.g. cp_process_final
  std::string basemapPath;
  std::string basemapFormat = "none";     // GPU format token from the renderer
  std::string basemapRule = "none";       // which resolve step matched
  bool basemapExact = false;              // false -> LEGACY badge on screen
  bool basemapLegacyBadge = false;
  int basemapW = 0, basemapH = 0;
  double basemapAspectDeviation = 0.0;
  // imageX/Y/W/H is the BOUNDARY rect (aspect == world aspect by construction).
  // textureX/Y/W/H is where the PNG is actually drawn: contain-fitted by the
  // IMAGE's own aspect inside that rect, so the picture is never stretched.
  int imageX = 0, imageY = 0, imageW = 0, imageH = 0;
  int textureX = 0, textureY = 0, textureW = 0, textureH = 0;
  int imagePrimitives = 0;
  std::string basemapSampler = "none";
  unsigned basemapMips = 0;
  double basemapDecodeMs = 0.0;
  double basemapUploadMs = 0.0;
  // mapping: published exactly as the basemap formula, so a gate can recompute
  // px = x * scale + offsetX, py = offsetY - y * scale.
  bool mappingValid = false;
  std::string mappingReason = "no-basemap";
  float boundsMinX = 0.0f, boundsMinY = 0.0f, boundsMaxX = 0.0f, boundsMaxY = 0.0f;
  float scale = 0.0f, offsetX = 0.0f, offsetY = 0.0f;
  // counts a gate can compare instead of counting pixels twice
  int playersTotal = 0, playersShown = 0, playersHidden = 0;
  int rowsRed = 0, rowsBlu = 0, rowsSkipped = 0;
  int iconsDrawn = 0, iconsSkipped = 0, arrowsDrawn = 0;
  // iconsSkipped is the total; these three say WHY a marker is absent, so a
  // roster full of players with an empty map cannot be read as 'the map is fine'.
  int iconsDead = 0, iconsNoPosition = 0, iconsOffMap = 0;
  int classIconsResolved = 0, chipsUsed = 0;
  int deadRows = 0;
  std::size_t rectPrimitives = 0, textPrimitives = 0, circlePrimitives = 0;
  int regionX = 0, regionY = 0, regionW = 0, regionH = 0;
};

// Composes the frame. window is only read for GetClientRect, exactly as
// hudMinimapPanelRect (native/src/main_hud.cpp:978) does, so the caller does not
// pass a size the window could disagree with. renderer is where the basemap
// texture is uploaded (map_basemap.cpp calls setHudImageTexture); null is legal
// and means 'compose without a texture' -- the Image primitive is then omitted
// and basemapReady=false, which is what the pure probes want.
OverviewDraw mapOverviewCompose(HWND window, const OverviewInput& input,
    Renderer* renderer);

// One line per publish plus one line per player and per row. The numbers here are
// what map-overview-check.sh recomputes from world coordinates; a gate that read
// pixels alone could not distinguish 'wrong mapping' from 'no players'.
void mapOverviewTrace(const OverviewDraw& draw);

// H1 (2026-11-23): the broadcast HUD draws the SAME class chip this screen
// decodes, so this hands over the cell runs instead of letting a second module
// open the VPK chain and decode the same VTF again. False means this install has
// no icon for the class (mapOverviewClassChipReason says why), and the caller
// draws its own letter instead.
struct OverviewClassChip {
  int x = 0;
  int y = 0;
  int w = 1;
  int h = 1;
  std::uint32_t rgb = 0;
};
bool mapOverviewClassChip(std::int32_t cls, std::vector<OverviewClassChip>& out);
const char* mapOverviewClassChipReason(std::int32_t cls);

}  // namespace tf2::native
