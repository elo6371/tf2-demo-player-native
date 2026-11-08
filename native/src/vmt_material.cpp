#include "vmt_material.h"

#include <algorithm>
#include <cctype>
#include <cmath>
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
// T59 ------------------------------------------------------------------------
// Patch VMTs. A TF2 map ships one wrapper per lightmapped face that needs a
// local env cubemap. The wrapper's header is the word patch, it names an include
// material and a replace block whose keys override the include's. The parser
// used to read the wrapper's own root fields only, so $basetexture was empty and
// every such material was refused and drawn as a flat shade.
//
// The include is read through the caller's store rather than a path here: a VMT
// can live loose, in a VPK, or inside the map's own BSP pakfile, and only the
// caller knows which -- the same lookup policy the product already uses for the
// .vtf files it fetches.
struct VmtNode {
  std::string shader;
  std::unordered_map<std::string, VmtValue> root;
};

bool loadNode(const std::string& text, VmtNode& node, std::string& error) {
  const auto tokens = tokenize(text);
  if (tokens.size() < 2 || tokens[0].kind != Token::Kind::Text || tokens[1].kind != Token::Kind::Open) {
    error = "VMT shader header is missing"; return false;
  }
  node.shader = tokens[0].value;
  node.root.clear();
  std::size_t cursor = 2;
  return readObject(tokens, cursor, node.root, error);
}

// Depth 4 is a refusal, not a tuning knob: a chain deeper than that is a cycle or
// a mistake, and following it forever would hang the loader.
constexpr int kMaxPatchDepth = 4;

bool resolveNode(VmtNode& node, const VmtParser::IncludeReader& read, int depth,
    VmtPatchReading& patch, std::string& error) {
  if (lower(node.shader) != "patch") return true;
  patch.declared = true;
  if (depth >= kMaxPatchDepth) { error = "patch include chain is deeper than 4"; return false; }
  const std::string include = scalar(node.root, "include");
  if (patch.includePath.empty()) patch.includePath = include;
  if (include.empty()) { error = "patch declares no include"; return false; }
  const std::string resource = VmtParser::resourcePath(include, ".vmt");
  if (patch.includeResource.empty()) patch.includeResource = resource;
  if (!read) { error = "patch include cannot be read: no store was given"; return false; }
  const auto bytes = read(resource);
  if (bytes.empty()) { error = "patch include is missing: " + resource; return false; }
  VmtNode included;
  std::string includeError;
  if (!loadNode(std::string(bytes.begin(), bytes.end()), included, includeError)) {
    error = "patch include did not parse: " + resource + ": " + includeError; return false;
  }
  if (!resolveNode(included, read, depth + 1, patch, includeError)) { error = includeError; return false; }
  const auto replace = node.root.find("replace");
  if (replace != node.root.end() && replace->second.object) {
    for (const auto& entry : replace->second.fields) {
      if (entry.second.object) { patch.objectBlocks.push_back(entry.first); continue; }
      included.root[entry.first] = entry.second;
      patch.replaced.push_back(entry.first);
    }
  }
  // The resolved node carries the INCLUDE's shader: the pass draws a
  // LightMappedGeneric, and patch is the wrapper's bookkeeping, not a shader this
  // renderer could fall back on.
  node = std::move(included);
  patch.resolved = true;
  return true;
}

// Parses one $basetexturetransform -- "center .5 .5 scale 4 4 rotate 45
// translate 0 0" -- into the numbers it names. Every number must be a whole
// token and every word must be one of the four keys this format defines;
// anything else returns false. A word this parser does not understand is NOT
// the identity: an unknown key is a uv change it cannot reproduce, and guessing
// identity is how a rotated texture gets drawn axis-aligned without anyone
// noticing.
bool parseTextureTransform(const std::string& text, float centerOut[2], float scaleOut[2],
    float* rotateOut, float translateOut[2]) {
  double center[2] = {0.5, 0.5}, scale[2] = {1.0, 1.0}, translate[2] = {0.0, 0.0}, rotate = 0.0;
  std::size_t i = 0;
  const auto word = [&](std::string& out) {
    while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i]))) ++i;
    out.clear();
    while (i < text.size() && !std::isspace(static_cast<unsigned char>(text[i]))) out.push_back(text[i++]);
    return !out.empty();
  };
  const auto number = [&](double& out) {
    std::string token;
    if (!word(token)) return false;
    char* end = nullptr;
    out = std::strtod(token.c_str(), &end);
    return end && *end == 0;
  };
  std::string key;
  while (word(key)) {
    const std::string name = lower(key);
    if (name == "center") {
      if (!number(center[0]) || !number(center[1])) return false;
    } else if (name == "scale") {
      if (!number(scale[0]) || !number(scale[1])) return false;
    } else if (name == "rotate") {
      if (!number(rotate)) return false;
    } else if (name == "translate") {
      if (!number(translate[0]) || !number(translate[1])) return false;
    } else {
      return false;
    }
  }
  centerOut[0] = static_cast<float>(center[0]);
  centerOut[1] = static_cast<float>(center[1]);
  scaleOut[0] = static_cast<float>(scale[0]);
  scaleOut[1] = static_cast<float>(scale[1]);
  *rotateOut = static_cast<float>(rotate);
  translateOut[0] = static_cast<float>(translate[0]);
  translateOut[1] = static_cast<float>(translate[1]);
  return true;
}

// True only for a transform that was parsed and leaves the uv alone.
bool textureTransformIsIdentity(const float center[2], const float scale[2], float rotate,
    const float translate[2]) {
  const auto same = [](float a, float b) { return std::fabs(a - b) <= 1.0e-6f; };
  return same(center[0], 0.5f) && same(center[1], 0.5f) && same(scale[0], 1.0f)
    && same(scale[1], 1.0f) && same(rotate, 0.0f) && same(translate[0], 0.0f)
    && same(translate[1], 0.0f);
}

// The field extraction stage, run on the EFFECTIVE root: for a patch wrapper that
// is the include's root after the replace block was overlaid, so every typed field
// below ($basetexture, $bumpmap, $blendmodulatetexture, ...) reads the merged
// material and not the empty wrapper.
bool extractFields(const std::unordered_map<std::string, VmtValue>& root, VmtMaterial& material) {
  for (const auto& [key, value] : root) if (!value.object) material.fields[key] = value.text;
  material.baseTexture = scalar(root, "$basetexture");
  // `EyeRefract` VMTs (the shared eyeball materials) name their base texture
  // only in a nested `Eyes_dx8` fallback block. Reading the root key alone
  // leaves them textureless, which used to make the model resolver drop the
  // eyeball meshes. Follow the fallback block; the world pass keeps reading
  // `baseTexture` and is unaffected.
  if (material.baseTexture.empty()) {
    const auto fallback = root.find("eyes_dx8");
    if (fallback != root.end() && fallback->second.object) {
      material.fallbackBaseTexture = scalar(fallback->second.fields, "$basetexture");
    }
  }
  material.baseTexture2 = scalar(root, "$basetexture2");
  material.blendModulateTexture = scalar(root, "$blendmodulatetexture");
  material.bumpMap = scalar(root, "$bumpmap");
  // `$bumpmap1` is layer 1 of a WorldVertexTransition. It is read as an alias
  // rather than ignored: `$bumpmap` and `$bumpmap1` are the same slot in the
  // SDK, and a blend material that names only the numbered form would otherwise
  // read as having no normal map at all.
  if (material.bumpMap.empty()) material.bumpMap = scalar(root, "$bumpmap1");
  material.bumpMap2 = scalar(root, "$bumpmap2");
  material.ssBump = enabled(scalar(root, "$ssbump"));
  material.detailTexture = scalar(root, "$detail");
  material.detailType = scalar(root, "%detailtype");
  {
    // `$detailblendmode` is an integer TCOMBINE_* index; a malformed value is
    // refused rather than defaulted, because defaulting would draw a mode-8
    // material as mode 0 and change every texel of it.
    const std::string mode = scalar(root, "$detailblendmode");
    if (!mode.empty()) {
      char* end = nullptr;
      const long value = std::strtol(mode.c_str(), &end, 10);
      if (end && *end == '\0') {
        material.detailBlendMode = static_cast<int>(value);
        // 0,1,2,3,8 are the pre-lighting branches TextureCombine implements for
        // a base colour and a detail colour; 4 needs the base's alpha, 5 and 6
        // run after lighting, 7 selects between two patterns by base alpha, 9
        // only writes alpha, and 10/11 consume an SSBump detail. None of those
        // inputs exist in the world pass, so they are named, not approximated.
        material.detailBlendModeUnsupported = !(value == 0 || value == 1 || value == 2
          || value == 3 || value == 8);
      } else {
        material.detailBlendModeUnsupported = true;
      }
    }
    const std::string factor = scalar(root, "$detailblendfactor");
    if (!factor.empty()) {
      char* end = nullptr;
      const double value = std::strtod(factor.c_str(), &end);
      if (end && *end == '\0') material.detailBlendFactor = static_cast<float>(value);
    }
    const std::string scale = scalar(root, "$detailscale");
    if (!scale.empty()) {
      char* end = nullptr;
      const double value = std::strtod(scale.c_str(), &end);
      if (end && *end == '\0' && value > 0.0) material.detailScale = static_cast<float>(value);
    }
  }
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
  // T59: $basetexturetransform re-maps the uv before sampling (center, scale,
  // rotate, translate). The world pass samples the face's own texinfo axes, so a
  // material carrying a transform has to be re-mapped by the same matrix or it
  // lands rotated and stretched from where TF2 puts it -- pl_odyssey's glass
  // windows ask for rotate 45 scale 4.
  //
  // R2: this pass now applies that matrix in the vertex stage, so a transform it
  // can read into numbers is no longer a refusal. What still refuses is the shape
  // it cannot read: an object block, or a word it does not understand. Guessing
  // the identity there would draw a rotated texture axis-aligned and nobody could
  // tell the picture is wrong.
  //
  // $basetexturetransform2 re-maps the second layer and is read the same way.
  {
    const auto recordTransform = [&material](const std::string& text, bool parsed,
                                             const float center[2], const float scale[2],
                                             float rotate, const float translate[2]) {
      if (material.baseTextureTransform) return;
      material.baseTextureTransform = true;
      material.baseTextureTransformText = text;
      if (!parsed) return;
      material.baseTextureTransformParsed = true;
      material.baseTextureTransformCenter[0] = center[0];
      material.baseTextureTransformCenter[1] = center[1];
      material.baseTextureTransformScale[0] = scale[0];
      material.baseTextureTransformScale[1] = scale[1];
      material.baseTextureTransformRotate = rotate;
      material.baseTextureTransformTranslate[0] = translate[0];
      material.baseTextureTransformTranslate[1] = translate[1];
    };
    for (const char* key : {"$basetexturetransform", "$basetexturetransform2"}) {
      const auto found = root.find(key);
      if (found == root.end()) continue;
      float center[2] = {0.5f, 0.5f}, scale[2] = {1.0f, 1.0f}, translate[2] = {0.0f, 0.0f};
      float rotate = 0.0f;
      if (found->second.object) {
        recordTransform(std::string(key) + " is an object block", false, center, scale,
          rotate, translate);
        continue;
      }
      const bool parsed =
        parseTextureTransform(found->second.text, center, scale, &rotate, translate);
      if (parsed && textureTransformIsIdentity(center, scale, rotate, translate)) continue;
      recordTransform(found->second.text, parsed, center, scale, rotate, translate);
    }
  }
  // T59 (regression repaired): this assignment lived in VmtParser::parse before
  // 788f95c moved the body into extractFields, and the move dropped it. No gate
  // noticed, because every gate reads the atlas path; the map-material fallback
  // (main.cpp:4671) is the only reader, and without this line it silently stopped
  // resolving the demo's worldspawn texture -- the window title printed
  // "maptex: fallback" and the fallback upload received 0x0.
  material.valid = true;
  return true;
}
}

bool VmtParser::parse(const std::string& text, VmtMaterial& material) {
  return parse(text, material, IncludeReader{});
}

bool VmtParser::parse(const std::string& text, VmtMaterial& material,
    const IncludeReader& readInclude) {
  material = {};
  VmtNode node;
  if (!loadNode(text, node, material.error)) return false;
  VmtPatchReading patch;
  std::string patchError;
  if (!resolveNode(node, readInclude, 0, patch, patchError)) {
    // The wrapper stays in the reading even when it fails: a patch whose include
    // is missing and a VMT with no shader are different refusals, and only the
    // first one can be fixed by shipping the asset.
    material.shader = "patch";
    material.patch = patch;
    material.error = patchError;
    return false;
  }
  material.shader = node.shader;
  material.patch = patch;
  return extractFields(node.root, material);
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
