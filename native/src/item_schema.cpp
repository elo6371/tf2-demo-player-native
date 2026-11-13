#include "item_schema.h"

#include <charconv>
#include <cctype>
#include <fstream>
#include <sstream>
#include <string_view>

namespace tf2::native {
namespace {

struct Node {
  std::string key;
  std::string value;
  std::vector<Node> children;
};

class Parser {
public:
  explicit Parser(std::string text) : text_(std::move(text)) {}

  bool parse(Node& root, std::string& error) {
    root = {};
    skipSpaceAndComments();
    while (pos_ < text_.size()) {
      Node node;
      if (!parseNode(node, error)) return false;
      root.children.push_back(std::move(node));
      skipSpaceAndComments();
    }
    return true;
  }

private:
  bool token(std::string& out, std::string& error) {
    skipSpaceAndComments();
    if (pos_ >= text_.size()) { error = "unexpected end of KeyValues"; return false; }
    if (text_[pos_] == '"') {
      ++pos_;
      out.clear();
      while (pos_ < text_.size()) {
        const char c = text_[pos_++];
        if (c == '"') return true;
        if (c == '\\' && pos_ < text_.size()) {
          const char escaped = text_[pos_++];
          out.push_back(escaped == 'n' ? '\n' : escaped);
        } else out.push_back(c);
      }
      error = "unterminated quoted token";
      return false;
    }
    const auto begin = pos_;
    while (pos_ < text_.size() && text_[pos_] != '{' && text_[pos_] != '}'
           && text_[pos_] != ' ' && text_[pos_] != '\t' && text_[pos_] != '\r'
           && text_[pos_] != '\n') ++pos_;
    if (begin == pos_) { error = "empty KeyValues token"; return false; }
    out.assign(text_, begin, pos_ - begin);
    return true;
  }

  void skipSpaceAndComments() {
    for (;;) {
      while (pos_ < text_.size() && (text_[pos_] == ' ' || text_[pos_] == '\t'
             || text_[pos_] == '\r' || text_[pos_] == '\n')) ++pos_;
      if (pos_ + 1 < text_.size() && text_[pos_] == '/' && text_[pos_ + 1] == '/') {
        pos_ += 2;
        while (pos_ < text_.size() && text_[pos_] != '\n') ++pos_;
        continue;
      }
      return;
    }
  }

  bool parseNode(Node& node, std::string& error) {
    if (!token(node.key, error)) return false;
    skipSpaceAndComments();
    if (pos_ < text_.size() && text_[pos_] == '{') {
      ++pos_;
      for (;;) {
        skipSpaceAndComments();
        if (pos_ >= text_.size()) { error = "unterminated KeyValues block"; return false; }
        if (text_[pos_] == '}') { ++pos_; return true; }
        Node child;
        if (!parseNode(child, error)) return false;
        node.children.push_back(std::move(child));
      }
    }
    return token(node.value, error);
  }

  std::string text_;
  std::size_t pos_ = 0;
};

const Node* child(const Node& node, std::string_view name) {
  for (const auto& c : node.children) if (c.key == name) return &c;
  return nullptr;
}

std::string value(const Node* node, std::string_view name) {
  const auto* found = node ? child(*node, name) : nullptr;
  return found && found->children.empty() ? found->value : std::string{};
}

void collectValues(const Node* node, std::unordered_map<std::string, std::string>& output) {
  if (!node) return;
  for (const auto& c : node->children) if (c.children.empty()) output[c.key] = c.value;
}

void collectLeaves(const Node* node, const std::string& prefix,
                   std::unordered_map<std::string, std::string>& output) {
  if (!node) return;
  for (const auto& childNode : node->children) {
    const std::string key = prefix.empty() ? childNode.key : prefix + "." + childNode.key;
    if (childNode.children.empty()) output[key] = childNode.value;
    else collectLeaves(&childNode, key, output);
  }
}

PaintKitEntry readPaintKit(const Node& node, int id) {
  PaintKitEntry result;
  result.id = id;
  result.name = value(&node, "name");
  result.description = value(&node, "description_string");
  collectValues(&node, result.fields);
  return result;
}

bool parseIndex(std::string_view text, int& result) {
  const auto* begin = text.data();
  const auto* end = begin + text.size();
  auto parsed = std::from_chars(begin, end, result);
  return parsed.ec == std::errc{} && parsed.ptr == end;
}

} // namespace

std::unique_ptr<ItemSchema> ItemSchema::load(const std::filesystem::path& path,
                                             std::string& error) {
  std::ifstream input(path, std::ios::binary);
  if (!input) { error = "cannot open items_game.txt: " + path.string(); return nullptr; }
  std::ostringstream data;
  data << input.rdbuf();
  Node root;
  Parser parser(data.str());
  if (!parser.parse(root, error)) return nullptr;
  const Node* game = child(root, "items_game");
  const Node* items = game ? child(*game, "items") : nullptr;
  if (!items) { error = "items_game.txt has no top-level items block"; return nullptr; }

  auto schema = std::unique_ptr<ItemSchema>(new ItemSchema());
  schema->sourcePath_ = path;
  if (const Node* prefabs = game ? child(*game, "prefabs") : nullptr) {
    for (const auto& prefab : prefabs->children) {
      ItemSchemaEntry entry;
      entry.key = prefab.key;
      entry.itemClass = value(&prefab, "item_class");
      entry.modelPlayer = value(&prefab, "model_player");
      entry.modelWorld = value(&prefab, "model_world");
      entry.prefab = value(&prefab, "prefab");
      collectValues(child(prefab, "model_player_per_class"), entry.modelPlayerPerClass);
      schema->prefabs_[prefab.key] = std::move(entry);
    }
  }
  if (const Node* paintKits = game ? child(*game, "paint_kits") : nullptr) {
    for (const auto& paintKit : paintKits->children) {
      int id = -1;
      if (parseIndex(paintKit.key, id)) schema->paintKits_[id] = readPaintKit(paintKit, id);
    }
  }
  for (const auto& item : items->children) {
    int id = -1;
    if (!parseIndex(item.key, id)) continue;
    ItemSchemaEntry entry;
    entry.itemDefinitionIndex = id;
    entry.key = item.key;
    entry.itemName = value(&item, "item_name");
    entry.itemClass = value(&item, "item_class");
    entry.modelPlayer = value(&item, "model_player");
    entry.modelWorld = value(&item, "model_world");
    entry.prefab = value(&item, "prefab");
    collectValues(child(item, "model_player_per_class"), entry.modelPlayerPerClass);
    collectLeaves(child(item, "visuals"), {}, entry.visuals);
    entry.hasExplicitModel = !entry.modelPlayer.empty() || !entry.modelWorld.empty()
      || !entry.modelPlayerPerClass.empty();
    schema->entries_[id] = std::move(entry);
  }
  if (schema->entries_.empty()) { error = "items block contains no numeric item definitions"; return nullptr; }
  return schema;
}

const ItemSchemaEntry* ItemSchema::find(int itemDefinitionIndex) const {
  const auto it = entries_.find(itemDefinitionIndex);
  return it == entries_.end() ? nullptr : &it->second;
}

ItemSchemaModelCandidates ItemSchema::modelCandidates(int itemDefinitionIndex) const {
  ItemSchemaModelCandidates result;
  result.itemDefinitionIndex = itemDefinitionIndex;
  const auto found = entries_.find(itemDefinitionIndex);
  if (found == entries_.end()) return result;
  ItemSchemaEntry merged = found->second;
  std::vector<std::string> queue;
  const auto append = [&](const std::string& value) {
    std::size_t start = 0;
    while (start < value.size()) {
      while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start]))) ++start;
      const auto end = value.find_first_of(" \t\r\n", start);
      if (start < value.size()) queue.push_back(value.substr(start, end == std::string::npos ? end : end - start));
      if (end == std::string::npos) break;
      start = end;
    }
  };
  append(merged.prefab);
  for (std::size_t i = 0; i < queue.size() && i < 32; ++i) {
    const auto prefab = prefabs_.find(queue[i]);
    if (prefab == prefabs_.end()) continue;
    const auto& source = prefab->second;
    if (merged.itemClass.empty()) merged.itemClass = source.itemClass;
    if (merged.modelPlayer.empty()) merged.modelPlayer = source.modelPlayer;
    if (merged.modelWorld.empty()) merged.modelWorld = source.modelWorld;
    for (const auto& model : source.modelPlayerPerClass) merged.modelPlayerPerClass.emplace(model);
    append(source.prefab);
  }
  result.itemClass = std::move(merged.itemClass);
  result.modelPlayer = std::move(merged.modelPlayer);
  result.modelWorld = std::move(merged.modelWorld);
  result.modelPlayerPerClass = std::move(merged.modelPlayerPerClass);
  return result;
}

const PaintKitEntry* ItemSchema::findPaintKit(int paintKitId) const {
  const auto found = paintKits_.find(paintKitId);
  return found == paintKits_.end() ? nullptr : &found->second;
}

AppearanceResolution ItemSchema::resolveAppearance(int itemDefinitionIndex, int paintKitId, int skin) const {
  AppearanceResolution result;
  result.itemDefinitionIndex = itemDefinitionIndex;
  result.paintKitId = paintKitId;
  result.skin = skin;
  if (itemDefinitionIndex >= 0) {
    const auto item = entries_.find(itemDefinitionIndex);
    result.itemStatus = item == entries_.end() ? AppearanceDataStatus::Missing : AppearanceDataStatus::Declared;
    if (item != entries_.end()) {
      result.visuals.reserve(item->second.visuals.size());
      for (const auto& visual : item->second.visuals)
        result.visuals.push_back({visual.first, visual.second, AppearanceDataStatus::Unknown});
    }
  }
  if (paintKitId >= 0) {
    result.paintKitStatus = paintKits_.find(paintKitId) == paintKits_.end()
      ? AppearanceDataStatus::Missing : AppearanceDataStatus::Declared;
  }
  return result;
}

} // namespace tf2::native
