// lighting_semantics_probe -- T24: the six lighting/shader controls, measured
// without a GPU, on a fixture and on a real map.
//
// WHAT IT ANSWERS
//   The task names six controls: `dynamicLighting`, `specular`, `bump`,
//   `$bumpmap2`, `%detailtype`, `$ssbump`. Three are pass switches (fields on
//   RenderSettings) and three are VMT keys. Before this probe the only reading
//   for any of them was a sentence in a report, and the two facts that decide
//   whether a control is implemented -- "the key was parsed" and "the constant
//   was uploaded" -- were not separable.
//
//   The probe runs the same `scanWorldLightingSemantics` the product's trace
//   runs, so the gate's six rows and the product's six rows are one function.
//   The self-test drives it with a fixture whose detail material really
//   declares `%detailtype`, which is the one key the reference install cannot
//   supply (every detail VMT it names is absent).
//
// Usage:
//   lighting_semantics_probe --self-test
//   lighting_semantics_probe --map <map.bsp> --tf-root <dir>
//       [--dynamic on|off] [--specular on|off] [--bump on|off]
//       [--bump-owner <material>] [--bump-texture <vtf path>]
//   lighting_semantics_probe --tf-root <dir> --resource <path>
// Exit: 0 = every check passed, 1 = a check failed, 2 = bad usage.
//
// The probe never touches the GPU and never writes a file. Whether a control
// changes a pixel is a question for a capture, not for a counter; that is the
// A/B half of lighting-semantics-check.sh.

#include "bsp_map.h"
#include "vmt_material.h"
#include "vpk_archive.h"
#include "world_lighting.h"
#include "world_material.h"
#include "world_material_features.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

using namespace tf2::native;

int failures = 0;

void emit(const std::string& key, const std::string& value) {
  std::cout << "{\"reading\":\"" << key << "\",\"value\":\"" << value << "\"}\n";
}
void emitNumber(const std::string& key, long long value) { emit(key, std::to_string(value)); }

void check(const char* name, bool ok, const std::string& detail) {
  std::cout << "{\"check\":\"" << name << "\",\"ok\":" << (ok ? "true" : "false")
            << "\",\"detail\":\"" << detail << "\"}\n";
  if (!ok) ++failures;
}

bool contains(const std::string& text, const std::string& needle) {
  return text.find(needle) != std::string::npos;
}

std::vector<std::uint8_t> readFile(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  return std::vector<std::uint8_t>((std::istreambuf_iterator<char>(input)),
    std::istreambuf_iterator<char>());
}

std::vector<std::uint8_t> bytesOf(const std::string& text) {
  return std::vector<std::uint8_t>(text.begin(), text.end());
}

const WorldLightingControl& controlAt(const WorldLightingReading& reading, std::size_t index) {
  return reading.controls[index];
}

// ---------------------------------------------------------------------------
// 1. the fixture: two materials that carry every key, one that carries none
// ---------------------------------------------------------------------------
int runSelfTest() {
  // A "install" of exactly the files below. The VTF entries hold one byte: the
  // probe only asks whether a resource resolves, and a decoder is not needed to
  // answer that.
  std::map<std::string, std::string> files;
  files["materials/fix/base_height.vtf"] = "v";
  files["materials/fix/b_height.vtf"] = "v";
  files["materials/fix/b2_height.vtf"] = "v";
  // The detail material is a real VMT with the one key the install lacks.
  files["materials/fix/detail_a.vmt"] = "LightmappedGeneric { \"%detailtype\" \"2\""
    " \"$basetexture\" \"fix/detail_a\" }";
  // `fix/detail_missing` is deliberately absent: it is the branch that makes
  // "%detailtype cannot be read here" a measurement rather than an assumption.

  VmtMaterial base, layer2, noBump;
  check("fixture.base.parses", VmtParser::parse(
    "LightmappedGeneric { \"$basetexture\" \"fix/base\" \"$bumpmap\" \"fix/base_height\""
    " \"$ssbump\" \"1\" \"$phong\" \"1\" \"$phongexponent\" \"12\" \"$phongboost\" \"2\""
    " \"$detail\" \"fix/detail_a\" }", base), base.error);
  check("fixture.layer2.parses", VmtParser::parse(
    "WorldVertexTransition { \"$basetexture\" \"fix/b\" \"$basetexture2\" \"fix/b2\""
    " \"$bumpmap\" \"fix/b_height\" \"$bumpmap2\" \"fix/b2_height\" \"$phong\" \"1\""
    " \"$phongexponent\" \"35\" }", layer2), layer2.error);
  check("fixture.nobump.parses", VmtParser::parse(
    "LightmappedGeneric { \"$basetexture\" \"fix/c\" \"$detail\" \"fix/detail_missing\" }",
    noBump), noBump.error);

  std::vector<WorldMaterialFeatureInput> inputs;
  inputs.push_back({"FIXTURE/BASE", &base, 100, 100});
  inputs.push_back({"FIXTURE/LAYER2", &layer2, 50, 50});
  inputs.push_back({"FIXTURE/NOBUMP", &noBump, 10, 0});   // no visible triangle

  WorldFeatureResolvers resolvers;
  resolvers.resourceExists = [&](const std::string& path) { return files.count(path) != 0; };
  resolvers.readResource = [&](const std::string& path) {
    const auto at = files.find(path);
    return at == files.end() ? std::vector<std::uint8_t>{} : bytesOf(at->second);
  };

  // ---- the default pass: every switch off --------------------------------
  WorldLightingPassState off;
  off.bumpOwnerMaterial = "FIXTURE/NOBUMP";
  off.bumpOwnerDeclared = false;
  const auto reading = scanWorldLightingSemantics(inputs, resolvers, off);
  check("controls.count", reading.controls.size() == 6,
    std::to_string(reading.controls.size()));
  const char* names[6] = {"dynamicLighting", "specular", "bump", "$bumpmap2", "%detailtype", "$ssbump"};
  const char* kinds[6] = {"pass-switch", "pass-switch", "pass-switch",
    "material-key", "material-key", "material-key"};
  bool namesOk = true, kindsOk = true;
  for (std::size_t index = 0; index < 6; ++index) {
    if (controlAt(reading, index).name != names[index]) namesOk = false;
    if (worldLightingKindName(controlAt(reading, index).kind) != std::string(kinds[index])) kindsOk = false;
  }
  check("controls.names", namesOk, "the six rows are in the task's order");
  check("controls.kinds", kindsOk, "three pass switches, three material keys");

  const auto& dynamic = controlAt(reading, 0);
  const auto& specular = controlAt(reading, 1);
  const auto& bump = controlAt(reading, 2);
  const auto& bump2 = controlAt(reading, 3);
  const auto& detail = controlAt(reading, 4);
  const auto& ssbump = controlAt(reading, 5);
  check("dynamic.off", dynamic.state == WorldLightingState::Disabled && !dynamic.requested
      && !dynamic.effective, worldLightingStateName(dynamic.state));
  check("dynamic.scope", dynamic.visibleMaterials == 2 && dynamic.visibleTriangles == 150,
    std::to_string(dynamic.visibleMaterials) + " materials / "
      + std::to_string(dynamic.visibleTriangles) + " triangles");
  check("specular.off", specular.state == WorldLightingState::Disabled,
    worldLightingStateName(specular.state));
  check("specular.phong", reading.phongDeclared == 2 && reading.phongVisible == 2,
    std::to_string(reading.phongDeclared) + " declared / "
      + std::to_string(reading.phongVisible) + " visible");
  check("specular.exponents", reading.phongExponentDeclared == 2
      && reading.phongExponentDistinct == 2,
    std::to_string(reading.phongExponentDeclared) + " declared / "
      + std::to_string(reading.phongExponentDistinct) + " distinct");
  check("specular.reason", contains(specular.reason, "$phong")
      && contains(specular.reason, "0.12") && contains(specular.reason, "32"),
    specular.reason);

  check("bump.counts", bump.declared == 2 && bump.visibleDeclared == 2
      && bump.resolved == 2 && bump.missing == 0,
    std::to_string(bump.declared) + "/" + std::to_string(bump.visibleDeclared)
      + " resolved " + std::to_string(bump.resolved));
  check("bump.refused", bump.state == WorldLightingState::Refused && bump.parsed
      && !bump.effective, worldLightingStateName(bump.state));
  check("bump.refusal.names.distinct", contains(bump.reason, "2 distinct"),
    bump.reason);

  check("bump2.counts", bump2.declared == 1 && bump2.visibleDeclared == 1
      && bump2.resolved == 1 && bump2.parsed,
    std::to_string(bump2.declared) + " declared, " + std::to_string(bump2.resolved) + " resolved");
  check("bump2.refused", bump2.state == WorldLightingState::Refused,
    worldLightingStateName(bump2.state));
  check("bump2.reason", contains(bump2.reason, "layer 2"), bump2.reason);

  // $ssbump qualifies $bumpmap, so its resource reading is the bump map's.
  check("ssbump.counts", ssbump.declared == 1 && ssbump.visibleDeclared == 1
      && ssbump.resolved == 1 && ssbump.parsed,
    std::to_string(ssbump.declared) + " declared, " + std::to_string(ssbump.resolved) + " resolved");
  check("ssbump.refused", ssbump.state == WorldLightingState::Refused,
    worldLightingStateName(ssbump.state));

  // The detail layer: two base materials name one, one detail VMT resolves and
  // it really declares `%detailtype`. This is the parsing evidence the real
  // install cannot produce.
  check("detail.counts", detail.declared == 2 && detail.visibleDeclared == 1
      && detail.resolved == 1 && detail.missing == 1,
    std::to_string(detail.declared) + " declared, " + std::to_string(detail.resolved)
      + " resolved, " + std::to_string(detail.missing) + " missing");
  check("detail.typeRead", detail.parsed, detail.reason);
  check("detail.refused", detail.state == WorldLightingState::Refused,
    worldLightingStateName(detail.state));
  check("detail.reason.names.parse", contains(detail.reason, "1 distinct %detailtype"),
    detail.reason);

  // ---- pass matrix: the same materials with the switches on ---------------
  {
    WorldLightingPassState on;
    on.dynamicLightingRequested = true;
    on.dynamicLightingEffective = true;
    on.specularRequested = true;
    on.specularEffective = true;
    on.bumpRequested = true;
    on.bumpEffective = true;
    on.bumpOwnerDeclared = true;
    on.bumpOwnerMaterial = "FIXTURE/BASE";
    on.bumpOwnerTexture = "materials/fix/base_height.vtf";
    on.bumpTextureSource = "material";
    const auto applied = scanWorldLightingSemantics(inputs, resolvers, on);
    check("pass.dynamic.applied", controlAt(applied, 0).state == WorldLightingState::Applied,
      worldLightingStateName(controlAt(applied, 0).state));
    check("pass.specular.applied", controlAt(applied, 1).state == WorldLightingState::Applied,
      worldLightingStateName(controlAt(applied, 1).state));
    check("pass.bump.applied", controlAt(applied, 2).state == WorldLightingState::Applied
        && contains(controlAt(applied, 2).reason, "FIXTURE/BASE"),
      controlAt(applied, 2).reason);
    emitNumber("pass.appliedCount", 3);
  }

  // ---- the refusal that only exists when requested != effective ----------
  {
    WorldLightingPassState requestedNoTexture;
    requestedNoTexture.bumpRequested = true;
    requestedNoTexture.bumpEffective = false;
    requestedNoTexture.bumpOwnerMaterial = "FIXTURE/NOBUMP";
    requestedNoTexture.bumpOwnerDeclared = false;
    const auto refused = scanWorldLightingSemantics(inputs, resolvers, requestedNoTexture);
    const auto& row = controlAt(refused, 2);
    check("pass.bump.requestedNotEffective", row.state == WorldLightingState::Refused
        && row.requested && !row.effective, worldLightingStateName(row.state));
    check("pass.bump.reason.names.owner", contains(row.reason, "declares no $bumpmap"),
      row.reason);
  }
  {
    WorldLightingPassState requestedNoConstant;
    requestedNoConstant.dynamicLightingRequested = true;
    requestedNoConstant.dynamicLightingEffective = false;
    const auto refused = scanWorldLightingSemantics(inputs, resolvers, requestedNoConstant);
    check("pass.dynamic.requestedNotEffective",
      controlAt(refused, 0).state == WorldLightingState::Refused,
      controlAt(refused, 0).reason);
  }

  emit("selfTest", failures == 0 ? "true" : "false");
  return failures == 0 ? 0 : 1;
}

// ---------------------------------------------------------------------------
// 2. a real map
// ---------------------------------------------------------------------------
struct Reader {
  std::filesystem::path tfRoot;
  VpkArchiveSet archives;
  std::vector<std::uint8_t> bspBytes;

  std::vector<std::uint8_t> read(const std::string& path) const {
    std::error_code error;
    const auto loose = tfRoot / std::filesystem::u8path(path);
    if (std::filesystem::exists(loose, error)) return readFile(loose);
    std::vector<std::filesystem::path> holders;
    archives.collectContaining(path, holders);
    for (const auto& holder : holders) {
      const auto* archive = archives.find(holder);
      if (!archive) continue;
      auto bytes = archive->read(path);
      if (!bytes.empty()) return bytes;
    }
    std::vector<std::uint8_t> packed;
    if (!bspBytes.empty() && bspPakfileEntry(bspBytes, path, packed, nullptr)) return packed;
    return {};
  }

  // The same lookup WITHOUT the map's own pakfile. The product's feature scan
  // reads through an AssetStoreChain, which has no pakfile fallback; this is
  // the instrument that says whether a path the product calls missing is
  // missing from the install or only from the chain it searched.
  std::vector<std::uint8_t> readChainOnly(const std::string& path) const {
    std::error_code error;
    const auto loose = tfRoot / std::filesystem::u8path(path);
    if (std::filesystem::exists(loose, error)) return readFile(loose);
    std::vector<std::filesystem::path> holders;
    archives.collectContaining(path, holders);
    for (const auto& holder : holders) {
      const auto* archive = archives.find(holder);
      if (!archive) continue;
      auto bytes = archive->read(path);
      if (!bytes.empty()) return bytes;
    }
    return {};
  }
};

void emitControl(const WorldLightingControl& control) {
  const std::string prefix = "lighting." + control.name + ".";
  emit(prefix + "kind", worldLightingKindName(control.kind));
  emit(prefix + "state", worldLightingStateName(control.state));
  emitNumber(prefix + "declared", static_cast<long long>(control.declared));
  emitNumber(prefix + "visible", static_cast<long long>(control.visibleDeclared));
  emitNumber(prefix + "resolved", static_cast<long long>(control.resolved));
  emitNumber(prefix + "missing", static_cast<long long>(control.missing));
  emitNumber(prefix + "materials", static_cast<long long>(control.visibleMaterials));
  emitNumber(prefix + "triangles", static_cast<long long>(control.visibleTriangles));
  emitNumber(prefix + "requested", control.requested ? 1 : 0);
  emitNumber(prefix + "effective", control.effective ? 1 : 0);
  emitNumber(prefix + "parsed", control.parsed ? 1 : 0);
  emit(prefix + "first", control.firstMaterial.empty() ? "-" : control.firstMaterial);
  emit(prefix + "resource", control.firstResource.empty() ? "-" : control.firstResource);
  emit(prefix + "reason", control.reason.empty() ? "-" : control.reason);
}

int runMap(const std::filesystem::path& mapPath, const std::filesystem::path& tfRoot,
    const WorldLightingPassState& requested, bool ownerGiven, const std::string& owner,
    const std::string& texture, bool listBump) {
  const auto bspBytes = readFile(mapPath);
  if (bspBytes.empty()) { std::cerr << "cannot read " << mapPath.string() << "\n"; return 2; }
  BspMap map;
  BspParser::parse(bspBytes, map, 200000);
  if (!map.valid) { std::cerr << "map did not parse: " << map.error << "\n"; return 2; }
  Reader reader;
  reader.tfRoot = tfRoot;
  reader.archives = VpkArchiveSet::openDirectory(tfRoot);
  reader.bspBytes = bspBytes;

  std::unordered_map<std::string, std::size_t> trianglesByMaterial;
  std::string firstMaterial;
  for (const auto& triangle : map.triangles) {
    ++trianglesByMaterial[triangle.material];
    if (firstMaterial.empty() && !triangle.material.empty()) firstMaterial = triangle.material;
  }
  emit("map.firstMaterial", firstMaterial.empty() ? "-" : firstMaterial);
  emitNumber("map.distinctMaterials", static_cast<long long>(trianglesByMaterial.size()));

  // The bump slot's owner, the way the product decides it: the first material
  // with a name, then that material's own $bumpmap if its VTF resolves. The
  // probe reproduces the product's default so the two ledgers can be compared;
  // --bump-owner / --bump-texture override it for an A/B.
  std::string resolvedOwner = ownerGiven ? owner : firstMaterial;
  std::string resolvedTexture = texture;
  bool ownerDeclared = false;
  {
    VmtMaterial ownerMaterial;
    const auto bytes = reader.read(VmtParser::resourcePath(resolvedOwner, ".vmt"));
    if (!bytes.empty() && VmtParser::parse(std::string(bytes.begin(), bytes.end()), ownerMaterial)) {
      ownerDeclared = !ownerMaterial.bumpMap.empty();
      if (resolvedTexture.empty() && ownerDeclared) {
        const auto path = VmtParser::resourcePath(ownerMaterial.bumpMap, ".vtf");
        if (!reader.read(path).empty()) resolvedTexture = path;
      }
    }
  }
  // A texture that does not resolve is not a bound texture. Clearing it here
  // makes `--bump on` with a missing path read as refused for the same reason
  // the product would: the sampler cannot hold bytes that are not there.
  if (!resolvedTexture.empty() && reader.read(resolvedTexture).empty()) {
    emit("map.bumpTextureMissing", resolvedTexture);
    resolvedTexture.clear();
  }
  emit("map.bumpOwner", resolvedOwner.empty() ? "-" : resolvedOwner);
  emit("map.bumpTexture", resolvedTexture.empty() ? "-" : resolvedTexture);
  emit("map.bumpTextureSource", resolvedTexture.empty() ? "none" : (texture.empty() ? "material" : "forced"));
  emitNumber("map.bumpOwnerDeclared", ownerDeclared ? 1 : 0);

  std::vector<VmtMaterial> materials;
  std::vector<std::string> names;
  std::vector<WorldMaterialFeatureInput> inputs;
  materials.reserve(trianglesByMaterial.size());
  names.reserve(trianglesByMaterial.size());
  for (const auto& entry : trianglesByMaterial) {
    VmtMaterial material;
    const auto bytes = reader.read(VmtParser::resourcePath(entry.first, ".vmt"));
    if (!bytes.empty()) VmtParser::parse(std::string(bytes.begin(), bytes.end()), material);
    materials.push_back(std::move(material));
    names.push_back(entry.first);
  }
  for (std::size_t index = 0; index < names.size(); ++index) {
    WorldMaterialFeatureInput input;
    input.material = names[index];
    input.vmt = &materials[index];
    input.triangles = trianglesByMaterial[names[index]];
    input.visibleTriangles = 0;   // the probe has no camera
    inputs.push_back(std::move(input));
  }

  WorldFeatureResolvers resolvers;
  resolvers.resourceExists = [&](const std::string& path) { return !reader.read(path).empty(); };
  resolvers.readResource = [&](const std::string& path) { return reader.read(path); };

  if (listBump) {
    // T24: attribute the product's `missing` count. The product's scan resolves
    // through an AssetStoreChain (loose + VPK), while the world material fetch
    // also reads the map's own pakfile. A path that resolves here but not there
    // is a limitation of the instrument, not an absent texture, and the two
    // must not be reported as the same fact.
    std::size_t chainMissing = 0, pakfileOnly = 0, absent = 0;
    for (std::size_t index = 0; index < names.size(); ++index) {
      if (materials[index].bumpMap.empty()) continue;
      const auto path = VmtParser::resourcePath(materials[index].bumpMap, ".vtf");
      const bool inChain = !reader.readChainOnly(path).empty();
      const bool anywhere = !reader.read(path).empty();
      if (!inChain) ++chainMissing;
      if (!inChain && anywhere) ++pakfileOnly;
      if (!anywhere) ++absent;
      emit("bump.item", names[index] + "|" + path
        + "|chain=" + (inChain ? "1" : "0") + "|pakfileOnly=" + ((!inChain && anywhere) ? "1" : "0"));
    }
    emitNumber("bump.chainMissing", static_cast<long long>(chainMissing));
    emitNumber("bump.pakfileOnly", static_cast<long long>(pakfileOnly));
    emitNumber("bump.absent", static_cast<long long>(absent));
  }

  WorldLightingPassState pass = requested;
  pass.bumpOwnerMaterial = resolvedOwner;
  pass.bumpOwnerTexture = resolvedTexture;
  pass.bumpOwnerDeclared = ownerDeclared;
  pass.bumpTextureSource = resolvedTexture.empty() ? "none"
    : (texture.empty() ? "material" : "forced");
  // A requested switch is only effective when the pass can honour it. That is
  // the product's rule (`bumpMapping && texture bound`); the probe states it
  // here rather than accepting `requested` as `effective`.
  pass.bumpEffective = requested.bumpEffective && !resolvedTexture.empty();

  const auto reading = scanWorldLightingSemantics(inputs, resolvers, pass);
  emit("lighting.summary", reading.summary);
  emitNumber("lighting.phongDeclared", static_cast<long long>(reading.phongDeclared));
  emitNumber("lighting.phongVisible", static_cast<long long>(reading.phongVisible));
  emitNumber("lighting.phongExponents", static_cast<long long>(reading.phongExponentDeclared));
  emitNumber("lighting.phongExponentDistinct", static_cast<long long>(reading.phongExponentDistinct));
  emit("lighting.firstPhongMaterial", reading.firstPhongMaterial.empty() ? "-" : reading.firstPhongMaterial);
  for (const auto& control : reading.controls) emitControl(control);
  return failures == 0 ? 0 : 1;
}

} // namespace

int main(int argc, char** argv) {
  bool selfTest = false, listBump = false;
  std::filesystem::path mapPath, tfRoot;
  std::string resource;
  std::string owner, texture;
  bool ownerGiven = false;
  WorldLightingPassState requested;
  for (int index = 1; index < argc; ++index) {
    const std::string arg = argv[index];
    if (arg == "--self-test") { selfTest = true; continue; }
    if (arg == "--map" && index + 1 < argc) { mapPath = std::filesystem::u8path(argv[++index]); continue; }
    if (arg == "--tf-root" && index + 1 < argc) { tfRoot = std::filesystem::u8path(argv[++index]); continue; }
    if (arg == "--resource" && index + 1 < argc) { resource = argv[++index]; continue; }
    if (arg == "--list-bump") { listBump = true; continue; }
    if (arg == "--bump-owner" && index + 1 < argc) { owner = argv[++index]; ownerGiven = true; continue; }
    if (arg == "--bump-texture" && index + 1 < argc) { texture = argv[++index]; continue; }
    if (arg == "--dynamic" && index + 1 < argc) {
      requested.dynamicLightingRequested = std::string(argv[++index]) == "on";
      requested.dynamicLightingEffective = requested.dynamicLightingRequested;
      continue;
    }
    if (arg == "--specular" && index + 1 < argc) {
      requested.specularRequested = std::string(argv[++index]) == "on";
      requested.specularEffective = requested.specularRequested;
      continue;
    }
    if (arg == "--bump" && index + 1 < argc) {
      requested.bumpRequested = std::string(argv[++index]) == "on";
      requested.bumpEffective = requested.bumpRequested;
      continue;
    }
    std::cerr << "usage: lighting_semantics_probe --self-test |"
                 " --map <map.bsp> --tf-root <dir> [--dynamic on|off] [--specular on|off]"
                 " [--bump on|off] [--bump-owner <material>] [--bump-texture <vtf>] |"
                 " --tf-root <dir> --resource <path>\n";
    return 2;
  }
  int result = 0;
  if (selfTest) result |= runSelfTest();
  if (!resource.empty()) {
    if (tfRoot.empty()) { std::cerr << "--resource needs --tf-root\n"; return 2; }
    Reader reader;
    reader.tfRoot = tfRoot;
    reader.archives = VpkArchiveSet::openDirectory(tfRoot);
    const auto bytes = reader.read(resource);
    emit("resource.path", resource);
    emit("resource.exists", bytes.empty() ? "false" : "true");
    emitNumber("resource.bytes", static_cast<long long>(bytes.size()));
  }
  if (!mapPath.empty()) {
    if (tfRoot.empty()) { std::cerr << "--map needs --tf-root\n"; return 2; }
    result |= runMap(mapPath, tfRoot, requested, ownerGiven, owner, texture, listBump);
  }
  if (!selfTest && mapPath.empty() && resource.empty()) {
    std::cerr << "nothing to do: pass --self-test, --map or --resource\n";
    return 2;
  }
  return result;
}
