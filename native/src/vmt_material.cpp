#include "vmt_material.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <sstream>
#include <utility>

namespace tf2::native {
namespace {
struct Token { enum class Kind { Text, Open, Close }; Kind kind; std::string value; };

std::string lower(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return value;
}

std::vector<Token> tokenize(const std::string& text) {
  std::vector<Token> out;
  std::size_t i = 0;
  while (i < text.size()) {
    if (std::isspace(static_cast<unsigned char>(text[i]))) { ++i; continue; }
    if (text[i] == '/' && i + 1 < text.size() && text[i + 1] == '/') {
      i += 2; while (i < text.size() && text[i] != '\n') ++i; continue;
    }
    if (text[i] == '/' && i + 1 < text.size() && text[i + 1] == '*') {
      const auto end = text.find("*/", i + 2); i = end == std::string::npos ? text.size() : end + 2; continue;
    }
    if (text[i] == '{') { out.push_back({Token::Kind::Open, "{"}); ++i; continue; }
    if (text[i] == '}') { out.push_back({Token::Kind::Close, "}"}); ++i; continue; }
    std::string value;
    if (text[i] == '"') {
      ++i;
      while (i < text.size() && text[i] != '"') {
        // A backslash in a VMT value is a path separator, not an escape. TF2
        // ships VMTs whose $basetexture reads
        // `models\props_gameplay/resupply_locker`; consuming the backslash
        // turned that into `modelsprops_gameplay/resupply_locker`, which
        // resolves nowhere, so the model silently kept the world atlas. Only a
        // backslash in front of a quote or another backslash is consumed, which
        // keeps `\"` working for the rare file that uses it.
        if (text[i] == '\\' && i + 1 < text.size()
            && (text[i + 1] == '"' || text[i + 1] == '\\')) {
          ++i;
          value.push_back(text[i]);
        } else value.push_back(text[i]);
        ++i;
      }
      if (i < text.size()) ++i;
    } else {
      while (i < text.size() && !std::isspace(static_cast<unsigned char>(text[i])) && text[i] != '{' && text[i] != '}') value.push_back(text[i++]);
    }
    if (!value.empty()) out.push_back({Token::Kind::Text, std::move(value)});
  }
  return out;
}

bool readObject(const std::vector<Token>& tokens, std::size_t& cursor, std::unordered_map<std::string, VmtValue>& result, std::string& error) {
  while (cursor < tokens.size()) {
    const Token& key = tokens[cursor++];
    if (key.kind == Token::Kind::Close) return true;
    if (key.kind != Token::Kind::Text || cursor >= tokens.size()) { error = "VMT field key/value is truncated"; return false; }
    const Token& value = tokens[cursor++];
    const std::string name = lower(key.value);
    if (value.kind == Token::Kind::Open) {
      VmtValue nested; nested.object = true;
      if (!readObject(tokens, cursor, nested.fields, error)) return false;
      result[name] = std::move(nested);
    } else if (value.kind == Token::Kind::Text) {
      VmtValue scalar; scalar.text = value.value; result[name] = std::move(scalar);
    } else { error = "VMT field has an unexpected closing brace"; return false; }
  }
  error = "VMT object is missing a closing brace";
  return false;
}

std::string scalar(const std::unordered_map<std::string, VmtValue>& fields, const char* key) {
  const auto it = fields.find(key); return it != fields.end() && !it->second.object ? it->second.text : std::string{};
}

bool enabled(const std::string& value) {
  if (value.empty()) return false;
  char* end = nullptr; const double number = std::strtod(value.c_str(), &end);
  if (end && *end == '\0') return number != 0.0;
  return true;
}
}

bool VmtParser::parse(const std::string& text, VmtMaterial& material) {
  material = {};
  const auto tokens = tokenize(text);
  if (tokens.size() < 2 || tokens[0].kind != Token::Kind::Text || tokens[1].kind != Token::Kind::Open) {
    material.error = "VMT shader header is missing"; return false;
  }
  material.shader = tokens[0].value;
  std::unordered_map<std::string, VmtValue> root;
  std::size_t cursor = 2;
  if (!readObject(tokens, cursor, root, material.error)) return false;
  for (const auto& [key, value] : root) if (!value.object) material.fields[key] = value.text;
  material.baseTexture = scalar(root, "$basetexture");
  material.baseTexture2 = scalar(root, "$basetexture2");
  material.bumpMap = scalar(root, "$bumpmap");
  material.envMap = scalar(root, "$envmap");
  material.selfIllumTint = scalar(root, "$selfillumtint");
  material.phongTint = scalar(root, "$phongtint");
  material.waterBottomMaterial = scalar(root, "$bottommaterial");
  material.underwaterOverlay = scalar(root, "$underwateroverlay");
  material.phong = enabled(scalar(root, "$phong"));
  material.selfIllum = enabled(scalar(root, "$selfillum"));
  material.translucent = enabled(scalar(root, "$translucent"));
  material.alphaTest = enabled(scalar(root, "$alphatest"));
  material.noCull = enabled(scalar(root, "$nocull")) || scalar(root, "$cull") == "0";
  material.aboveWater = enabled(scalar(root, "$abovewater"));
  const auto shaderName = lower(material.shader);
  material.waterShader = shaderName == "water" || shaderName == "water_dx90"
    || !material.waterBottomMaterial.empty() || !material.underwaterOverlay.empty();
  material.valid = true;
  return true;
}

std::string VmtParser::resourcePath(const std::string& materialName, const char* extension) {
  std::string name = materialName;
  std::replace(name.begin(), name.end(), '\\', '/');
  while (!name.empty() && name.front() == '/') name.erase(name.begin());
  const std::string prefix = "materials/";
  if (name.size() >= prefix.size() && lower(name.substr(0, prefix.size())) == prefix) name.erase(0, prefix.size());
  const std::string suffix = extension ? extension : "";
  if (!suffix.empty() && (name.size() < suffix.size() || lower(name.substr(name.size() - suffix.size())) != lower(suffix))) name += suffix;
  return prefix + name;
}
}
