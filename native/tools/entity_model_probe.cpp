#include "asset_root.h"
#include "demo_header.h"
#include "entity_model.h"
#include "model_loader.h"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

namespace {

int fail(const std::string& message) {
  std::cout << "{\"ok\":false,\"error\":\"" << message << "\"}\n";
  return 1;
}

bool runSelfTest() {
  tf2::native::EntityState state;
  tf2::native::EntityPropertyValue origin;
  origin.type = tf2::native::SendPropType::Vector;
  origin.x = 100.0f; origin.y = 200.0f; origin.z = 32.0f;
  state.properties["DT_BaseEntity.m_vecOrigin"] = origin;
  tf2::native::EntityPropertyValue team;
  team.type = tf2::native::SendPropType::Int;
  team.intValue = 2;
  state.properties["DT_TFPlayerResource.m_iTeamNum"] = team;
  tf2::native::EntityPropertyValue playerClass;
  playerClass.type = tf2::native::SendPropType::Int;
  playerClass.intValue = 1;
  state.properties["DT_TFPlayer.m_iClass"] = playerClass;
  const auto transform = tf2::native::EntityModelResolver::extractTransform(state);
  if (!transform.hasOrigin || transform.origin[0] != 100.0f || transform.origin[2] != 32.0f) return false;
  if (!transform.hasTeam || transform.team != 2) return false;
  if (!transform.hasPlayerClass || transform.playerClass != 1) return false;
  if (tf2::native::EntityModelResolver::defaultPlayerModelPath(1) != "models/player/scout.mdl") return false;

  std::vector<tf2::native::AssetReference> malformed;
  tf2::native::AssetReference empty;
  empty.hasModelPath = true;
  malformed.push_back(empty);
  tf2::native::AssetReference bad;
  bad.hasModelPath = true;
  bad.modelPath = "models/not/a/real_model.mdl";
  bad.entityIndex = 3;
  malformed.push_back(bad);
  tf2::native::AssetRoot missingRoot;
  const auto missing = tf2::native::ModelLoader::buildRenderRequests(missingRoot, malformed, nullptr);
  if (missing.size() != 1) return false;
  if (missing[0].renderable) return false;

  std::vector<tf2::native::ModelRenderRequest> flood;
  flood.resize(3000);
  for (std::size_t i = 0; i < flood.size(); ++i) {
    flood[i].entityIndex = static_cast<std::uint16_t>(i > 0xffffu ? 0xffffu : i);
    flood[i].modelPath = "models/player/scout.mdl";
    flood[i].cacheKey = "models/player/scout.mdl#1";
  }
  std::vector<tf2::native::EntityState> states(4);
  states[3] = state;
  const auto bounded = tf2::native::EntityModelResolver::buildInstances(flood, states, {}, 2048);
  if (bounded.size() > tf2::native::EntityModelResolver::kMaxInstances) return false;

  const auto near = [](float actual, float expected) {
    return std::fabs(actual - expected) <= 1.0e-4f;
  };
  const auto applyPosition = [](const tf2::native::EntityModelInstanceRows& rows, float x, float y, float z, float out[3]) {
    const float point[4] = {x, y, z, 1.0f};
    for (int row = 0; row < 3; ++row) {
      out[row] = rows.positionRows[row * 4] * point[0] + rows.positionRows[row * 4 + 1] * point[1]
        + rows.positionRows[row * 4 + 2] * point[2] + rows.positionRows[row * 4 + 3] * point[3];
    }
  };
  const auto applyNormal = [](const tf2::native::EntityModelInstanceRows& rows, float x, float y, float z, float out[3]) {
    for (int row = 0; row < 3; ++row) {
      out[row] = rows.normalRows[row * 3] * x + rows.normalRows[row * 3 + 1] * y + rows.normalRows[row * 3 + 2] * z;
    }
  };
  tf2::native::EntityModelWorldMap world;
  world.centerX = 10.0f;
  world.centerY = 20.0f;
  world.minZ = 5.0f;
  world.horizontalScale = 0.01f;
  world.zScale = 0.02f;
  const float worldOrigin[3] = {100.0f, 200.0f, 50.0f};
  tf2::native::EntityModelInstanceRows rows;
  if (!tf2::native::buildEntityModelInstanceRows(worldOrigin, nullptr, false, world, rows)) return false;
  float mapped[3] = {};
  applyPosition(rows, 1.0f, 2.0f, 3.0f, mapped);
  if (!near(mapped[0], 0.91f) || !near(mapped[1], 1.82f) || !near(mapped[2], 1.06f)) return false;
  float normal[3] = {};
  applyNormal(rows, 0.0f, 1.0f, 0.0f, normal);
  if (!near(normal[0], 0.0f) || !near(normal[1], 1.0f) || !near(normal[2], 0.0f)) return false;
  if (!near(rows.positionRows[0], world.horizontalScale) || !near(rows.normalRows[0], 1.0f)) return false;

  const float yaw90[3] = {0.0f, 90.0f, 0.0f};
  const float yawOrigin[3] = {10.0f, 0.0f, 0.0f};
  tf2::native::EntityModelWorldMap unit;
  unit.horizontalScale = 1.0f;
  unit.zScale = 1.0f;
  tf2::native::EntityModelInstanceRows yawRows;
  if (!tf2::native::buildEntityModelInstanceRows(yawOrigin, yaw90, true, unit, yawRows)) return false;
  applyPosition(yawRows, 1.0f, 0.0f, 0.0f, mapped);
  if (!near(mapped[0], 10.0f) || !near(mapped[1], 1.0f) || !near(mapped[2], 0.1f)) return false;
  applyNormal(yawRows, 1.0f, 0.0f, 0.0f, normal);
  if (!near(normal[0], 0.0f) || !near(normal[1], 1.0f) || !near(normal[2], 0.0f)) return false;

  const float pitch90[3] = {90.0f, 0.0f, 0.0f};
  tf2::native::EntityModelInstanceRows pitchRows;
  const float zero[3] = {};
  if (!tf2::native::buildEntityModelInstanceRows(zero, pitch90, true, unit, pitchRows)) return false;
  applyPosition(pitchRows, 0.0f, 0.0f, 1.0f, mapped);
  if (!near(mapped[0], 1.0f) || !near(mapped[1], 0.0f) || !near(mapped[2], 0.1f)) return false;

  const float badOrigin[3] = {0.0f, 0.0f, std::numeric_limits<float>::quiet_NaN()};
  if (tf2::native::buildEntityModelInstanceRows(badOrigin, nullptr, false, unit, rows)) return false;
  return true;
}

} // namespace

int main(int argc, char** argv) {
  std::string tfRoot;
  std::string demoPath;
  bool selfTest = false;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--self-test") selfTest = true;
    else if (arg == "--tf-root" && i + 1 < argc) tfRoot = argv[++i];
    else if (arg == "--demo" && i + 1 < argc) demoPath = argv[++i];
  }
  if (selfTest && !runSelfTest()) return fail("self-test failed");

  std::size_t uniqueRenderable = 0;
  std::size_t vpkRenderable = 0;
  std::size_t cacheHits = 0;
  std::size_t inspections = 0;
  std::size_t scoutVertices = 0;
  std::uint32_t scoutChecksum = 0;
  std::size_t instanceCount = 0;
  std::size_t playerFallbacks = 0;
  std::string scoutKey;
  bool duplicateStable = true;

  if (!tfRoot.empty()) {
    const auto assets = tf2::native::AssetRoot::fromPath(tfRoot);
    if (!assets.valid()) return fail("tf-root is not a valid AssetRoot");
    std::vector<tf2::native::AssetReference> references;
    const char* models[] = {
      "models/player/scout.mdl",
      "models/player/soldier.mdl",
      "models/player/scout.mdl",
      "models/weapons/w_models/w_rocketlauncher.mdl",
      "models/not/a/real_model.mdl",
    };
    for (int i = 0; i < 5; ++i) {
      tf2::native::AssetReference reference;
      reference.entityIndex = static_cast<std::uint16_t>(i + 1);
      reference.hasModelPath = true;
      reference.modelPath = models[i];
      reference.className = i < 2 ? "CTFPlayer" : "CBaseAnimating";
      references.push_back(reference);
    }
    tf2::native::ModelRenderRequestStats stats;
    const auto first = tf2::native::ModelLoader::buildRenderRequests(assets, references, nullptr, &stats);
    const auto second = tf2::native::ModelLoader::buildRenderRequests(assets, references, nullptr, nullptr);
    inspections = stats.inspections;
    cacheHits = stats.inspectionCacheHits;
    if (first.size() != second.size()) duplicateStable = false;
    for (std::size_t i = 0; i < first.size() && i < second.size(); ++i) {
      if (first[i].cacheKey != second[i].cacheKey) duplicateStable = false;
      if (first[i].renderable) ++uniqueRenderable;
      if (first[i].renderable && first[i].inspectedFromVpk) ++vpkRenderable;
      if (first[i].modelPath.find("scout.mdl") != std::string::npos && first[i].renderable) {
        scoutKey = first[i].cacheKey;
        scoutChecksum = first[i].checksum;
        std::string error;
        auto metadata = first[i].inspection.metadata;
        if (tf2::native::ModelLoader::buildBindPoseMeshLod0(metadata, error)) {
          scoutVertices = metadata.bindPoseVertices.size();
        } else {
          scoutKey += ";mesh=" + error + ";idx=" + std::to_string(metadata.indices.size())
            + ";renderIdx=" + std::to_string(metadata.renderIndices.size())
            + ";verts=" + std::to_string(metadata.vertices.size())
            + ";vtxFail=" + metadata.vtxDiagnostics.failureReason
            + ";vtxDesc=" + std::to_string(metadata.vtxDiagnostics.vtxDescriptorCount);
          for (const auto& diagnostic : first[i].inspection.diagnostics) {
            if (scoutKey.size() < 900) scoutKey += "|" + diagnostic;
          }
        }
      }
    }
    if (first.size() >= 3 && first[0].cacheKey != first[2].cacheKey) duplicateStable = false;
    if (inspections > 4) duplicateStable = false;

    std::vector<tf2::native::EntityState> states(8);
    tf2::native::EntityPropertyValue origin;
    origin.type = tf2::native::SendPropType::Vector;
    origin.x = 10; origin.y = 20; origin.z = 30;
    states[1].classId = 0;
    states[1].properties["DT_BaseEntity.m_vecOrigin"] = origin;
    tf2::native::EntityPropertyValue playerClass;
    playerClass.type = tf2::native::SendPropType::Int;
    playerClass.intValue = 3;
    states[6].classId = 0;
    states[6].properties["DT_BaseEntity.m_vecOrigin"] = origin;
    states[6].properties["DT_TFPlayer.m_iClass"] = playerClass;
    std::vector<tf2::native::ServerClassSchema> schemas(1);
    schemas[0].name = "CTFPlayer";
    const auto instances = tf2::native::EntityModelResolver::buildInstances(first, states, schemas, 64);
    instanceCount = instances.size();
    for (const auto& instance : instances) if (instance.playerClassFallback) ++playerFallbacks;
  }

  if (!demoPath.empty() && !tfRoot.empty()) {
    tf2::native::DemoHeader header;
    tf2::native::DemoIndex index;
    tf2::native::DemoNetworkSummary summary;
    if (!tf2::native::parseDemoHeaderFile(demoPath, header)) return fail("demo header parse failed");
    tf2::native::indexDemoFile(demoPath, header, index);
    // The protocol has to be copied in before the scan: svc_CreateStringTable
    // reads its payload length as a varint above protocol 23 and as a fixed
    // 20-bit field at or below it. Leaving this at 0 took the wrong branch and
    // produced a plausible-looking summary -- 673 asset references and 0 render
    // requests -- that had nothing to do with the demo's actual contents.
    summary.networkProtocol = header.networkProtocol;
    if (!tf2::native::scanKnownDemoMessages(demoPath, index, summary)) {
      return fail("demo scan failed (network protocol not set?)");
    }
    tf2::native::buildAssetReferenceList(summary, summary.assetReferences);
    const auto assets = tf2::native::AssetRoot::fromPath(tfRoot);
    tf2::native::ModelRenderRequestStats stats;
    const auto requests = tf2::native::ModelLoader::buildRenderRequests(
      assets, summary.assetReferences, nullptr, &stats);
    std::size_t demoRenderable = 0;
    for (const auto& request : requests) if (request.renderable) ++demoRenderable;
    std::cout << "{\"ok\":true"
      << ",\"selfTest\":" << (selfTest ? "true" : "false")
      << ",\"demo\":\"" << demoPath << "\""
      << ",\"assetRefs\":" << summary.assetReferences.size()
      << ",\"requests\":" << requests.size()
      << ",\"demoRenderable\":" << demoRenderable
      << ",\"inspections\":" << stats.inspections
      << ",\"inspectionCacheHits\":" << stats.inspectionCacheHits
      << ",\"vpkExtracts\":" << stats.vpkExtracts
      << ",\"uniqueRenderable\":" << uniqueRenderable
      << ",\"vpkRenderable\":" << vpkRenderable
      << ",\"scoutVertices\":" << scoutVertices
      << ",\"scoutChecksum\":" << scoutChecksum
      << ",\"duplicateStable\":" << (duplicateStable ? "true" : "false")
      << ",\"instanceCount\":" << instanceCount
      << ",\"playerFallbacks\":" << playerFallbacks
      << "}\n";
    return (selfTest && demoRenderable + uniqueRenderable == 0) ? 2 : 0;
  }

  std::cout << "{\"ok\":true"
    << ",\"selfTest\":" << (selfTest ? "true" : "false")
    << ",\"uniqueRenderable\":" << uniqueRenderable
    << ",\"vpkRenderable\":" << vpkRenderable
    << ",\"inspections\":" << inspections
    << ",\"inspectionCacheHits\":" << cacheHits
    << ",\"scoutVertices\":" << scoutVertices
    << ",\"scoutChecksum\":" << scoutChecksum
    << ",\"scoutKey\":\"" << scoutKey << "\""
    << ",\"duplicateStable\":" << (duplicateStable ? "true" : "false")
    << ",\"instanceCount\":" << instanceCount
    << ",\"playerFallbacks\":" << playerFallbacks
    << "}\n";
  return 0;
}
