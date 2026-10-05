#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>

namespace tf2::native {

struct ItemModelResolution {
  std::int32_t itemDefIndex = -1;
  std::string itemClass;
  std::string modelPath;
  bool found = false;
};

/** Read-only index of scripts/items/items_game.txt model declarations. */
class ItemCatalog final {
public:
  static ItemCatalog parse(const std::string& text);

  ItemModelResolution resolve(std::int32_t itemDefIndex,
    const std::string& playerClass = {}) const;
  std::size_t size() const { return items_.size(); }

  // Kept public for the parser's small, allocation-free merge helpers.
  struct Item {
    std::string itemClass;
    std::string modelWorld;
    std::string modelPlayer;
    std::unordered_map<std::string, std::string> modelPerClass;
    std::string prefab;
  };
private:
  std::unordered_map<std::int32_t, Item> items_;
  std::unordered_map<std::string, Item> prefabs_;
};

} // namespace tf2::native
