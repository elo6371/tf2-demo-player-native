#include "item_catalog.h"

#include <cctype>
#include <cstdlib>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace tf2::native {
namespace {
struct Node {
  std::unordered_map<std::string, std::string> values;
  std::unordered_map<std::string, Node> children;
};

std::vector<std::string> tokens(std::string_view text) {
  std::vector<std::string> out;
  for (std::size_t i = 0; i < text.size();) {
    while (i < text.size() && (std::isspace(static_cast<unsigned char>(text[i])) || text[i] == '/')) {
      if (text[i] == '/' && i + 1 < text.size() && text[i + 1] == '/') {
        i += 2; while (i < text.size() && text[i] != '\n') ++i;
      } else ++i;
    }
    if (i >= text.size()) break;
    if (text[i] == '{' || text[i] == '}') { out.emplace_back(1, text[i++]); continue; }
    if (text[i] == '"') {
      ++i; std::string value;
      while (i < text.size() && text[i] != '"') {
        if (text[i] == '\\' && i + 1 < text.size()) ++i;
        value.push_back(text[i++]);
      }
      if (i < text.size()) ++i;
      out.push_back(std::move(value));
      continue;
    }
    const auto begin = i;
    while (i < text.size() && !std::isspace(static_cast<unsigned char>(text[i])) && text[i] != '{' && text[i] != '}') ++i;
    out.emplace_back(text.substr(begin, i - begin));
  }
  return out;
}

Node parseNode(const std::vector<std::string>& words, std::size_t& at) {
  Node node;
  while (at < words.size() && words[at] != "}") {
    const std::string key = words[at++];
    if (at >= words.size()) break;
    if (words[at] == "{") { ++at; node.children[key] = parseNode(words, at); if (at < words.size()) ++at; }
    else node.values[key] = words[at++];
  }
  return node;
}

ItemCatalog::Item readItem(const Node& node) {
  ItemCatalog::Item item;
  if (auto it = node.values.find("item_class"); it != node.values.end()) item.itemClass = it->second;
  if (auto it = node.values.find("model_world"); it != node.values.end()) item.modelWorld = it->second;
  if (auto it = node.values.find("model_player"); it != node.values.end()) item.modelPlayer = it->second;
  if (auto it = node.values.find("prefab"); it != node.values.end()) item.prefab = it->second;
  if (auto it = node.children.find("model_player_per_class"); it != node.children.end()) item.modelPerClass = it->second.values;
  return item;
}

void merge(ItemCatalog::Item& into, const ItemCatalog::Item& from) {
  if (into.itemClass.empty()) into.itemClass = from.itemClass;
  if (into.modelWorld.empty()) into.modelWorld = from.modelWorld;
  if (into.modelPlayer.empty()) into.modelPlayer = from.modelPlayer;
  for (const auto& pair : from.modelPerClass) into.modelPerClass.emplace(pair);
}
}

ItemCatalog ItemCatalog::parse(const std::string& text) {
  ItemCatalog catalog;
  const auto words = tokens(text);
  std::size_t at = 0;
  Node root;
  while (at + 1 < words.size()) {
    const auto key = words[at++];
    if (words[at] != "{") { ++at; continue; }
    ++at; root.children[key] = parseNode(words, at); if (at < words.size()) ++at;
  }
  const auto game = root.children.find("items_game");
  if (game == root.children.end()) return catalog;
  if (const auto it = game->second.children.find("prefabs"); it != game->second.children.end())
    for (const auto& pair : it->second.children) catalog.prefabs_[pair.first] = readItem(pair.second);
  if (const auto it = game->second.children.find("items"); it != game->second.children.end()) {
    for (const auto& pair : it->second.children) {
      char* end = nullptr; const auto index = std::strtol(pair.first.c_str(), &end, 10);
      if (!end || *end != '\0' || index <= 0) continue;
      catalog.items_[static_cast<std::int32_t>(index)] = readItem(pair.second);
    }
  }
  return catalog;
}

ItemModelResolution ItemCatalog::resolve(std::int32_t itemDefIndex, const std::string& playerClass) const {
  ItemModelResolution result; result.itemDefIndex = itemDefIndex;
  const auto found = items_.find(itemDefIndex);
  if (found == items_.end()) return result;
  Item item = found->second;
  std::vector<std::string> queue;
  for (std::size_t start = 0; start < item.prefab.size();) {
    while (start < item.prefab.size() && std::isspace(static_cast<unsigned char>(item.prefab[start]))) ++start;
    const auto end = item.prefab.find_first_of(" \t\r\n", start);
    if (start < item.prefab.size()) queue.push_back(item.prefab.substr(start, end == std::string::npos ? end : end - start));
    if (end == std::string::npos) break; start = end;
  }
  for (std::size_t i = 0; i < queue.size() && i < 32; ++i) {
    const auto prefab = prefabs_.find(queue[i]); if (prefab == prefabs_.end()) continue;
    merge(item, prefab->second);
    for (std::size_t start = 0; start < prefab->second.prefab.size();) {
      while (start < prefab->second.prefab.size() && std::isspace(static_cast<unsigned char>(prefab->second.prefab[start]))) ++start;
      const auto end = prefab->second.prefab.find_first_of(" \t\r\n", start);
      if (start < prefab->second.prefab.size()) queue.push_back(prefab->second.prefab.substr(start, end == std::string::npos ? end : end - start));
      if (end == std::string::npos) break; start = end;
    }
  }
  result.itemClass = item.itemClass;
  if (!item.modelWorld.empty()) result.modelPath = item.modelWorld;
  else if (!playerClass.empty()) {
    const auto model = item.modelPerClass.find(playerClass);
    if (model != item.modelPerClass.end()) result.modelPath = model->second;
  }
  if (result.modelPath.empty()) result.modelPath = item.modelPlayer;
  result.found = !result.modelPath.empty();
  return result;
}
} // namespace tf2::native
