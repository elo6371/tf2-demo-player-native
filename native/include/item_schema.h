#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace tf2::native {

struct ItemSchemaEntry {
  int itemDefinitionIndex = -1;
  std::string key;
  std::string itemName;
  std::string itemClass;
  std::string modelPlayer;
  std::string modelWorld;
  std::unordered_map<std::string, std::string> modelPlayerPerClass;
  std::unordered_map<std::string, std::string> visuals;
  bool hasExplicitModel = false;
  std::string prefab;
};

struct ItemSchemaModelCandidates {
  int itemDefinitionIndex = -1;
  std::string itemClass;
  std::string modelPlayer;
  std::string modelWorld;
  std::unordered_map<std::string, std::string> modelPlayerPerClass;
};

struct PaintKitEntry {
  int id = -1;
  std::string name;
  std::string description;
  std::unordered_map<std::string, std::string> fields;
};

enum class AppearanceDataStatus { Unknown, Declared, Missing, Unavailable };

struct AppearanceVisualDeclaration {
  std::string key;
  std::string value;
  AppearanceDataStatus resourceStatus = AppearanceDataStatus::Unknown;
};

struct AppearanceResolution {
  int itemDefinitionIndex = -1;
  int paintKitId = -1;
  int skin = -1;
  AppearanceDataStatus itemStatus = AppearanceDataStatus::Unknown;
  AppearanceDataStatus paintKitStatus = AppearanceDataStatus::Unknown;
  AppearanceDataStatus manifestStatus = AppearanceDataStatus::Unavailable;
  AppearanceDataStatus replacementStatus = AppearanceDataStatus::Unavailable;
  std::vector<AppearanceVisualDeclaration> visuals;
};

class ItemSchema final {
public:
  ItemSchema() = default;
  ItemSchema(ItemSchema&&) noexcept = default;
  ItemSchema& operator=(ItemSchema&&) noexcept = default;
  ItemSchema(const ItemSchema&) = delete;
  ItemSchema& operator=(const ItemSchema&) = delete;

  static std::unique_ptr<ItemSchema> load(const std::filesystem::path& path,
                                          std::string& error);
  const ItemSchemaEntry* find(int itemDefinitionIndex) const;
  ItemSchemaModelCandidates modelCandidates(int itemDefinitionIndex) const;
  const PaintKitEntry* findPaintKit(int paintKitId) const;
  AppearanceResolution resolveAppearance(int itemDefinitionIndex, int paintKitId, int skin) const;
  std::size_t paintKitCount() const { return paintKits_.size(); }
  std::size_t size() const { return entries_.size(); }
  const std::filesystem::path& sourcePath() const { return sourcePath_; }

private:
  std::filesystem::path sourcePath_;
  std::unordered_map<int, ItemSchemaEntry> entries_;
  std::unordered_map<std::string, ItemSchemaEntry> prefabs_;
  std::unordered_map<int, PaintKitEntry> paintKits_;
};

} // namespace tf2::native
