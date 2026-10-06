#include "animation_decoder.h"
#include "vpk_archive.h"

#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace {
bool parentChain() {
  const float h = std::sqrt(0.5f);
  const std::vector<std::int32_t> parents{-1, 0};
  const std::vector<std::array<float, 3>> pos{{{0, 0, 0}, {1, 0, 0}}};
  const std::vector<std::array<float, 4>> identity{{{0, 0, 0, 1}, {0, 0, 0, 1}}};
  std::vector<std::array<float, 4>> turned = identity;
  turned[0] = {0, 0, h, h};
  std::vector<std::array<float, 3>> rest, moved;
  std::string reason;
  if (!tf2::native::composeLocalToModel(parents, pos, identity, rest, reason) ||
      !tf2::native::composeLocalToModel(parents, pos, turned, moved, reason)) return false;
  return rest.size() == 2 && moved.size() == 2 &&
    std::fabs(rest[1][0] - 1.0f) < 1e-4f && std::fabs(moved[1][0]) < 1e-4f &&
    std::fabs(moved[1][1] - 1.0f) < 1e-4f;
}
}

int main() {
  const char* env = std::getenv("TF_ROOT");
  const std::filesystem::path root = env ? env : "D:/SteamLibrary/steamapps/common/Team Fortress 2/tf";
  tf2::native::VpkArchive archive;
  std::string error;
  const bool opened = archive.open(root / "tf2_misc_dir.vpk", &error);
  tf2::native::AnimationModel scout, soldier;
  bool scoutOk = false, soldierOk = false, motion = false;
  if (opened) {
    std::string readError;
    const auto scoutBytes = archive.read("models/player/scout_animations.mdl", &readError);
    const auto soldierBytes = archive.read("models/player/soldier_animations.mdl", &readError);
    scoutOk = tf2::native::decodeAnimationModel(scoutBytes.data(), scoutBytes.size(), scout) == tf2::native::AnimationStatus::Ok &&
      !scout.bones.empty() && !scout.animations.empty();
    soldierOk = tf2::native::decodeAnimationModel(soldierBytes.data(), soldierBytes.size(), soldier) == tf2::native::AnimationStatus::Ok &&
      !soldier.bones.empty() && !soldier.animations.empty();
    if (scoutOk) {
      for (std::size_t i = 0; i < scout.sequences.size(); ++i) {
        const auto a = tf2::native::sampleAnimation(scout, static_cast<std::int32_t>(i), 0, 30.0f);
        const auto b = tf2::native::sampleAnimation(scout, static_cast<std::int32_t>(i), 1, 30.0f);
        if (a.status == tf2::native::AnimationStatus::Ok && b.status == tf2::native::AnimationStatus::Ok &&
            a.modelPosition.size() == b.modelPosition.size()) {
          for (std::size_t j = 0; j < a.modelPosition.size(); ++j)
            if (std::memcmp(a.modelPosition[j].data(), b.modelPosition[j].data(), sizeof(a.modelPosition[j])) != 0) { motion = true; break; }
        }
        if (motion) break;
      }
    }
  }
  const bool pass = parentChain() && opened && scoutOk && soldierOk && motion;
  std::cout << "parent_chain=" << (parentChain() ? "pass" : "fail")
            << " opened=" << (opened ? "true" : "false")
            << " scout=" << (scoutOk ? "pass" : "fail")
            << " soldier=" << (soldierOk ? "pass" : "fail")
            << " motion=" << (motion ? "pass" : "fail") << "\n";
  if (!pass && !error.empty()) std::cerr << error << "\n";
  return pass ? 0 : 1;
}
