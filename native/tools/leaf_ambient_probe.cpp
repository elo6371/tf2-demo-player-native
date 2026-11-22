// leaf_ambient_probe -- T105/task-37: the BSP leaf ambient cube, measured
// without a GPU, on fixtures, on a map, on a map corpus and on a real demo.
//
// WHAT IT ANSWERS
//   Task-37 half A parses LUMP_LEAF_AMBIENT_INDEX (52, HDR 51) and
//   LUMP_LEAF_AMBIENT_LIGHTING (56, HDR 55) and exposes the six-direction
//   ambient cube for a world point. Three questions have to be separable
//   before any renderer consumes it:
//     1. did the lumps parse into the layout every shipped map carries
//        (a PER-LEAF index of (count, first) then 28-byte sample records)?
//     2. when a point is asked about, did the lookup HIT or did it fall back?
//     3. what do the six decoded colours actually look like -- numbers, not
//        adjectives -- and are they the right order of magnitude?
//   A zero cube answers none of them: it reads as "this model is in the dark"
//   when the truth is "this leaf carries no sample", which is 42.7% of the
//   leaves in this host's 261-map corpus. The probe therefore reports the
//   hit/miss counters on every path, and the fixtures assert that a miss
//   leaves the caller's cube untouched rather than zeroed.
//
// Usage:
//   leaf_ambient_probe --self-test
//   leaf_ambient_probe --map <map.bsp> [--leaves N]
//   leaf_ambient_probe --corpus <maps dir>
//   leaf_ambient_probe --demo <demo.dem> [--tf-root <dir>] [--ticks a,b,c]
//       [--entities N]
// Exit: 0 = every check passed, 1 = a check failed or an input did not parse,
//       2 = bad arguments.
//
// The probe touches no GPU and writes no file. Whether a cube brightens a
// pixel is a capture's question; what this proves is that the cube exists,
// where it came from, and that the reader can say when it does not.

#include "asset_root.h"
#include "bsp_map.h"
#include "demo_header.h"
#include "entity_model.h"
#include "leaf_ambient_table.h"
#include "map_lookup.h"
#include "vpk_archive.h"
#include "world_material.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <system_error>
#include <unordered_map>
#include <vector>

namespace {

using tf2::native::BspFixture;
using tf2::native::BspFixtureFace;
using tf2::native::BspLeafAmbientCube;
using tf2::native::BspLeafAmbientMode;
using tf2::native::BspMap;
using tf2::native::BspParser;

int failures = 0;

void emit(const std::string& key, const std::string& value) {
  std::cout << "{\"reading\":\"" << key << "\",\"value\":\"" << value << "\"}\n";
}
void emitNumber(const std::string& key, long long value) {
  emit(key, std::to_string(value));
}

void check(const char* name, bool ok, const std::string& detail) {
  std::cout << "{\"check\":\"" << name << "\",\"ok\":" << (ok ? "true" : "false")
            << ",\"detail\":\"" << detail << "\"}\n";
  if (!ok) ++failures;
}

std::string fmt(float value) {
  char buffer[32];
  std::snprintf(buffer, sizeof(buffer), "%.6g", static_cast<double>(value));
  return std::string(buffer);
}

bool near(float actual, float expected, float tolerance) {
  return std::fabs(actual - expected) <= tolerance;
}

// The transform the renderer applies before a cube reaches the slot table:
// decoded radiance times kLeafAmbientScale. One function, so the table checks
// cannot compare a different number than the one that gets uploaded.
tf2::native::LeafAmbientCubeFace rendererFaceOf(const BspLeafAmbientCube& cube) {
  tf2::native::LeafAmbientCubeFace face;
  for (int direction = 0; direction < tf2::native::kLeafAmbientFaces; ++direction) {
    for (int channel = 0; channel < 3; ++channel) {
      face.rgb[direction][channel] =
        cube.color[direction][channel] * tf2::native::kLeafAmbientScale;
    }
  }
  return face;
}

// One cube as a comparable string, so a check can quote the numbers that
// failed instead of "cube mismatch".
std::string cubeText(const BspLeafAmbientCube& cube) {
  std::string text;
  for (int direction = 0; direction < 6 && cube.available; ++direction) {
    if (direction) text += "|";
    text += fmt(cube.color[direction][0]) + "," + fmt(cube.color[direction][1])
      + "," + fmt(cube.color[direction][2]);
  }
  return text;
}

std::vector<std::uint8_t> readFile(const std::filesystem::path& path) {
  std::vector<std::uint8_t> bytes;
  FILE* file = nullptr;
  if (_wfopen_s(&file, path.wstring().c_str(), L"rb") != 0 || file == nullptr) return bytes;
  std::fseek(file, 0, SEEK_END);
  const long size = std::ftell(file);
  std::fseek(file, 0, SEEK_SET);
  if (size > 0) {
    bytes.resize(static_cast<std::size_t>(size));
    if (std::fread(bytes.data(), 1, bytes.size(), file) != bytes.size()) bytes.clear();
  }
  std::fclose(file);
  return bytes;
}

// The quad every fixture needs: one face with a 2x2 luxel box, so a fixture
// parses through the same face path a real map does instead of exercising the
// ambient lumps under a parser that had to invent a world model.
BspFixtureFace fixtureQuad() {
  BspFixtureFace face;
  const float corners[4][3] = {{0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f},
    {1.0f, 1.0f, 0.0f}, {0.0f, 1.0f, 0.0f}};
  for (int i = 0; i < 4; ++i) {
    for (int axis = 0; axis < 3; ++axis) face.corners[i][axis] = corners[i][axis];
  }
  face.luAxis[0] = 1.0f;
  face.lvAxis[1] = 1.0f;
  face.samplesW = 1;
  face.samplesH = 1;
  return face;
}

// Six colours distinct per direction, so a reader that shuffled the slots
// cannot pass by decoding the same value six times. Held as int32 because the
// last direction carries the signed exponent -1: writing -1 into a uint8_t
// literal is a narrowing conversion, and this tree treats that warning as a
// build failure. The cast happens once, where the bytes are assembled.
const std::int32_t kDirectional[6][4] = {
  {255, 0, 0, 0}, {0, 255, 0, 0}, {0, 0, 255, 0},
  {255, 255, 255, 0}, {128, 64, 32, 0}, {64, 0, 0, -1},
};

void fillCube(std::uint8_t cube[6][4], std::uint8_t r, std::uint8_t g, std::uint8_t b,
    std::int8_t exponent) {
  for (int direction = 0; direction < 6; ++direction) {
    cube[direction][0] = r;
    cube[direction][1] = g;
    cube[direction][2] = b;
    cube[direction][3] = static_cast<std::uint8_t>(exponent);
  }
}

// A two-leaf BSP tree: the plane at x=0 puts child 0 (leaf 0) on the +x side
// and child 1 (leaf 1) on the -x side, which is the layout buildFixtureBsp
// writes and the layout leafAt() has to agree with.
BspFixture twoLeafFixture() {
  BspFixture fixture;
  fixture.faces.push_back(fixtureQuad());
  fixture.withTree = true;
  fixture.splitX = 0.0f;
  fixture.leaves.resize(2);
  // Leaf 0 is the +x half. Its box is 0..255 on every axis so a position byte
  // IS a world coordinate -- 255*d/255 = d -- which makes the expected numbers
  // below readable instead of "0.5019 times the box".
  fixture.leaves[0].cluster = 3;
  fixture.leaves[0].mins[0] = 0;
  fixture.leaves[0].mins[1] = 0;
  fixture.leaves[0].mins[2] = 0;
  fixture.leaves[0].maxs[0] = 255;
  fixture.leaves[0].maxs[1] = 255;
  fixture.leaves[0].maxs[2] = 255;
  fixture.leaves[1].cluster = 7;
  fixture.leaves[1].mins[0] = -255;
  fixture.leaves[1].mins[1] = 0;
  fixture.leaves[1].mins[2] = 0;
  fixture.leaves[1].maxs[0] = 0;
  fixture.leaves[1].maxs[1] = 255;
  fixture.leaves[1].maxs[2] = 255;
  return fixture;
}

// Three cubes for leaf 0 -- at its -x corner, its +x corner and its middle --
// and one for leaf 1. The middle cube is the one leafAmbientAtLeaf() must
// choose (it is nearest the box centre), while a query AT the -x corner must
// choose the corner cube: the two entry points disagree on purpose, and a
// fixture with one cube per leaf could not tell them apart.
void addThreePlusOneSamples(BspFixture& fixture, std::vector<std::uint8_t>& page,
    std::vector<std::uint8_t>& index) {
  std::uint8_t cube[6][4];
  fillCube(cube, 10, 20, 30, 0);
  tf2::native::appendAmbientSample(page, cube, 0, 0, 0);
  fillCube(cube, 40, 50, 60, 0);
  tf2::native::appendAmbientSample(page, cube, 255, 255, 255);
  for (int direction = 0; direction < 6; ++direction) {
    for (int channel = 0; channel < 4; ++channel) {
      cube[direction][channel] = static_cast<std::uint8_t>(kDirectional[direction][channel]);
    }
  }
  tf2::native::appendAmbientSample(page, cube, 127, 127, 127);
  fillCube(cube, 200, 100, 50, 0);
  tf2::native::appendAmbientSample(page, cube, 100, 100, 100);
  tf2::native::appendAmbientEntry(index, 3, 0);
  tf2::native::appendAmbientEntry(index, 1, 3);
  fixture.leafAmbientLighting = page;
  fixture.leafAmbientIndex = index;
}

// Rec. 709 luminance of one decoded cube corner. The six numbers in a cube are
// linear radiance per axis, not an 8-bit colour, so a plain mean of the three
// channels would let a blue corner outrank a green one of the same perceived
// brightness. Weighted the standard way so "which direction is brightest" is a
// question about light, not about channel order.
float cornerLuminance(const float rgb[3]) {
  return 0.2126f * rgb[0] + 0.7152f * rgb[1] + 0.0722f * rgb[2];
}

// Which corner of the cube carries the light, aggregated over every cube a page
// holds. This is the only evidence this tree has for the AXIS ORDER: the SDK
// header (mathlib/compressed_light_cube.h:17-21) declares six ColorRGBExp32 and
// never says which is which, so the order in bsp_map.h is an inference. If the
// inference is right, an indoor map lights its ceiling (index 4, +Z) more than
// its floor (index 5, -Z); if it is wrong, this histogram says so, because the
// two directions would trade places. Reported as a distribution rather than a
// mean so a map with one blazing corner cannot pass for a general trend.
struct DirectionTally {
  long long brightest[6] = {};
  double luminance[6] = {};
  long long cubes = 0;
};

void tallyDirections(const BspMap& map, DirectionTally& tally) {
  for (const auto& sample : map.leafAmbientSamples) {
    int best = 0;
    float bestLuminance = cornerLuminance(sample.color[0]);
    for (int direction = 0; direction < 6; ++direction) {
      const float luminance = cornerLuminance(sample.color[direction]);
      tally.luminance[direction] += static_cast<double>(luminance);
      if (luminance > bestLuminance) {
        bestLuminance = luminance;
        best = direction;
      }
    }
    ++tally.brightest[best];
    ++tally.cubes;
  }
}

void emitDirectionTally(const DirectionTally& tally, const std::string& name) {
  std::cout << "{\"reading\":\"directionStats\"";
  if (!name.empty()) std::cout << ",\"map\":\"" << name << "\"";
  std::cout << ",\"cubes\":" << tally.cubes << ",\"brightest\":[";
  for (int direction = 0; direction < 6; ++direction) {
    if (direction != 0) std::cout << ",";
    std::cout << tally.brightest[direction];
  }
  std::cout << "],\"meanLuminance\":[";
  for (int direction = 0; direction < 6; ++direction) {
    if (direction != 0) std::cout << ",";
    const double mean = tally.cubes == 0 ? 0.0
      : tally.luminance[direction] / static_cast<double>(tally.cubes);
    std::cout << fmt(static_cast<float>(mean));
  }
  std::cout << "]}\n";
}

// Every page as one line, so a corpus run can be aggregated by a caller and a
// failing map can be spotted without re-parsing it here.
void emitMapReadings(const BspMap& map, const std::string& name) {
  std::cout << "{\"map\":\"" << name << "\",\"mode\":\"" << BspParser::leafAmbientModeName(map.leafAmbientMode)
    << "\",\"leaves\":" << map.leaves.size()
    << ",\"entries\":" << map.leafAmbientEntries.size()
    << ",\"samples\":" << map.leafAmbientSamples.size()
    << ",\"leavesWith\":" << map.leavesWithAmbient
    << ",\"leavesWithout\":" << map.leavesWithoutAmbient
    << ",\"outOfRange\":" << map.leafAmbientRecordsOutOfRange
    << ",\"unreferenced\":" << map.leafAmbientSamplesUnreferenced
    << "}\n";
}

// ---------------------------------------------------------------------------
// Self-test: fixtures
// ---------------------------------------------------------------------------

bool parseFixture(BspFixture& fixture, BspMap& map) {
  return BspParser::parse(tf2::native::buildFixtureBsp(fixture), map, 200000, 4096);
}

int runSelfTest() {
  // ---- 1. the layout, on a two-leaf fixture -----------------------------
  {
    BspFixture fixture = twoLeafFixture();
    std::vector<std::uint8_t> page;
    std::vector<std::uint8_t> index;
    addThreePlusOneSamples(fixture, page, index);
    BspMap map;
    const bool parsed = parseFixture(fixture, map);
    check("fixture.parsed", parsed, parsed ? map.error : map.error);
    check("fixture.mode", map.leafAmbientMode == BspLeafAmbientMode::RgbExp32,
      BspParser::leafAmbientModeName(map.leafAmbientMode));
    // The index is a PER-LEAF array of 4-byte records: two leaves, eight bytes.
    check("fixture.entries", map.leafAmbientEntries.size() == 2,
      "entries=" + std::to_string(map.leafAmbientEntries.size()));
    check("fixture.samples", map.leafAmbientSamples.size() == 4,
      "samples=" + std::to_string(map.leafAmbientSamples.size()));
    check("fixture.leavesWith", map.leavesWithAmbient == 2 && map.leavesWithoutAmbient == 0,
      "with=" + std::to_string(map.leavesWithAmbient)
        + " without=" + std::to_string(map.leavesWithoutAmbient));
    // Every record is claimed and none overruns: 0 and 0 is the state all 260
    // shipped maps that carry the lumps are in, so a fixture that produced a
    // non-zero here would be testing a layout real maps do not have.
    check("fixture.noOutOfRange", map.leafAmbientRecordsOutOfRange == 0
      && map.leafAmbientSamplesUnreferenced == 0,
      "outOfRange=" + std::to_string(map.leafAmbientRecordsOutOfRange)
        + " unreferenced=" + std::to_string(map.leafAmbientSamplesUnreferenced));
  }

  // ---- 2. leafAt() is the leaf-index sibling of clusterAt() -------------
  {
    BspFixture fixture = twoLeafFixture();
    std::vector<std::uint8_t> page, index;
    addThreePlusOneSamples(fixture, page, index);
    BspMap map;
    const bool parsed = parseFixture(fixture, map);
    const int right = BspParser::leafAt(map, 5.0f, 5.0f, 5.0f);
    const int left = BspParser::leafAt(map, -5.0f, 5.0f, 5.0f);
    check("leafAt.right", parsed && right == 0, "leaf=" + std::to_string(right));
    check("leafAt.left", parsed && left == 1, "leaf=" + std::to_string(left));
    // The two walks must agree about which leaf a point lands in: the leaf
    // number is the cluster lookup for that same leaf, or the ambient cube
    // would be read for a leaf the PVS test was not asked about.
    const int clusterRight = BspParser::clusterAt(map, 5.0f, 5.0f, 5.0f);
    const int clusterLeft = BspParser::clusterAt(map, -5.0f, 5.0f, 5.0f);
    check("leafAt.agreesWithClusterAt",
      right >= 0 && static_cast<std::size_t>(right) < map.leaves.size()
        && map.leaves[static_cast<std::size_t>(right)].cluster == clusterRight
        && left >= 0 && static_cast<std::size_t>(left) < map.leaves.size()
        && map.leaves[static_cast<std::size_t>(left)].cluster == clusterLeft,
      "clusters=" + std::to_string(clusterRight) + "," + std::to_string(clusterLeft));
    check("leafAt.refuses", BspParser::leafAt(map, std::nanf(""), 0.0f, 0.0f) == -1,
      "nan must not resolve to a leaf");
  }

  // ---- 3. the point overload chooses the cube nearest the POINT ---------
  {
    BspFixture fixture = twoLeafFixture();
    std::vector<std::uint8_t> page, index;
    addThreePlusOneSamples(fixture, page, index);
    BspMap map;
    const bool parsed = parseFixture(fixture, map);
    BspLeafAmbientCube atLeaf;
    const bool leafOk = parsed && BspParser::leafAmbientAtLeaf(map, 0, atLeaf);
    check("cube.atLeaf", leafOk && atLeaf.available && atLeaf.leafIndex == 0
      && atLeaf.sampleCount == 3,
      "available=" + std::string(atLeaf.available ? "1" : "0")
        + " sampleCount=" + std::to_string(atLeaf.sampleCount));
    // The box centre is (127.5,127.5,127.5); of the three stored positions
    // (0,0,0) (255,255,255) (127,127,127) the last is nearest, and its first
    // direction is the 255/0/0 cube -> 1.0 linear.
    check("cube.atLeafPicksCentre", leafOk && atLeaf.sampleIndex == 2
      && near(atLeaf.color[0][0], 1.0f, 1e-6f) && near(atLeaf.color[0][1], 0.0f, 1e-6f)
      && near(atLeaf.color[2][2], 1.0f, 1e-6f) && near(atLeaf.color[5][0], 64.0f / 510.0f, 1e-6f),
      "sampleIndex=" + std::to_string(atLeaf.sampleIndex) + " cube=" + cubeText(atLeaf));
    BspLeafAmbientCube atCorner;
    const bool cornerOk = parsed && BspParser::leafAmbientAt(map, 1.0f, 1.0f, 1.0f, atCorner);
    // (1,1,1) is nearest the (0,0,0) sample, whose cube is 10/20/30 at exponent
    // 0 -> 10/255, 20/255, 30/255.
    check("cube.atCornerPicksCorner", cornerOk && atCorner.sampleIndex == 0
      && near(atCorner.color[0][0], 10.0f / 255.0f, 1e-6f)
      && near(atCorner.color[0][1], 20.0f / 255.0f, 1e-6f)
      && near(atCorner.color[0][2], 30.0f / 255.0f, 1e-6f),
      "sampleIndex=" + std::to_string(atCorner.sampleIndex) + " cube=" + cubeText(atCorner));
    // The chosen sample position is the stored fraction mapped through the
    // leaf box: box 0..255 turns the byte into the coordinate itself.
    check("cube.position", cornerOk && near(atCorner.position[0], 0.0f, 1e-4f)
      && near(atCorner.position[1], 0.0f, 1e-4f) && near(atCorner.position[2], 0.0f, 1e-4f),
      "position=" + fmt(atCorner.position[0]) + "," + fmt(atCorner.position[1])
        + "," + fmt(atCorner.position[2]));
    // The second leaf carries one cube whose first direction is 200/100/50.
    BspLeafAmbientCube other;
    const bool otherOk = parsed && BspParser::leafAmbientAtLeaf(map, 1, other);
    check("cube.secondLeaf", otherOk && other.cluster == 7
      && near(other.color[0][0], 200.0f / 255.0f, 1e-6f),
      "cluster=" + std::to_string(other.cluster) + " cube=" + cubeText(other));
  }

  // ---- 4. a miss must not hand back a zero cube -------------------------
  {
    BspFixture fixture = twoLeafFixture();
    std::vector<std::uint8_t> page, index;
    addThreePlusOneSamples(fixture, page, index);
    // Leaf 1 keeps a cube; leaf 0 is emptied so exactly one leaf answers.
    index[0] = 0;
    index[1] = 0;
    index[2] = 0;
    index[3] = 0;
    fixture.leafAmbientIndex = index;
    BspMap map;
    const bool parsed = parseFixture(fixture, map);
    check("empty.oneLeafWithoutCube", parsed && map.leavesWithAmbient == 1
      && map.leavesWithoutAmbient == 1,
      "with=" + std::to_string(map.leavesWithAmbient)
        + " without=" + std::to_string(map.leavesWithoutAmbient));
    BspLeafAmbientCube cube;
    cube.color[0][0] = 12345.0f;
    const bool hit = parsed && BspParser::leafAmbientAt(map, 5.0f, 5.0f, 5.0f, cube);
    check("empty.missReturnsFalse", !hit, hit ? "a cube was returned for an empty leaf" : "false");
    // The out parameter is contractually untouched: a caller that filled it
    // with a fallback keeps that fallback, rather than silently getting 0/0/0.
    check("empty.missLeavesCallerValue", cube.available == false
      && near(cube.color[0][0], 12345.0f, 1e-3f),
      "color[0][0]=" + fmt(cube.color[0][0]));
    check("empty.counters", map.ambientLookupsHit == 0 && map.ambientLookupsMissed == 1,
      "hit=" + std::to_string(map.ambientLookupsHit)
        + " missed=" + std::to_string(map.ambientLookupsMissed));
    // And the hit path, so the counters are shown to move in both directions.
    BspLeafAmbientCube second;
    const bool hitSecond = parsed && BspParser::leafAmbientAt(map, -5.0f, 5.0f, 5.0f, second);
    check("empty.countersBothWays", hitSecond && map.ambientLookupsHit == 1
      && map.ambientLookupsMissed == 1,
      "hit=" + std::to_string(map.ambientLookupsHit)
        + " missed=" + std::to_string(map.ambientLookupsMissed));
  }

  // ---- 5. pages: LDR wins, HDR is the HDR-only fallback, neither = none --
  {
    // LDR only.
    {
      BspFixture fixture = twoLeafFixture();
      std::vector<std::uint8_t> page, index;
      addThreePlusOneSamples(fixture, page, index);
      BspMap map;
      const bool parsed = parseFixture(fixture, map);
      check("page.ldrOnly", parsed && map.leafAmbientMode == BspLeafAmbientMode::RgbExp32
        && map.leafAmbientSamples.size() == 4,
        "samples=" + std::to_string(map.leafAmbientSamples.size()));
    }
    // HDR only: no index 52 and no data 56, so the 51/55 pair is the whole
    // lighting of the map -- the compile the rule above cannot lose.
    {
      BspFixture fixture = twoLeafFixture();
      std::vector<std::uint8_t> page, index;
      addThreePlusOneSamples(fixture, page, index);
      fixture.leafAmbientIndex.clear();
      fixture.leafAmbientLighting.clear();
      fixture.leafAmbientIndexHdr = index;
      fixture.leafAmbientLightingHdr = page;
      BspMap map;
      const bool parsed = parseFixture(fixture, map);
      check("page.hdrOnly", parsed && map.leafAmbientMode == BspLeafAmbientMode::RgbExp32
        && map.leafAmbientSamples.size() == 4,
        "mode=" + std::string(BspParser::leafAmbientModeName(map.leafAmbientMode))
          + " samples=" + std::to_string(map.leafAmbientSamples.size()));
    }
    // Both pages, and they disagree: the LDR page must be the one read. The
    // HDR page here holds 1/1/1 cubes, so a reader that preferred it would
    // report 1/255 instead of 10/255 on the first direction.
    {
      BspFixture fixture = twoLeafFixture();
      std::vector<std::uint8_t> page, index;
      addThreePlusOneSamples(fixture, page, index);
      std::uint8_t flat[6][4];
      fillCube(flat, 1, 1, 1, 0);
      tf2::native::appendAmbientSample(fixture.leafAmbientLightingHdr, flat, 0, 0, 0);
      tf2::native::appendAmbientSample(fixture.leafAmbientLightingHdr, flat, 0, 0, 0);
      tf2::native::appendAmbientSample(fixture.leafAmbientLightingHdr, flat, 0, 0, 0);
      tf2::native::appendAmbientSample(fixture.leafAmbientLightingHdr, flat, 0, 0, 0);
      fixture.leafAmbientIndexHdr = index;
      BspMap map;
      const bool parsed = parseFixture(fixture, map);
      BspLeafAmbientCube cube;
      const bool ok = parsed && BspParser::leafAmbientAt(map, 1.0f, 1.0f, 1.0f, cube);
      check("page.ldrWins", ok && near(cube.color[0][0], 10.0f / 255.0f, 1e-6f),
        "cube=" + cubeText(cube));
    }
    // Neither page: Unavailable, empty vectors, and the accessor says no.
    {
      BspFixture fixture = twoLeafFixture();
      BspMap map;
      const bool parsed = parseFixture(fixture, map);
      check("page.none", parsed && map.leafAmbientMode == BspLeafAmbientMode::Unavailable
        && map.leafAmbientEntries.empty() && map.leafAmbientSamples.empty(),
        "mode=" + std::string(BspParser::leafAmbientModeName(map.leafAmbientMode)));
      BspLeafAmbientCube cube;
      const bool ok = parsed && BspParser::leafAmbientAt(map, 1.0f, 1.0f, 1.0f, cube);
      check("page.noneAccessorRefuses", !ok && map.ambientLookupsMissed == 1 && !cube.available,
        "missed=" + std::to_string(map.ambientLookupsMissed));
    }
    // An index whose length is not leafCount*4 is not this lump: reading it
    // anyway would hand leaf i another leaf's cube.
    {
      BspFixture fixture = twoLeafFixture();
      std::vector<std::uint8_t> page, index;
      addThreePlusOneSamples(fixture, page, index);
      index.resize(index.size() - 4);
      fixture.leafAmbientIndex = index;
      BspMap map;
      const bool parsed = parseFixture(fixture, map);
      check("page.shortIndexRefused", parsed
        && map.leafAmbientMode == BspLeafAmbientMode::Unavailable
        && map.leafAmbientEntries.empty(),
        "mode=" + std::string(BspParser::leafAmbientModeName(map.leafAmbientMode)));
    }
    // A data lump that is not a whole number of 28-byte records.
    {
      BspFixture fixture = twoLeafFixture();
      std::vector<std::uint8_t> page, index;
      addThreePlusOneSamples(fixture, page, index);
      page.resize(page.size() - 3);
      fixture.leafAmbientLighting = page;
      BspMap map;
      const bool parsed = parseFixture(fixture, map);
      check("page.raggedDataRefused", parsed
        && map.leafAmbientMode == BspLeafAmbientMode::Unavailable,
        "mode=" + std::string(BspParser::leafAmbientModeName(map.leafAmbientMode)));
    }
    // A record that reaches past the data lump is clamped AND counted.
    {
      BspFixture fixture = twoLeafFixture();
      std::vector<std::uint8_t> page, index;
      addThreePlusOneSamples(fixture, page, index);
      // Leaf 1 claims three cubes starting at the last one only.
      index[4] = 3;
      index[5] = 0;
      index[6] = 3;
      index[7] = 0;
      fixture.leafAmbientIndex = index;
      BspMap map;
      const bool parsed = parseFixture(fixture, map);
      check("page.overrunClampedAndCounted", parsed
        && map.leafAmbientRecordsOutOfRange == 1
        && map.leavesWithAmbient == 2,
        "outOfRange=" + std::to_string(map.leafAmbientRecordsOutOfRange)
          + " with=" + std::to_string(map.leavesWithAmbient));
    }
  }

  // ---- 6. the miss contract on the two-step path the renderer uses ---
  //
  // The renderer cannot call leafAmbientAt(): it has to tell "outside every
  // leaf" from "in a leaf that shipped no bake", because those two absences
  // point at different repairs (a wrong coordinate space versus a map that
  // never baked that room). So it resolves leafAt() first and then asks
  // leafAmbientAtLeaf(). That second call must obey the same contract as the
  // one-shot form: false, and the caller's cube left exactly as it was.
  // A miss that quietly zeroed the struct would hand the renderer a black
  // cube it had no way to distinguish from a bake that is genuinely black.
  {
    BspFixture fixture = twoLeafFixture();
    std::vector<std::uint8_t> page, index;
    addThreePlusOneSamples(fixture, page, index);
    index[0] = 0;
    index[1] = 0;
    fixture.leafAmbientIndex = index;
    BspMap map;
    const bool parsed = parseFixture(fixture, map);
    const int leaf = parsed ? BspParser::leafAt(map, 5.0f, 5.0f, 5.0f) : -1;
    BspLeafAmbientCube carried;
    carried.available = false;
    carried.leafIndex = -1;
    carried.color[0][0] = 12345.0f;
    carried.color[5][2] = 6789.0f;
    const bool inLeaf = leaf >= 0
      && BspParser::leafAmbientAtLeaf(map, leaf, carried);
    check("leaf.emptiedLeafFoundButNoCube", leaf >= 0 && !inLeaf,
      "leaf=" + std::to_string(leaf) + (inLeaf ? " returned a cube" : " false"));
    check("leaf.leafAtLeafMissLeavesCallerValue", carried.available == false
      && carried.leafIndex == -1
      && near(carried.color[0][0], 12345.0f, 1e-3f)
      && near(carried.color[5][2], 6789.0f, 1e-3f),
      "available=" + std::string(carried.available ? "true" : "false")
        + " leafIndex=" + std::to_string(carried.leafIndex)
        + " color[0][0]=" + fmt(carried.color[0][0])
        + " color[5][2]=" + fmt(carried.color[5][2]));
    // leafAt() itself is a pure query: the renderer calls it once per
    // instance in addition to the lookup, so a query that moved the same
    // counters the lookup reports would inflate every number downstream.
    check("leaf.leafAtDoesNotCount", parsed && map.ambientLookupsHit == 0
      && map.ambientLookupsMissed == 0,
      "hit=" + std::to_string(map.ambientLookupsHit)
        + " missed=" + std::to_string(map.ambientLookupsMissed));
  }

  // ---- 7. the per-frame table: equal cubes get equal slots ------------
  //
  // Two instances standing in the same room resolve to the same decoded
  // sample, so the table must hand them the SAME slot -- otherwise the
  // buffer grows a row per instance and the per-leaf cube quietly becomes
  // per-instance data. The comparison is exact on purpose: the cubes come
  // from one decoded lump, so equality of the floats is the right test and
  // a tolerance would merge genuinely different readings.
  {
    BspFixture fixture = twoLeafFixture();
    std::vector<std::uint8_t> page, index;
    addThreePlusOneSamples(fixture, page, index);
    BspMap map;
    const bool parsed = parseFixture(fixture, map);
    // The same leaf asked about twice, exactly as two instances in one room
    // would ask, then a different leaf.
    BspLeafAmbientCube first, again, other;
    const bool okFirst = parsed && BspParser::leafAmbientAt(map, 5.0f, 5.0f, 5.0f, first);
    const bool okAgain = parsed && BspParser::leafAmbientAt(map, 5.0f, 5.0f, 5.0f, again);
    const bool okOther = parsed && BspParser::leafAmbientAt(map, -5.0f, 5.0f, 5.0f, other);
    std::vector<tf2::native::LeafAmbientCubeFace> distinct;
    const std::uint32_t slotFirst = okFirst ? tf2::native::leafAmbientSlotFor(distinct, rendererFaceOf(first)) : 99u;
    const std::uint32_t slotAgain = okAgain ? tf2::native::leafAmbientSlotFor(distinct, rendererFaceOf(again)) : 98u;
    // Read the size BEFORE the third cube goes in. Asking for a different cube
    // is exactly what makes the table grow, so a size checked after that call
    // measures the wrong thing and fails a correct table -- which is what this
    // check did on its first run (distinct=2 while both equal cubes sat on slot
    // 0). The check was wrong, not the table; the slot numbers already agreed.
    const std::size_t distinctAfterEqual = distinct.size();
    const std::uint32_t slotOther = okOther ? tf2::native::leafAmbientSlotFor(distinct, rendererFaceOf(other)) : 97u;
    check("table.equalCubesEqualSlots", okFirst && okAgain && slotFirst == slotAgain
      && distinctAfterEqual == 1,
      "slotFirst=" + std::to_string(slotFirst) + " slotAgain=" + std::to_string(slotAgain)
        + " distinctAfterEqual=" + std::to_string(distinctAfterEqual));
    check("table.differentCubeNewSlot", okOther && slotOther != slotFirst
      && distinct.size() == 2,
      "slotOther=" + std::to_string(slotOther) + " distinct=" + std::to_string(distinct.size()));
    // Every slot must be a real index into what would be uploaded, and the
    // sentinel must not collide with any of them.
    check("table.slotIsIndexAndSentinelDistinct", slotFirst < distinct.size()
      && slotOther < distinct.size()
      && tf2::native::kLeafAmbientNoSlot < 0.0f,
      "slots=" + std::to_string(slotFirst) + "," + std::to_string(slotOther)
        + " sentinel=" + fmt(tf2::native::kLeafAmbientNoSlot));
  }

  // ---- 7.1 the row the shader indexes is the row the CPU packs ---------
  //
  // The consumer is `StructuredBuffer<float4> leafAmbientCubeFaces`, read as
  // six float4 rows per slot (native/src/native_renderer_shaders.cpp:616-618),
  // so one cube on the CPU must be exactly six float4s or the upload shears:
  // the shader would take face 1 from floats 4-6 while the source had put it at
  // 3-5. It was six float3s for one task round, and the same mismatch also ran
  // the tail of the memcpy past the end of the vector, which is what made the
  // leaf arm worse than misaligned -- it was irreproducible. Both numbers are
  // asserted here so a future edit to either side fails a CHECK rather than a
  // picture, and a picture is the harder failure to read.
  {
    tf2::native::LeafAmbientCubeFace row;
    const std::size_t rowBytes = sizeof(float) * 4;
    const std::size_t faces = static_cast<std::size_t>(tf2::native::kLeafAmbientFaces);
    const auto faceSpan = static_cast<std::size_t>(
      reinterpret_cast<const char*>(&row.rgb[1]) - reinterpret_cast<const char*>(&row.rgb[0]));
    check("table.rowIsOneFloat4PerFace",
      sizeof(tf2::native::LeafAmbientCubeFace) == faces * rowBytes
        && faceSpan == rowBytes && row.rgb[0].size() == std::size_t{4},
      "sizeof=" + std::to_string(sizeof(tf2::native::LeafAmbientCubeFace))
        + " faces=" + std::to_string(faces) + " rowBytes=" + std::to_string(rowBytes)
        + " faceSpan=" + std::to_string(faceSpan));
    // The fourth lane is padding the shader never reads, so it must not be part
    // of a cube's identity: two cubes whose decoded colours agree and whose
    // padding differs are the SAME bake and must share a slot, or the table
    // would grow a row for a float nobody shades with.
    tf2::native::LeafAmbientCubeFace packed{};
    packed.rgb[3][0] = 1.0f;
    tf2::native::LeafAmbientCubeFace padded = packed;
    padded.rgb[3][3] = 123.0f;
    std::vector<tf2::native::LeafAmbientCubeFace> laneDistinct;
    const std::uint32_t slotPacked = tf2::native::leafAmbientSlotFor(laneDistinct, packed);
    const std::uint32_t slotPadded = tf2::native::leafAmbientSlotFor(laneDistinct, padded);
    check("table.paddingLaneIsNotIdentity",
      slotPacked == slotPadded && laneDistinct.size() == 1,
      "slotPacked=" + std::to_string(slotPacked)
        + " slotPadded=" + std::to_string(slotPadded)
        + " distinct=" + std::to_string(laneDistinct.size()));
  }

  // ---- 8. the slot BUDGET: rows follow distinct cubes, not samples ------
  //
  // The buffer exists so that N instances standing in one room cost ONE row.
  // Section 7 inserts the same cube twice; this inserts it sixteen times -- a
  // full sample page -- and then proves the falsifier, because a table that
  // ignored its argument would pass a dedupe test just as happily. One mantissa
  // LSB on one channel of one direction is a genuinely different bake (0 -> 1
  // at exponent 0 is 1/255 of full scale, 2.894 after the renderer's x738.0f)
  // and MUST take a new slot.
  {
    BspFixture fixture = twoLeafFixture();
    std::vector<std::uint8_t> page, index;
    addThreePlusOneSamples(fixture, page, index);
    BspMap map;
    const bool parsed = parseFixture(fixture, map);
    BspLeafAmbientCube cube{};
    const bool ok = parsed && BspParser::leafAmbientAt(map, 5.0f, 5.0f, 5.0f, cube);
    const tf2::native::LeafAmbientCubeFace base = rendererFaceOf(cube);
    std::vector<tf2::native::LeafAmbientCubeFace> distinct;
    std::uint32_t lastSlot = 99u;
    bool everyInsertReused = true;
    for (int insert = 0; insert < 16; ++insert) {
      lastSlot = tf2::native::leafAmbientSlotFor(distinct, base);
      everyInsertReused = everyInsertReused && lastSlot == 0u;
    }
    const std::size_t distinctAfterSixteen = distinct.size();
    check("table.sixteenEqualInstancesOneSlot",
      ok && everyInsertReused && distinctAfterSixteen == 1,
      "lastSlot=" + std::to_string(lastSlot)
        + " distinctAfter16=" + std::to_string(distinctAfterSixteen));
    // The falsifier. base.rgb[1][0] is 0: direction -X of the centre sample is
    // black in this fixture. Lifting it by one mantissa LSB is the smallest
    // change a shipped page can carry, and the table must see it.
    tf2::native::LeafAmbientCubeFace oneLsb = base;
    oneLsb.rgb[1][0] = (1.0f / 255.0f) * tf2::native::kLeafAmbientScale;
    const std::uint32_t slotLsb = tf2::native::leafAmbientSlotFor(distinct, oneLsb);
    check("table.oneMantissaLsbIsANewSlot",
      slotLsb == 1u && distinct.size() == 2,
      "slotLsb=" + std::to_string(slotLsb) + " distinct=" + std::to_string(distinct.size())
        + " delta=" + fmt(oneLsb.rgb[1][0] - base.rgb[1][0]));
    // -0.0f and +0.0f are the same light. IEEE equality says so and the table
    // therefore merges them; this is one of the candidate causes the first RED
    // report had to rule out, so it is asserted instead of assumed.
    tf2::native::LeafAmbientCubeFace positiveZero{};
    tf2::native::LeafAmbientCubeFace negativeZero{};
    negativeZero.rgb[2][2] = -0.0f;
    std::vector<tf2::native::LeafAmbientCubeFace> zeroDistinct;
    const std::uint32_t slotPositiveZero = tf2::native::leafAmbientSlotFor(zeroDistinct, positiveZero);
    const std::uint32_t slotNegativeZero = tf2::native::leafAmbientSlotFor(zeroDistinct, negativeZero);
    check("table.signedZeroIsOneCube",
      slotPositiveZero == slotNegativeZero && zeroDistinct.size() == 1,
      "slotPositiveZero=" + std::to_string(slotPositiveZero)
        + " slotNegativeZero=" + std::to_string(slotNegativeZero)
        + " distinct=" + std::to_string(zeroDistinct.size()));
  }

  // ---- 9. a baked-black cube is a HIT, not a miss -----------------------
  //
  // The renderer's absence design exists because an all-zero cube and a missing
  // cube look the same on screen. The API has to keep them apart: a leaf whose
  // bake is black must still report available=true, land in withCube and be
  // drawn black -- it must NOT spend a noCubeInLeaf. Half A measured 42.65% of
  // the corpus leaves carrying no sample at all, which is the other case; if a
  // reader collapsed the two, the feature would read as "no cube" on exactly
  // the leaves that do have one.
  {
    BspFixture fixture = twoLeafFixture();
    std::vector<std::uint8_t> page, index;
    addThreePlusOneSamples(fixture, page, index);
    // Sample 0 is the corner cube at (0,0,0): 28 bytes, six ColorRGBExp32 in
    // the first 24. Zero those and only those, so its position bytes stay 0 and
    // a query near that corner still reaches THIS sample.
    for (int byte = 0; byte < 24; ++byte) page[static_cast<std::size_t>(byte)] = 0;
    fixture.leafAmbientLighting = page;
    BspMap map;
    const bool parsed = parseFixture(fixture, map);
    BspLeafAmbientCube black{};
    // (1,1,1) is the point section 3 proves picks sample 0.
    const bool hit = parsed && BspParser::leafAmbientAt(map, 1.0f, 1.0f, 1.0f, black);
    float brightest = 0.0f;
    if (hit) {
      for (int direction = 0; direction < 6; ++direction) {
        for (int channel = 0; channel < 3; ++channel) {
          brightest = std::max(brightest, std::fabs(black.color[direction][channel]));
        }
      }
    }
    check("cube.blackCubeIsAHit",
      hit && black.available && black.sampleIndex == 0 && brightest == 0.0f
        && map.ambientLookupsHit == 1 && map.ambientLookupsMissed == 0,
      "available=" + std::string(black.available ? "true" : "false")
        + " sampleIndex=" + std::to_string(black.sampleIndex)
        + " brightest=" + fmt(brightest)
        + " hit=" + std::to_string(map.ambientLookupsHit)
        + " missed=" + std::to_string(map.ambientLookupsMissed));
  }

  const int exitCode = failures == 0 ? 0 : 1;
  std::cout << "{\"summary\":\"self-test\",\"failures\":" << failures << "}\n";
  return exitCode;
}

// ---------------------------------------------------------------------------
// --map / --corpus: the same question asked of real BSP files
// ---------------------------------------------------------------------------

// The candidate order map_lookup.h prescribes, so the demo resolves the map
// the game would load and not a lookalike in another store. `chosen` records
// which candidate answered, because "the cube was dark" and "the cube came
// from an unexpected copy of the map" are different bugs.
std::vector<std::uint8_t> readDemoMap(const tf2::native::AssetRoot& root,
    const std::string& mapName, std::string& chosen) {
  const auto plan = tf2::native::planMapLookup(mapName);
  if (!plan.nameUsable) {
    chosen = "unusable:" + plan.reason;
    return {};
  }
  const auto& archives = root.archives();
  for (const auto& candidate : plan.candidates) {
    if (!candidate.packed) {
      if (auto bytes = readFile(root.resolve(candidate.relativePath)); !bytes.empty()) {
        chosen = candidate.store + ":" + candidate.relativePath;
        return bytes;
      }
      continue;
    }
    std::vector<std::filesystem::path> holders;
    archives.collectContaining(candidate.relativePath, holders);
    for (const auto& archivePath : holders) {
      const auto* archive = archives.find(archivePath);
      if (archive == nullptr) continue;
      std::string error;
      auto bytes = archive->read(candidate.relativePath, &error);
      if (!bytes.empty()) {
        chosen = candidate.store + ":" + candidate.relativePath;
        return bytes;
      }
    }
  }
  chosen = "missing";
  return {};
}

void dumpCube(const BspLeafAmbientCube& cube, std::int32_t leaf) {
  std::cout << "{\"leaf\":" << leaf << ",\"cluster\":" << cube.cluster
    << ",\"samples\":" << cube.sampleCount << ",\"picked\":" << cube.sampleIndex
    << ",\"position\":[" << fmt(cube.position[0]) << "," << fmt(cube.position[1])
    << "," << fmt(cube.position[2]) << "],\"cube\":\"" << cubeText(cube) << "\"}\n";
}

void emitLookupCounters(const BspMap& map) {
  std::cout << "{\"reading\":\"leafAmbientLookups\",\"hit\":" << map.ambientLookupsHit
    << ",\"missed\":" << map.ambientLookupsMissed << "}\n";
}

int runMap(const std::filesystem::path& mapPath, std::size_t leafLimit) {
  const auto bytes = readFile(mapPath);
  if (bytes.empty()) {
    std::cout << "{\"error\":\"unreadable map\",\"path\":\"" << mapPath.filename().string() << "\"}\n";
    return 1;
  }
  BspMap map;
  if (!BspParser::parse(bytes, map, 200000, 4096)) {
    std::cout << "{\"error\":\"parse\",\"detail\":\"" << map.error << "\"}\n";
    return 1;
  }
  emitMapReadings(map, mapPath.filename().string());
  // The whole page, not just the leaves below: "which corner is brightest" is a
  // property of the map, and asking it of twelve sampled leaves would answer
  // about the sample. Same tally the corpus run aggregates.
  DirectionTally tally;
  tallyDirections(map, tally);
  emitDirectionTally(tally, mapPath.filename().string());
  // Up to leafLimit leaves that DO carry a cube, dumped in leaf order so the
  // same map always prints the same leaves. A leaf with no sample prints
  // nothing here -- the counts above already say how many there are, and
  // padding this list with zero cubes is the failure mode the probe exists
  // to prevent.
  std::size_t dumped = 0;
  for (std::size_t leaf = 0; leaf < map.leaves.size() && dumped < leafLimit; ++leaf) {
    BspLeafAmbientCube cube;
    if (!BspParser::leafAmbientAtLeaf(map, static_cast<int>(leaf), cube)) continue;
    dumpCube(cube, static_cast<std::int32_t>(leaf));
    ++dumped;
  }
  emitLookupCounters(map);
  return 0;
}

int runCorpus(const std::filesystem::path& directory) {
  std::error_code error;
  if (!std::filesystem::is_directory(directory, error)) {
    std::cout << "{\"error\":\"not a directory\"}\n";
    return 1;
  }
  std::vector<std::filesystem::path> maps;
  for (const auto& entry : std::filesystem::directory_iterator(directory, error)) {
    if (!entry.is_regular_file(error)) continue;
    if (entry.path().extension() != L".bsp") continue;
    maps.push_back(entry.path());
  }
  std::sort(maps.begin(), maps.end());
  long long usable = 0, ldr = 0, hdrOnly = 0, none = 0, unreadable = 0, unparsed = 0;
  long long leaves = 0, leavesWith = 0, leavesWithout = 0, samples = 0;
  long long outOfRange = 0, unreferenced = 0;
  DirectionTally tally;
  for (const auto& path : maps) {
    const auto bytes = readFile(path);
    const std::string name = path.filename().string();
    if (bytes.empty()) {
      ++unreadable;
      std::cout << "{\"map\":\"" << name << "\",\"error\":\"unreadable\"}\n";
      continue;
    }
    BspMap map;
    if (!BspParser::parse(bytes, map, 200000, 4096)) {
      ++unparsed;
      std::cout << "{\"map\":\"" << name << "\",\"error\":\"parse\",\"detail\":\""
        << map.error << "\"}\n";
      continue;
    }
    emitMapReadings(map, name);
    ++usable;
    if (map.leafAmbientMode == BspLeafAmbientMode::Unavailable) ++none;
    else if (!map.leafAmbientHdrPage) ++ldr;
    else ++hdrOnly;
    leaves += static_cast<long long>(map.leaves.size());
    leavesWith += map.leavesWithAmbient;
    leavesWithout += map.leavesWithoutAmbient;
    samples += static_cast<long long>(map.leafAmbientSamples.size());
    outOfRange += map.leafAmbientRecordsOutOfRange;
    unreferenced += map.leafAmbientSamplesUnreferenced;
    tallyDirections(map, tally);
  }
  // The corpus-wide axis histogram. One map cannot settle the axis order -- a
  // single map can be built almost entirely indoors or almost entirely
  // outdoors -- so the claim in bsp_map.h is checked against every cube on
  // this host, and the per-map lines above let a reader see which map disagrees.
  emitDirectionTally(tally, "corpus");
  std::cout << "{\"summary\":\"corpus\",\"maps\":" << maps.size()
    << ",\"usable\":" << usable << ",\"ldr\":" << ldr << ",\"hdrOnly\":" << hdrOnly
    << ",\"none\":" << none << ",\"unreadable\":" << unreadable
    << ",\"unparsed\":" << unparsed << ",\"leaves\":" << leaves
    << ",\"leavesWith\":" << leavesWith << ",\"leavesWithout\":" << leavesWithout
    << ",\"samples\":" << samples << ",\"outOfRange\":" << outOfRange
    << ",\"unreferenced\":" << unreferenced << "}\n";
  return 0;
}

// ---------------------------------------------------------------------------
// --demo: a real tick of a real demo, joined to the real map
// ---------------------------------------------------------------------------

// An entity's world position. Two things have to be right before a leaf
// lookup means anything, and both are already solved elsewhere in this tree:
//
//   1. WHICH slot. EntityState::properties is an unordered_map, so "first name
//      ending in m_vecOrigin" is hash order, not a rule. rankPropertyCandidates
//      (native/include/entity_model.h:319) IS the selection rule, exported for
//      exactly this reason -- a probe that re-implemented it would drift from
//      the renderer's answer while both stayed green.
//   2. WHICH z. A player's m_vecOrigin is a VectorXY: the wire carries x and y
//      and the real z travels in a separate Float named "<slot>[2]" (the P1 z
//      defect, native/src/entity_model.cpp:84-96). Reading only the XY leaves
//      every player at z = 0, which is under the floor of most leaves -- a
//      lookup that would then answer confidently and wrongly.
//
// `complete` is false exactly when z is still a guess, and the caller prints
// it, because a partial point is a weaker reading than a full one.
bool readOrigin(const tf2::native::EntityState& state, float origin[3], bool& complete) {
  complete = false;
  const auto candidates = tf2::native::rankPropertyCandidates(state, "m_vecOrigin");
  for (const auto& candidate : candidates) {
    const auto it = state.properties.find(candidate.name);
    if (it == state.properties.end()) continue;
    const auto& value = it->second;
    if (value.type != tf2::native::SendPropType::Vector
        && value.type != tf2::native::SendPropType::VectorXY) {
      continue;
    }
    origin[0] = value.x;
    origin[1] = value.y;
    bool carriesZ = value.type == tf2::native::SendPropType::Vector;
    origin[2] = carriesZ ? value.z : 0.0f;
    if (!carriesZ) {
      const auto sibling = state.properties.find(candidate.name + "[2]");
      if (sibling != state.properties.end()) {
        const auto& z = sibling->second;
        origin[2] = z.type == tf2::native::SendPropType::Int
            ? static_cast<float>(z.intValue) : z.x;
        carriesZ = true;
      }
    }
    complete = carriesZ;
    return std::isfinite(origin[0]) && std::isfinite(origin[1]) && std::isfinite(origin[2]);
  }
  return false;
}

int runDemo(const std::filesystem::path& demoPath, const std::filesystem::path& tfRoot,
    const std::vector<std::int32_t>& requestedTicks, std::size_t entityLimit) {
  tf2::native::DemoHeader header;
  if (!tf2::native::parseDemoHeaderFile(demoPath, header)) {
    std::cout << "{\"error\":\"demo header\",\"detail\":\"" << header.error << "\"}\n";
    return 1;
  }
  tf2::native::DemoIndex index;
  if (!tf2::native::indexDemoFile(demoPath, header, index)) {
    std::cout << "{\"error\":\"demo index\",\"detail\":\"" << index.error << "\"}\n";
    return 1;
  }
  tf2::native::DemoNetworkSummary summary;
  summary.networkProtocol = header.networkProtocol;
  if (!tf2::native::scanKnownDemoMessages(demoPath, index, summary)) {
    std::cout << "{\"error\":\"demo scan\"}\n";
    return 1;
  }
  const auto root = tf2::native::AssetRoot::fromPath(tfRoot);
  std::string mapSource;
  const auto mapBytes = readDemoMap(root, header.mapName, mapSource);
  if (mapBytes.empty()) {
    std::cout << "{\"error\":\"map unavailable\",\"map\":\"" << header.mapName
      << "\",\"detail\":\"" << mapSource << "\"}\n";
    return 1;
  }
  BspMap map;
  if (!BspParser::parse(mapBytes, map, 200000, 4096)) {
    std::cout << "{\"error\":\"map parse\",\"detail\":\"" << map.error << "\"}\n";
    return 1;
  }
  std::cout << "{\"demo\":\"" << demoPath.filename().string() << "\",\"map\":\""
    << header.mapName << "\",\"mapSource\":\"" << mapSource << "\",\"mode\":\""
    << BspParser::leafAmbientModeName(map.leafAmbientMode) << "\",\"leaves\":"
    << map.leaves.size() << ",\"leavesWith\":" << map.leavesWithAmbient
    << ",\"leavesWithout\":" << map.leavesWithoutAmbient << "}\n";

  std::unordered_map<std::uint16_t, std::string> classNames;
  for (const auto& schema : summary.serverClassSchemas) classNames[schema.id] = schema.name;

  std::vector<std::int32_t> ticks = requestedTicks;
  if (ticks.empty()) {
    // No tick asked for: use the retained checkpoints, which is the coarsest
    // resolution the demo actually offers. --ticks exists for a caller that
    // already cares about a tick, and the resolved tick is printed either way
    // so the coarseness is never hidden.
    for (const auto& checkpoint : summary.entityHistoryCheckpoints) {
      ticks.push_back(checkpoint.tick);
    }
    for (const auto& checkpoint : summary.entityHistoryArchive) ticks.push_back(checkpoint.tick);
    std::sort(ticks.begin(), ticks.end());
    if (ticks.size() > 3) ticks.resize(3);
  }

  long long entitiesSeen = 0, noOrigin = 0, outsideTree = 0, partial = 0;
  for (const auto tick : ticks) {
    std::vector<tf2::native::EntityState> states;
    std::int32_t resolved = tick;
    const auto status = tf2::native::queryEntitySnapshotAtOrBeforeTick(summary, tick, states, &resolved);
    std::cout << "{\"query\":\"tick\",\"tick\":" << tick << ",\"resolved\":" << resolved
      << ",\"status\":" << static_cast<int>(status) << ",\"entities\":" << states.size() << "}\n";
    std::size_t printed = 0;
    for (std::size_t entity = 0; entity < states.size() && printed < entityLimit; ++entity) {
      float origin[3] = {0.0f, 0.0f, 0.0f};
      bool complete = false;
      if (!readOrigin(states[entity], origin, complete)) { ++noOrigin; continue; }
      ++entitiesSeen;
      if (!complete) ++partial;
      const int leaf = BspParser::leafAt(map, origin[0], origin[1], origin[2]);
      const int cluster = leaf >= 0 && static_cast<std::size_t>(leaf) < map.leaves.size()
        ? map.leaves[static_cast<std::size_t>(leaf)].cluster : -1;
      BspLeafAmbientCube cube;
      const bool hit = BspParser::leafAmbientAt(map, origin[0], origin[1], origin[2], cube);
      if (leaf < 0) ++outsideTree;
      std::string className;
      const auto name = classNames.find(static_cast<std::uint16_t>(states[entity].classId));
      if (name != classNames.end()) className = name->second;
      std::cout << "{\"entity\":" << entity << ",\"class\":\"" << className
        << "\",\"origin\":[" << fmt(origin[0]) << "," << fmt(origin[1]) << ","
        << fmt(origin[2]) << "],\"complete\":" << (complete ? 1 : 0)
        << ",\"leaf\":" << leaf << ",\"cluster\":" << cluster
        << ",\"hit\":" << (hit ? 1 : 0) << ",\"samples\":" << cube.sampleCount
        << ",\"picked\":" << cube.sampleIndex << ",\"cube\":\""
        << (hit ? cubeText(cube) : std::string()) << "\"}\n";
      ++printed;
    }
  }
  std::cout << "{\"reading\":\"demoEntities\",\"withOrigin\":" << entitiesSeen
    << ",\"withoutOrigin\":" << noOrigin << ",\"partialOrigin\":" << partial
    << ",\"outsideTree\":" << outsideTree << "}\n";
  emitLookupCounters(map);
  return 0;
}

} // namespace

int wmain(int argc, wchar_t** argv) {
  const wchar_t* usage =
      L"usage: leaf_ambient_probe --self-test\n"
      L"       leaf_ambient_probe --map <map.bsp> [--leaves N]\n"
      L"       leaf_ambient_probe --corpus <maps dir>\n"
      L"       leaf_ambient_probe --demo <demo.dem> [--tf-root <dir>]"

      L" [--ticks a,b,c] [--entities N]\n";
  std::filesystem::path mapPath;
  std::filesystem::path corpusPath;
  std::filesystem::path demoPath;
  std::filesystem::path tfRoot = L"D:/SteamLibrary/steamapps/common/Team Fortress 2/tf";
  std::vector<std::int32_t> requestedTicks;
  std::size_t leafLimit = 8;
  std::size_t entityLimit = 24;
  bool selfTest = false;
  for (int i = 1; i < argc; ++i) {
    const std::wstring flag(argv[i]);
    const auto value = [&](const wchar_t* name) -> const wchar_t* {
      if (i + 1 >= argc) { std::wcerr << L"missing value for " << name << L"\n" << usage; return nullptr; }
      return argv[++i];
    };
    if (flag == L"--self-test") {
      selfTest = true;
    } else if (flag == L"--map" || flag == L"--corpus" || flag == L"--demo" || flag == L"--tf-root") {
      const wchar_t* text = value(flag.c_str());
      if (text == nullptr) return 2;
      if (flag == L"--map") mapPath = std::filesystem::path(text);
      else if (flag == L"--corpus") corpusPath = std::filesystem::path(text);
      else if (flag == L"--demo") demoPath = std::filesystem::path(text);
      else tfRoot = std::filesystem::path(text);
    } else if (flag == L"--leaves" || flag == L"--entities") {
      const wchar_t* text = value(flag.c_str());
      if (text == nullptr) return 2;
      wchar_t* end = nullptr;
      const long parsed = std::wcstol(text, &end, 10);
      if (end == text || *end != L'\0' || parsed < 0 || parsed > 100000) {
        std::wcerr << L"invalid number for " << flag << L"\n";
        return 2;
      }
      if (flag == L"--leaves") leafLimit = static_cast<std::size_t>(parsed);
      else entityLimit = static_cast<std::size_t>(parsed);
    } else if (flag == L"--ticks") {
      const wchar_t* text = value(flag.c_str());
      if (text == nullptr) return 2;
      const std::wstring list(text);
      std::size_t start = 0;
      while (start <= list.size()) {
        const std::size_t comma = list.find(L',', start);
        const std::wstring item = list.substr(start,
            comma == std::wstring::npos ? std::wstring::npos : comma - start);
        if (!item.empty()) {
          wchar_t* end = nullptr;
          const long parsed = std::wcstol(item.c_str(), &end, 10);
          if (end == item.c_str() || *end != L'\0' || parsed < 0) {
            std::wcerr << L"invalid tick in --ticks\n";
            return 2;
          }
          requestedTicks.push_back(static_cast<std::int32_t>(parsed));
        }
        if (comma == std::wstring::npos) break;
        start = comma + 1;
      }
    } else {
      std::wcerr << L"unknown option: " << flag << L"\n" << usage;
      return 2;
    }
  }
  if (selfTest && mapPath.empty() && corpusPath.empty() && demoPath.empty()) return runSelfTest();
  if (!mapPath.empty() && corpusPath.empty() && demoPath.empty()) return runMap(mapPath, leafLimit);
  if (!corpusPath.empty() && mapPath.empty() && demoPath.empty()) return runCorpus(corpusPath);
  if (!demoPath.empty() && mapPath.empty() && corpusPath.empty()) {
    return runDemo(demoPath, tfRoot, requestedTicks, entityLimit);
  }
  std::wcerr << usage;
  return 2;
}
