// brush_entity_probe -- does a brush entity's *N dmodel actually move?
//
// WHY THIS EXISTS (D-14 follow-up, 2026-11-09). The product refuses to draw a
// brush model whose classname is not in its static whitelist (bsp_map.cpp,
// brushClassIsStatic) because the vertices are baked at the compile-time pose
// and nothing in the renderer applies a per-frame transform. That refusal is a
// claim about the DEMO: that these entities move. Today the only number behind
// it is "skippedMoving=84", and 84 reads the same whether those are 84 doors or
// 84 trains that never open.
//
// This probe asks the demo directly. It walks the retained entity snapshots,
// keeps every entity whose m_nModelIndex resolves to a "*N" model in the
// modelprecache (that is what a brush entity carries, and what the numbered
// dmodel in the BSP is called), and reports the origin series per entity. A
// series that never changes is a pose the baked vertices already have; a series
// that moves is the price of the refusal, in map units.
//
// READ THE CAVEAT. queryEntitySnapshotAtOrBeforeTick answers from retained
// CHECKPOINTS (8 live + up to 96 archived), and the median checkpoint gap on
// koth_bagel_rc13 is 822 ticks (demo_header.h, T53 note). A door that opens and
// shuts inside one gap reads as static here. "moving" is therefore a positive
// that can be trusted; "static" is only "no movement at the resolution this
// retention provides", and the row prints the gap so a reader can see it.
//
// Usage: brush_entity_probe [--model <N>] [--ticks <a,b,c>] <demo.dem>
//   --model N    only the dmodel "*N" (e.g. 12 for func_door's *12)
//   --ticks ...  sample these demo ticks instead of the retained checkpoints;
//                the row prints resolved=<tick> so a coarse answer is visible
// Exit: 0 = the demo parsed and every requested snapshot was answered,
//       1 = the demo did not parse, 2 = bad arguments.

#include "demo_header.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

// m_vecOrigin is a Vector on a brush entity. VectorXY (a 2D slot some classes
// carry) is accepted for x/y but leaves z at 0, which is honest for a question
// about horizontal movement and wrong for a lift -- so the caller is told.
bool readOrigin(const tf2::native::EntityState& state, float origin[3], bool& complete) {
  const auto it = state.properties.find("m_vecOrigin");
  if (it == state.properties.end()) return false;
  const auto& value = it->second;
  if (value.type == tf2::native::SendPropType::Vector) {
    origin[0] = value.x; origin[1] = value.y; origin[2] = value.z;
    complete = true;
    return true;
  }
  if (value.type == tf2::native::SendPropType::VectorXY) {
    origin[0] = value.x; origin[1] = value.y; origin[2] = 0.0f;
    complete = false;
    return true;
  }
  return false;
}

bool readModelIndex(const tf2::native::EntityState& state, std::uint16_t& index) {
  const auto it = state.properties.find("m_nModelIndex");
  if (it == state.properties.end()) return false;
  if (it->second.type != tf2::native::SendPropType::Int) return false;
  const long long raw = it->second.intValue;
  if (raw < 0 || raw > 0xffff) return false;
  index = static_cast<std::uint16_t>(raw);
  return true;
}

struct SnapshotView {
  std::int32_t tick = 0;       // the tick asked for
  std::int32_t resolved = 0;   // the checkpoint the answer came from
  const std::vector<tf2::native::EntityState>* states = nullptr;
  const std::vector<std::int32_t>* classByIndex = nullptr;
};

struct Candidate {
  int entity = -1;
  int model = -1;
  std::string className;
  std::size_t samples = 0;
  bool haveFirst = false;
  float first[3] = {0.0f, 0.0f, 0.0f};
  float last[3] = {0.0f, 0.0f, 0.0f};
  float displacement = 0.0f;
};

float distanceBetween(const float left[3], const float right[3]) {
  const float dx = left[0] - right[0], dy = left[1] - right[1], dz = left[2] - right[2];
  return std::sqrt(dx * dx + dy * dy + dz * dz);
}

} // namespace

int wmain(int argc, wchar_t** argv) {
  const wchar_t* usage =
      L"usage: brush_entity_probe [--model <N>] [--ticks <a,b,c>] <demo.dem>\n";
  int modelFilter = -1;
  std::vector<std::int32_t> requestedTicks;
  int firstPath = 1;
  while (firstPath < argc && argv[firstPath][0] == L'-') {
    const std::wstring flag(argv[firstPath]);
    if (flag == L"--model") {
      if (firstPath + 1 >= argc) { std::wcerr << usage; return 2; }
      const long parsed = std::wcstol(argv[firstPath + 1], nullptr, 10);
      if (parsed <= 0 || parsed > 32767) { std::wcerr << L"invalid --model\n"; return 2; }
      modelFilter = static_cast<int>(parsed);
      firstPath += 2;
    } else if (flag == L"--ticks") {
      if (firstPath + 1 >= argc) { std::wcerr << usage; return 2; }
      const std::wstring list(argv[firstPath + 1]);
      std::size_t start = 0;
      while (start <= list.size()) {
        const std::size_t comma = list.find(L',', start);
        const std::wstring item = list.substr(start, comma == std::wstring::npos ? std::wstring::npos : comma - start);
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
      firstPath += 2;
    } else {
      std::wcerr << L"unknown option: " << flag << L"\n" << usage;
      return 2;
    }
  }
  if (firstPath >= argc) { std::wcerr << usage; return 2; }
  const std::filesystem::path path(argv[firstPath]);

  tf2::native::DemoHeader header;
  if (!tf2::native::parseDemoHeaderFile(path, header)) {
    std::cout << "brush_entity_probe header=0 error=" << header.error << "\n";
    return 1;
  }
  tf2::native::DemoIndex index;
  if (!tf2::native::indexDemoFile(path, header, index)) {
    std::cout << "brush_entity_probe index=0 error=" << index.error << "\n";
    return 1;
  }
  tf2::native::DemoNetworkSummary summary;
  summary.networkProtocol = header.networkProtocol;
  if (!tf2::native::scanKnownDemoMessages(path, index, summary)) {
    std::cout << "brush_entity_probe scan=0\n";
    return 1;
  }

  std::unordered_map<std::uint16_t, std::string> classNames;
  for (const auto& schema : summary.serverClassSchemas) classNames[schema.id] = schema.name;

  // The snapshot list. Retained checkpoints are the real resolution of the demo,
  // so they are the default; --ticks exists to ask about a tick a caller already
  // cares about, and prints resolved= so the coarseness is never hidden.
  std::vector<SnapshotView> snapshots;
  std::vector<std::vector<tf2::native::EntityState>> ownedStates;  // keeps --ticks answers alive
  if (requestedTicks.empty()) {
    std::vector<const tf2::native::EntityHistoryCheckpoint*> rows;
    for (const auto& checkpoint : summary.entityHistoryCheckpoints) rows.push_back(&checkpoint);
    for (const auto& checkpoint : summary.entityHistoryArchive) rows.push_back(&checkpoint);
    std::sort(rows.begin(), rows.end(), [](const auto* left, const auto* right) { return left->tick < right->tick; });
    for (const auto* checkpoint : rows) {
      SnapshotView view;
      view.tick = checkpoint->tick;
      view.resolved = checkpoint->tick;
      view.states = &checkpoint->states;
      view.classByIndex = &checkpoint->classByIndex;
      snapshots.push_back(view);
    }
  } else {
    for (const auto tick : requestedTicks) {
      std::vector<tf2::native::EntityState> states;
      std::int32_t resolved = tick;
      const auto status = tf2::native::queryEntitySnapshotAtOrBeforeTick(summary, tick, states, &resolved);
      SnapshotView view;
      view.tick = tick;
      view.resolved = resolved;
      ownedStates.push_back(std::move(states));
      view.states = &ownedStates.back();
      view.classByIndex = nullptr;
      snapshots.push_back(view);
      std::cout << "brush_entity_probe query tick=" << tick << " resolved=" << resolved
                << " status=" << static_cast<int>(status) << "\n";
    }
  }

  // Candidate discovery: an entity whose modelprecache path starts with '*'.
  // That is the only place a brush entity's dmodel number is written down; the
  // BSP's own model lump names models by position, so this is the join.
  std::unordered_map<int, Candidate> candidates;
  for (const auto& snapshot : snapshots) {
    if (snapshot.states == nullptr) continue;
    for (std::size_t entity = 0; entity < snapshot.states->size(); ++entity) {
      std::uint16_t modelIndex = 0;
      if (!readModelIndex((*snapshot.states)[entity], modelIndex)) continue;
      const auto precache = summary.modelPrecache.find(modelIndex);
      if (precache == summary.modelPrecache.end() || precache->second.size() < 2 || precache->second[0] != '*') continue;
      const long model = std::strtol(precache->second.c_str() + 1, nullptr, 10);
      if (model <= 0) continue;
      if (modelFilter > 0 && model != modelFilter) continue;
      auto& candidate = candidates[static_cast<int>(entity)];
      if (candidate.entity < 0) {
        candidate.entity = static_cast<int>(entity);
        candidate.model = static_cast<int>(model);
        if (snapshot.classByIndex != nullptr && entity < snapshot.classByIndex->size()) {
          const auto name = classNames.find(static_cast<std::uint16_t>((*snapshot.classByIndex)[entity]));
          if (name != classNames.end()) candidate.className = name->second;
        }
      }
    }
  }

  std::vector<int> order;
  for (const auto& [entity, candidate] : candidates) order.push_back(entity);
  std::sort(order.begin(), order.end());

  std::size_t missing = 0, samples = 0;
  for (const auto& snapshot : snapshots) {
    if (snapshot.states == nullptr) continue;
    for (const int entity : order) {
      auto& candidate = candidates[entity];
      if (static_cast<std::size_t>(entity) >= snapshot.states->size()) { ++missing; continue; }
      float origin[3] = {0.0f, 0.0f, 0.0f};
      bool complete = false;
      if (!readOrigin((*snapshot.states)[static_cast<std::size_t>(entity)], origin, complete)) { ++missing; continue; }
      ++candidate.samples;
      ++samples;
      if (!candidate.haveFirst) {
        candidate.haveFirst = true;
        std::copy(origin, origin + 3, candidate.first);
      }
      std::copy(origin, origin + 3, candidate.last);
      candidate.displacement = distanceBetween(candidate.first, origin);
      std::cout << "brush_entity_probe tick=" << snapshot.tick << " resolved=" << snapshot.resolved
                << " entity=" << candidate.entity << " class=" << candidate.className
                << " model=*" << candidate.model << " origin=" << origin[0] << "," << origin[1] << "," << origin[2]
                << " complete=" << (complete ? 1 : 0) << "\n";
    }
  }

  std::size_t moved = 0;
  float worst = 0.0f;
  for (const int entity : order) {
    const auto& candidate = candidates[entity];
    if (candidate.samples >= 2 && candidate.displacement > 1.0f) ++moved;
    if (candidate.displacement > worst) worst = candidate.displacement;
    std::cout << "brush_entity_probe candidate entity=" << candidate.entity << " class=" << candidate.className
              << " model=*" << candidate.model << " samples=" << candidate.samples
              << " first=" << candidate.first[0] << "," << candidate.first[1] << "," << candidate.first[2]
              << " last=" << candidate.last[0] << "," << candidate.last[1] << "," << candidate.last[2]
              << " displacement=" << candidate.displacement
              << " verdict=" << (candidate.samples < 2 ? "insufficient-samples"
                                  : candidate.displacement > 1.0f ? "moving" : "static")
              << "\n";
  }
  const char* verdict = order.empty() ? "no-brush-entities"
      : (moved > 0 ? "moving" : (samples > order.size() ? "static" : "insufficient-samples"));
  std::cout << "brush_entity_probe summary map=" << header.mapName
            << " snapshots=" << snapshots.size()
            << " candidates=" << order.size()
            << " samples=" << samples
            << " missing=" << missing
            << " moved=" << moved
            << " max_displacement=" << worst
            << " verdict=" << verdict << "\n";
  return 0;
}
