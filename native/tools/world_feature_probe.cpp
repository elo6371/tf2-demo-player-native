// world_feature_probe -- T27: the per-material feature path, measured without a
// GPU, on a fixture and on a real map.
//
// WHAT IT ANSWERS
//   T27's chain has four links and each one can fail silently:
//
//     1. the VMT declares the key      -- VmtParser ($bumpmap/$bumpmap1,
//                                         $bumpmap2, $ssbump, $detail,
//                                         $detailblendmode, $detailblendfactor,
//                                         $detailscale, %detailtype)
//     2. the resource resolves          -- the same store chain the product uses
//     3. the shader input exists        -- the atlas rectangle, the vertex bit,
//                                         the mode/factor the vertex carries
//     4. the pixel changes              -- a capture, not this probe
//
//   This probe measures 1-3, and it measures them on the SAME functions the
//   product runs (VmtParser, scanWorldMaterialFeatures, scanWorldLightingSemantics,
//   worldFeatureTileRect). Link 4 is evidence/world-material-semantics/ and the
//   gate's A/B pairs; a probe that claimed it would be a counter pretending to be
//   a picture, which is the thing the task refuses.
//
//   `combineDetail`, `decodeNormal` and `tangentNormal` are CPU transcriptions of
//   the pixel shader's functions. They exist because "the shader lerps by
//   `factor * detail.a`" is a claim that needs a number attached, and because the
//   tangent frame's sign is the easiest part of a normal map to get backwards.
//   They are NOT a second implementation of the image: the frames are the
//   implementation, and the transcription is checked against hand-computed values
//   rather than against itself.
//
// Usage:
//   world_feature_probe --self-test
//   world_feature_probe --map <map.bsp> --tf-root <dir> [--material-bump on|off]
//       [--bump2 on|off] [--detail on|off] [--list-detail] [--list-bump]
//   world_feature_probe --tf-root <dir> --resource <path>
// Exit: 0 = every check passed, 1 = a check failed, 2 = bad usage.

#include "bsp_map.h"
#include "vmt_material.h"
#include "vpk_archive.h"
#include "vtf_texture.h"
#include "world_lighting.h"
#include "world_material.h"
#include "world_material_features.h"
#include "world_tile_atlas.h"

#include <algorithm>
#include <cmath>
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

// The shader's vector types, so the transcription below reads like the HLSL it
// is transcribed from. Aggregates, so `{a, b, c}` is the same brace form the
// shader uses.
struct float3 { float x = 0.0f; float y = 0.0f; float z = 0.0f; };
struct float4 { float x = 0.0f; float y = 0.0f; float z = 0.0f; float w = 0.0f; };

void emit(const std::string& key, const std::string& value) {
  std::cout << "{\"reading\":\"" << key << "\",\"value\":\"" << value << "\"}\n";
}
void emitNumber(const std::string& key, long long value) { emit(key, std::to_string(value)); }
void emitReal(const std::string& key, double value) {
  char buffer[64] = {0};
  std::snprintf(buffer, sizeof(buffer), "%.6f", value);
  emit(key, buffer);
}

void check(const char* name, bool ok, const std::string& detail) {
  std::cout << "{\"check\":\"" << name << "\",\"ok\":" << (ok ? "true" : "false")
            << ",\"detail\":\"" << detail << "\"}\n";
  if (!ok) ++failures;
}

bool contains(const std::string& text, const std::string& needle) {
  return text.find(needle) != std::string::npos;
}

bool near(double left, double right, double tolerance) {
  return std::fabs(left - right) <= tolerance;
}

std::vector<std::uint8_t> readFile(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  return std::vector<std::uint8_t>((std::istreambuf_iterator<char>(input)),
    std::istreambuf_iterator<char>());
}

std::vector<std::uint8_t> bytesOf(const std::string& text) {
  return std::vector<std::uint8_t>(text.begin(), text.end());
}

std::string jsonEscape(const std::string& text) {
  std::string out;
  out.reserve(text.size());
  for (const char character : text) {
    if (character == '"' || character == '\\') out.push_back('\\');
    out.push_back(character);
  }
  return out;
}

std::string prefixOf(const char* layer, const std::string& property) {
  return std::string("feature.") + layer + "." + property;
}

// ---------------------------------------------------------------------------
// 1. the shader's arithmetic, transcribed
// ---------------------------------------------------------------------------
// common_ps_fxc.h, TextureCombine: the five pre-lighting branches this pass
// implements. The comment on each line is the SDK expression, so a reader can
// check the transcription without opening the SDK.
float3 combineDetail(float3 base, float4 detail, int mode, float factor) {
  if (mode == 0) {  // rgb *= lerp(1, 2*detail, f)
    const float weight = factor;
    return {base.x * (1.0f + weight * (2.0f * detail.x - 1.0f)),
            base.y * (1.0f + weight * (2.0f * detail.y - 1.0f)),
            base.z * (1.0f + weight * (2.0f * detail.z - 1.0f))};
  }
  if (mode == 1) {  // rgb += f * detail
    return {base.x + factor * detail.x, base.y + factor * detail.y,
            base.z + factor * detail.z};
  }
  if (mode == 2) {  // fblend = f * detail.a; rgb = lerp(base, detail, fblend)
    const float weight = factor * detail.w;
    return {base.x + (detail.x - base.x) * weight,
            base.y + (detail.y - base.y) * weight,
            base.z + (detail.z - base.z) * weight};
  }
  if (mode == 3) {  // rgb = lerp(base, detail, f)
    return {base.x + (detail.x - base.x) * factor,
            base.y + (detail.y - base.y) * factor,
            base.z + (detail.z - base.z) * factor};
  }
  if (mode == 8) {  // rgb = lerp(base, base*detail, f)
    return {base.x * (1.0f + factor * (detail.x - 1.0f)),
            base.y * (1.0f + factor * (detail.y - 1.0f)),
            base.z * (1.0f + factor * (detail.z - 1.0f))};
  }
  return base;  // no branch: the CPU refuses these modes before they reach here
}

float3 decodeNormal(float3 texel) {
  float3 normal = {texel.x * 2.0f - 1.0f, texel.y * 2.0f - 1.0f, texel.z * 2.0f - 1.0f};
  const float length = std::sqrt(normal.x * normal.x + normal.y * normal.y + normal.z * normal.z);
  if (!(length > 1.0e-6f)) return {0.0f, 0.0f, 0.0f};
  return {normal.x / length, normal.y / length, normal.z / length};
}

// The same frame the shader builds: T orthogonalised against N, B = cross(N,T)
// with the sign from the texinfo v axis, texel rotated into it.
float3 tangentNormal(float3 texel, float3 normal, float3 tangentAxis, float sign) {
  const float projection = normal.x * tangentAxis.x + normal.y * tangentAxis.y
    + normal.z * tangentAxis.z;
  float3 tangent = {tangentAxis.x - normal.x * projection,
                    tangentAxis.y - normal.y * projection,
                    tangentAxis.z - normal.z * projection};
  const float length = std::sqrt(tangent.x * tangent.x + tangent.y * tangent.y
    + tangent.z * tangent.z);
  if (!(length > 1.0e-6f)) return normal;
  tangent.x /= length; tangent.y /= length; tangent.z /= length;
  float3 bitangent = {normal.y * tangent.z - normal.z * tangent.y,
                      normal.z * tangent.x - normal.x * tangent.z,
                      normal.x * tangent.y - normal.y * tangent.x};
  bitangent.x *= sign; bitangent.y *= sign; bitangent.z *= sign;
  const float3 texelNormal = decodeNormal(texel);
  float3 result = {tangent.x * texelNormal.x + bitangent.x * texelNormal.y + normal.x * texelNormal.z,
                   tangent.y * texelNormal.x + bitangent.y * texelNormal.y + normal.y * texelNormal.z,
                   tangent.z * texelNormal.x + bitangent.z * texelNormal.y + normal.z * texelNormal.z};
  const float resultLength = std::sqrt(result.x * result.x + result.y * result.y
    + result.z * result.z);
  if (!(resultLength > 1.0e-6f)) return normal;
  return {result.x / resultLength, result.y / resultLength, result.z / resultLength};
}

double dot(float3 left, float3 right) {
  return static_cast<double>(left.x) * right.x + static_cast<double>(left.y) * right.y
    + static_cast<double>(left.z) * right.z;
}

// ---------------------------------------------------------------------------
// 2. the fixture
// ---------------------------------------------------------------------------
int runSelfTest() {
  // ---- 2a. the parser: every key T27 reads, and the alias ----------------
  {
    VmtMaterial material;
    const bool parsed = VmtParser::parse(
      "WorldVertexTransition {"
      " \"$basetexture\" \"fix/a\" \"$basetexture2\" \"fix/b\""
      " \"$bumpmap1\" \"fix/a_height\" \"$bumpmap2\" \"fix/b_height\""
      " \"$detail\" \"fix/detail2\" \"$detailblendmode\" \"8\""
      " \"$detailblendfactor\" \"0.375\" \"$detailscale\" \"1.9\" }", material);
    check("parse.ok", parsed, material.error);
    check("parse.bumpmap1.alias", material.bumpMap == "fix/a_height",
      "$bumpmap1 -> bumpMap = " + material.bumpMap);
    check("parse.bumpmap2", material.bumpMap2 == "fix/b_height", material.bumpMap2);
    check("parse.detail.mode", material.detailBlendMode == 8,
      std::to_string(material.detailBlendMode));
    check("parse.detail.factor", near(material.detailBlendFactor, 0.375, 1.0e-6),
      std::to_string(material.detailBlendFactor));
    check("parse.detail.scale", near(material.detailScale, 1.9, 1.0e-6),
      std::to_string(material.detailScale));
    check("parse.detail.mode.supported", !material.detailBlendModeUnsupported,
      "mode 8 is TCOMBINE_MULTIPLY, which this pass implements");
    check("parse.ssbump.absent", !material.ssBump, "$ssbump was not declared");
  }
  {
    // `$bumpmap` wins over `$bumpmap1` when both are present: they are one slot,
    // and a material that names both means the unnumbered form.
    VmtMaterial material;
    VmtParser::parse("LightmappedGeneric { \"$basetexture\" \"fix/a\""
      " \"$bumpmap\" \"fix/plain\" \"$bumpmap1\" \"fix/numbered\" }", material);
    check("parse.bumpmap.precedence", material.bumpMap == "fix/plain", material.bumpMap);
  }
  {
    // The unsupported modes, one VMT each. 4 needs the base's alpha, 5 and 6 run
    // after lighting, 7 selects by base alpha, 9 writes only alpha, 10/11 consume
    // an SSBump detail. Every one of them is a different image, so a pass that
    // drew them as mode 0 would be wrong on every texel.
    const int refused[] = {4, 5, 6, 7, 9, 10, 11};
    bool allRefused = true;
    for (const int mode : refused) {
      VmtMaterial material;
      VmtParser::parse("LightmappedGeneric { \"$basetexture\" \"fix/a\" \"$detail\" \"fix/d\""
        " \"$detailblendmode\" \"" + std::to_string(mode) + "\" }", material);
      if (!material.detailBlendModeUnsupported) allRefused = false;
    }
    check("parse.detail.mode.refused", allRefused, "modes 4,5,6,7,9,10,11 are named, not drawn");
    const int drawn[] = {0, 1, 2, 3, 8};
    bool allDrawn = true;
    for (const int mode : drawn) {
      VmtMaterial material;
      VmtParser::parse("LightmappedGeneric { \"$basetexture\" \"fix/a\" \"$detail\" \"fix/d\""
        " \"$detailblendmode\" \"" + std::to_string(mode) + "\" }", material);
      if (material.detailBlendModeUnsupported) allDrawn = false;
    }
    check("parse.detail.mode.drawn", allDrawn, "modes 0,1,2,3,8 reach TextureCombine");
  }
  {
    // A malformed mode is refused rather than defaulted: defaulting would draw
    // an unknown material as mode 0 and change every texel of it.
    VmtMaterial material;
    VmtParser::parse("LightmappedGeneric { \"$basetexture\" \"fix/a\" \"$detail\" \"fix/d\""
      " \"$detailblendmode\" \"eight\" }", material);
    check("parse.detail.mode.malformed", material.detailBlendModeUnsupported,
      "a non-numeric $detailblendmode is refused");
  }
  {
    VmtMaterial material;
    VmtParser::parse("LightmappedGeneric { \"$basetexture\" \"fix/a\" \"$bumpmap\" \"fix/h\""
      " \"$ssbump\" \"1\" }", material);
    check("parse.ssbump", material.ssBump, "$ssbump 1 parses as true");
  }
  {
    VmtMaterial material;
    VmtParser::parse("LightmappedGeneric { \"%detailtype\" \"2\" \"$basetexture\" \"fix/d\" }",
      material);
    check("parse.detailtype", material.detailType == "2", material.detailType);
  }

  // ---- 2b. the atlas rectangle ------------------------------------------
  {
    // A 512-texel tile at (2057,1) in a 4112-wide atlas, features at half
    // resolution (2056 wide). The rect must be the SAME normalised rectangle:
    // that is the only reason one uv can address both atlases.
    WorldTileSlot slot;
    slot.x = 2057; slot.y = 1; slot.width = 512; slot.height = 512;
    slot.sourceWidth = 512; slot.sourceHeight = 512; slot.batch = 0;
    WorldFeatureTileRect rect;
    const bool placed = worldFeatureTileRect(slot, 4112, 4112, 2056, 2056, rect);
    check("rect.placed", placed, "a half-resolution rect exists for the tile");
    const double albedoCentreU = (2057.0 + 256.0) / 4112.0;
    const double featureCentreU = (rect.x + rect.width / 2.0) / 2056.0;
    check("rect.centre.u", near(albedoCentreU, featureCentreU, 1.5 / 2056.0),
      std::to_string(albedoCentreU) + " vs " + std::to_string(featureCentreU));
    const double albedoCentreV = (1.0 + 256.0) / 4112.0;
    const double featureCentreV = (rect.y + rect.height / 2.0) / 2056.0;
    check("rect.centre.v", near(albedoCentreV, featureCentreV, 1.5 / 2056.0),
      std::to_string(albedoCentreV) + " vs " + std::to_string(featureCentreV));
    check("rect.size", rect.width == 256 && rect.height == 256,
      std::to_string(rect.width) + "x" + std::to_string(rect.height));
    // Inside the atlas, away from the edge a clamped sampler would smear.
    check("rect.inside", rect.x >= 1 && rect.y >= 1
        && rect.x + rect.width <= 2055 && rect.y + rect.height <= 2055,
      std::to_string(rect.x) + "," + std::to_string(rect.y));
  }
  {
    // Two neighbouring tiles must not land on the same feature texels: an
    // overlap would put one material's normal map under another material's uv.
    WorldTileSlot left, right;
    left.x = 1; left.y = 1; left.width = 512; left.height = 512; left.batch = 0;
    right.x = 513; right.y = 1; right.width = 512; right.height = 512; right.batch = 0;
    WorldFeatureTileRect leftRect, rightRect;
    const bool a = worldFeatureTileRect(left, 1032, 1032, 516, 516, leftRect);
    const bool b = worldFeatureTileRect(right, 1032, 1032, 516, 516, rightRect);
    check("rect.neighbours", a && b && leftRect.x + leftRect.width <= rightRect.x,
      std::to_string(leftRect.x + leftRect.width) + " <= " + std::to_string(rightRect.x));
  }
  {
    // A rect that cannot fit is refused rather than clipped: a clipped tile is a
    // normal map under the wrong uv, which looks like paint rather than a bug.
    // A 4-texel feature atlas for a 512-texel tile cannot hold it at any scale.
    WorldTileSlot slot;
    slot.x = 1; slot.y = 1; slot.width = 512; slot.height = 512; slot.batch = 0;
    WorldFeatureTileRect rect;
    check("rect.too.small", !worldFeatureTileRect(slot, 512, 512, 4, 4, rect),
      "a 4-texel feature atlas cannot hold a 512-texel tile");
    check("rect.degenerate.feature", !worldFeatureTileRect(slot, 512, 512, 2, 2, rect),
      "a feature atlas narrower than 3 texels is refused outright");
  }

  // ---- 2c. TextureCombine, against hand-computed values ------------------
  {
    const float3 base = {0.4f, 0.5f, 0.6f};
    const float4 detail = {0.5f, 0.25f, 1.0f, 0.5f};
    const float factor = 0.5f;
    const float3 mode0 = combineDetail(base, detail, 0, factor);
    const float3 mode1 = combineDetail(base, detail, 1, factor);
    const float3 mode2 = combineDetail(base, detail, 2, factor);
    const float3 mode3 = combineDetail(base, detail, 3, factor);
    const float3 mode8 = combineDetail(base, detail, 8, factor);
    // base * (0.5 + detail) with f = 0.5
    check("combine.mode0", near(mode0.x, 0.40, 1e-6) && near(mode0.y, 0.375, 1e-6)
        && near(mode0.z, 0.90, 1e-6),
      std::to_string(mode0.x) + "," + std::to_string(mode0.y) + "," + std::to_string(mode0.z));
    // base + 0.5 * detail
    check("combine.mode1", near(mode1.x, 0.65, 1e-6) && near(mode1.y, 0.625, 1e-6)
        && near(mode1.z, 1.10, 1e-6),
      std::to_string(mode1.x) + "," + std::to_string(mode1.y) + "," + std::to_string(mode1.z));
    // lerp(base, detail, 0.5 * 0.5)
    check("combine.mode2", near(mode2.x, 0.425, 1e-6) && near(mode2.y, 0.4375, 1e-6)
        && near(mode2.z, 0.70, 1e-6),
      std::to_string(mode2.x) + "," + std::to_string(mode2.y) + "," + std::to_string(mode2.z));
    // lerp(base, detail, 0.5)
    check("combine.mode3", near(mode3.x, 0.45, 1e-6) && near(mode3.y, 0.375, 1e-6)
        && near(mode3.z, 0.80, 1e-6),
      std::to_string(mode3.x) + "," + std::to_string(mode3.y) + "," + std::to_string(mode3.z));
    // base * (0.5 + 0.5*detail)
    check("combine.mode8", near(mode8.x, 0.30, 1e-6) && near(mode8.y, 0.3125, 1e-6)
        && near(mode8.z, 0.60, 1e-6),
      std::to_string(mode8.x) + "," + std::to_string(mode8.y) + "," + std::to_string(mode8.z));
    // Factor 0 is the identity for every multiplicative branch and mode 3; a
    // branch that ignored the factor would show up here.
    const float3 zero0 = combineDetail(base, detail, 0, 0.0f);
    const float3 zero3 = combineDetail(base, detail, 3, 0.0f);
    const float3 zero8 = combineDetail(base, detail, 8, 0.0f);
    check("combine.factor.zero", near(zero0.x, base.x, 1e-6) && near(zero0.y, base.y, 1e-6)
        && near(zero0.z, base.z, 1e-6) && near(zero3.z, base.z, 1e-6)
        && near(zero8.x, base.x, 1e-6),
      "factor 0 leaves the base alone (modes 0/3/8)");
    // Mode 2's weight is `factor * detail.a`, so an alpha of 0 must be a no-op
    // even at factor 1. That is the difference between mode 2 and mode 3.
    const float4 transparent = {0.5f, 0.25f, 1.0f, 0.0f};
    const float3 mode2zero = combineDetail(base, transparent, 2, 1.0f);
    const float3 mode3zero = combineDetail(base, transparent, 3, 1.0f);
    check("combine.mode2.alpha", near(mode2zero.x, base.x, 1e-6)
        && near(mode3zero.x, transparent.x, 1e-6),
      "mode 2 scales by detail.a, mode 3 does not");
  }

  // ---- 2d. the tangent frame --------------------------------------------
  {
    const float3 normal = {0.0f, 0.0f, 1.0f};
    const float3 uAxis = {1.0f, 0.0f, 0.0f};
    // A flat texel (0.5,0.5,1) decodes to +N, so the result must be N itself.
    const float3 flat = tangentNormal({0.5f, 0.5f, 1.0f}, normal, uAxis, 1.0f);
    check("tangent.flat", near(dot(flat, normal), 1.0, 1e-5),
      std::to_string(dot(flat, normal)));
    // A texel tilted toward +u must come out tilted toward +u, not -u: that sign
    // is the difference between a lit wall and an inside-out one.
    const float3 tilted = tangentNormal({1.0f, 0.5f, 1.0f}, normal, uAxis, 1.0f);
    const double alongU = dot(tilted, uAxis);
    const double alongN = dot(tilted, normal);
    check("tangent.u", alongU > 0.6 && near(alongN, std::sqrt(0.5), 1e-4),
      "dot(T)=" + std::to_string(alongU) + " dot(N)=" + std::to_string(alongN));
    check("tangent.unit", near(std::sqrt(dot(tilted, tilted)), 1.0, 1e-5),
      "the result is a unit vector");
    // The bitangent's sign comes from the texinfo v axis; flipping it must flip
    // the v tilt and nothing else. The texel has to carry a nonzero y for this
    // to mean anything: `(0.5, 1, 1)` decodes to a normal tilted along +v.
    const float3 vTexel = {0.5f, 1.0f, 1.0f};
    const float3 tiltedPlus = tangentNormal(vTexel, normal, uAxis, 1.0f);
    const float3 tiltedMinus = tangentNormal(vTexel, normal, uAxis, -1.0f);
    const double vPlus = dot(tiltedPlus, float3{0.0f, 1.0f, 0.0f});
    const double vMinus = dot(tiltedMinus, float3{0.0f, 1.0f, 0.0f});
    check("tangent.sign", vPlus > 0.6 && near(vPlus, -vMinus, 1e-6),
      "v=" + std::to_string(vPlus) + " vs " + std::to_string(vMinus));
    // Rotating the surface rotates the result: the normal map is a local frame,
    // so a 90-degree yaw must move T into -X and leave N alone.
    const float3 rotatedNormal = {1.0f, 0.0f, 0.0f};
    const float3 rotatedAxis = {0.0f, 0.0f, -1.0f};
    const float3 rotated = tangentNormal({1.0f, 0.5f, 1.0f}, rotatedNormal, rotatedAxis, 1.0f);
    check("tangent.rotation", near(dot(rotated, rotatedNormal), alongN, 1e-4)
        && near(dot(rotated, rotatedAxis), alongU, 1e-4),
      "the same texel tilts by the same angle in the rotated frame");
    // A degenerate tangent (parallel to N) must be a no-op, not a NaN.
    const float3 degenerate = tangentNormal({1.0f, 0.5f, 1.0f}, normal, normal, 1.0f);
    check("tangent.degenerate", near(dot(degenerate, normal), 1.0, 1e-6),
      "a tangent parallel to N falls back to the geometric normal");
  }

  // ---- 2e. the ledger with the per-material path on ----------------------
  {
    // The fixture: one material that can use the per-material path, one that is
    // SSBump (so it cannot), one with a detail layer at a supported mode, one at
    // an unsupported mode.
    std::map<std::string, std::string> files;
    files["materials/fix/a_height.vtf"] = "v";
    files["materials/fix/a2_height.vtf"] = "v";
    // The detail VMT carries its own `%detailtype` on purpose: the key belongs to the
    // material that names the layer, so a scan that reads it from the detail file would
    // report "9" and the check below would fail. This is the placement the engine uses
    // (VBSP reads the surface material's key at compile time).
    files["materials/fix/detail_a.vmt"] =
      "LightmappedGeneric { \"%detailtype\" \"9\" \"$basetexture\" \"fix/detail_a\" }";
    files["materials/fix/detail_a.vtf"] = "v";

    VmtMaterial plain, ssbump, detail8, detail4;
    VmtParser::parse("WorldVertexTransition { \"$basetexture\" \"fix/a\""
      " \"$bumpmap\" \"fix/a_height\" \"$bumpmap2\" \"fix/a2_height\" }", plain);
    VmtParser::parse("LightmappedGeneric { \"$basetexture\" \"fix/s\""
      " \"$bumpmap\" \"fix/a_height\" \"$ssbump\" \"1\" }", ssbump);
    VmtParser::parse("LightmappedGeneric { \"$basetexture\" \"fix/d\""
      " \"%detailtype\" \"2\" \"$detail\" \"fix/detail_a\" \"$detailblendmode\" \"8\""
      " \"$detailblendfactor\" \"0.5\" \"$detailscale\" \"2\" }", detail8);
    VmtParser::parse("LightmappedGeneric { \"$basetexture\" \"fix/e\""
      " \"$detail\" \"fix/detail_a\" \"$detailblendmode\" \"4\" }", detail4);

    std::vector<WorldMaterialFeatureInput> inputs = {
      {"FIXTURE/PLAIN", &plain, 300, 300},
      {"FIXTURE/SSBUMP", &ssbump, 200, 200},
      {"FIXTURE/DETAIL8", &detail8, 100, 100},
      {"FIXTURE/DETAIL4", &detail4, 50, 50},
    };
    WorldFeatureResolvers resolvers;
    resolvers.resourceExists = [&](const std::string& path) { return files.count(path) != 0; };
    resolvers.readResource = [&](const std::string& path) {
      const auto at = files.find(path);
      return at == files.end() ? std::vector<std::uint8_t>{} : bytesOf(at->second);
    };
    const auto features = scanWorldMaterialFeatures(inputs, resolvers);
    check("fixture.bump.split", features.bumpMap.declared == 2
        && features.ssBumpDeclared == 1 && features.bumpMapWithoutSsBump == 1,
      std::to_string(features.bumpMap.declared) + " declared, "
        + std::to_string(features.ssBumpDeclared) + " ssbump, "
        + std::to_string(features.bumpMapWithoutSsBump) + " not");
    check("fixture.detail.type", features.detailTypesRead == 1
        && features.firstDetailType == "2", features.firstDetailType);

    // Off: four refusals, no applied row among the material keys.
    WorldLightingPassState off;
    const auto offReading = scanWorldLightingSemantics(inputs, resolvers, off);
    check("ledger.off.bump", offReading.controls[2].state == WorldLightingState::Refused,
      worldLightingStateName(offReading.controls[2].state));
    check("ledger.off.bump.reason", contains(offReading.controls[2].reason,
        "--world-material-bump"), offReading.controls[2].reason);
    check("ledger.off.bump2", offReading.controls[3].state == WorldLightingState::Refused,
      worldLightingStateName(offReading.controls[3].state));

    // On: bump and $bumpmap2 applied, $ssbump still refused with both counts in
    // the sentence, %detailtype still refused because its consumer is VBSP.
    WorldLightingPassState on;
    on.materialBumpRequested = true;
    on.materialBumpEffective = true;
    on.materialBumpMaterials = 2;
    on.bump2Requested = true;
    on.bump2Effective = true;
    on.bump2Materials = 1;
    on.detailRequested = true;
    on.detailEffective = true;
    on.detailMaterials = 1;
    const auto onReading = scanWorldLightingSemantics(inputs, resolvers, on);
    check("ledger.on.bump", onReading.controls[2].state == WorldLightingState::Applied,
      worldLightingStateName(onReading.controls[2].state));
    check("ledger.on.bump.reason", contains(onReading.controls[2].reason, "2 materials")
        && contains(onReading.controls[2].reason, "tangent basis"),
      onReading.controls[2].reason);
    check("ledger.on.bump2", onReading.controls[3].state == WorldLightingState::Applied,
      worldLightingStateName(onReading.controls[3].state));
    check("ledger.on.bump2.reason", contains(onReading.controls[3].reason, "lerp"),
      onReading.controls[3].reason);
    check("ledger.on.ssbump", onReading.controls[5].state == WorldLightingState::Refused,
      worldLightingStateName(onReading.controls[5].state));
    check("ledger.on.ssbump.split", contains(onReading.controls[5].reason, "1 of 2")
        && contains(onReading.controls[5].reason, "bumpBasis"),
      onReading.controls[5].reason);
    check("ledger.on.detailtype", onReading.controls[4].state == WorldLightingState::Refused
        && contains(onReading.controls[4].reason, "consumer")
        && contains(onReading.controls[4].reason, "first=2")
        && contains(onReading.controls[4].reason, "VBSP"),
      onReading.controls[4].reason);
    emitNumber("self.applied", onReading.controls[2].state == WorldLightingState::Applied ? 1 : 0);

    // Requested but not effective: the switch is on and the renderer has no
    // atlas, so the row must read refused and say why.
    WorldLightingPassState requestedOnly;
    requestedOnly.materialBumpRequested = true;
    requestedOnly.bump2Requested = true;
    const auto requestedReading = scanWorldLightingSemantics(inputs, resolvers, requestedOnly);
    check("ledger.requested.bump",
      requestedReading.controls[2].state == WorldLightingState::Refused
        && requestedReading.controls[2].requested && !requestedReading.controls[2].effective,
      worldLightingStateName(requestedReading.controls[2].state));
    check("ledger.requested.bump2", contains(requestedReading.controls[3].reason,
        "--world-material-bump is off"), requestedReading.controls[3].reason);
  }

  emit("selfTest", failures == 0 ? "true" : "false");
  return failures == 0 ? 0 : 1;
}

// ---------------------------------------------------------------------------
// 3. a real map
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

void emitFeature(const char* layer, const WorldMaterialFeatureCount& count) {
  emitNumber(prefixOf(layer, "declared"), static_cast<long long>(count.declared));
  emitNumber(prefixOf(layer, "visible"), static_cast<long long>(count.visibleDeclared));
  emitNumber(prefixOf(layer, "resolved"), static_cast<long long>(count.resourcesResolved));
  emitNumber(prefixOf(layer, "missing"), static_cast<long long>(count.resourcesMissing));
  emit(prefixOf(layer, "first"), count.firstMaterial.empty() ? "-" : count.firstMaterial);
  emit(prefixOf(layer, "resource"), count.firstResource.empty() ? "-" : count.firstResource);
  emit(prefixOf(layer, "reason"), count.reason.empty() ? "-" : jsonEscape(count.reason));
}

int runMap(const std::filesystem::path& mapPath, const std::filesystem::path& tfRoot,
    bool materialBump, bool bump2, bool detail, bool listDetail, bool listBump,
    const std::string& cameraMaterial) {
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
  for (const auto& triangle : map.triangles) {
    if (!triangle.material.empty()) ++trianglesByMaterial[triangle.material];
  }
  std::vector<VmtMaterial> materials;
  std::vector<std::string> names;
  std::vector<WorldMaterialFeatureInput> inputs;
  for (const auto& entry : trianglesByMaterial) {
    VmtMaterial material;
    const auto bytes = reader.read(VmtParser::resourcePath(entry.first, ".vmt"));
    if (!bytes.empty()) VmtParser::parse(std::string(bytes.begin(), bytes.end()), material);
    names.push_back(entry.first);
    materials.push_back(std::move(material));
  }
  for (std::size_t index = 0; index < names.size(); ++index) {
    WorldMaterialFeatureInput input;
    input.material = names[index];
    input.vmt = &materials[index];
    input.triangles = trianglesByMaterial[names[index]];
    input.visibleTriangles = 0;   // the probe has no camera; the product counts these
    inputs.push_back(std::move(input));
  }

  WorldFeatureResolvers resolvers;
  resolvers.resourceExists = [&](const std::string& path) { return !reader.read(path).empty(); };
  resolvers.readResource = [&](const std::string& path) { return reader.read(path); };

  const auto features = scanWorldMaterialFeatures(inputs, resolvers);
  emitNumber("feature.materials", static_cast<long long>(inputs.size()));
  emitFeature("bumpmap", features.bumpMap);
  emitFeature("bumpmap2", features.bumpMap2);
  emitFeature("ssbump", features.ssBump);
  emitFeature("detail", features.detail);
  emitNumber("feature.bumpmap.distinctPaths", static_cast<long long>(features.distinctBumpPaths));
  emitNumber("feature.bumpmap.nonSsBump", static_cast<long long>(features.bumpMapWithoutSsBump));
  emitNumber("feature.bumpmap2.ssBump", static_cast<long long>(features.bumpMap2SsBump));
  emitNumber("feature.ssbump.declared", static_cast<long long>(features.ssBumpDeclared));
  emitNumber("feature.detail.typesRead", static_cast<long long>(features.detailTypesRead));
  emit("feature.detail.firstType",
    features.firstDetailType.empty() ? "-" : features.firstDetailType);

  // The ledger, driven by the same switches the product has.
  WorldLightingPassState pass;
  pass.materialBumpRequested = materialBump;
  pass.materialBumpEffective = materialBump;
  pass.materialBumpMaterials = materialBump ? features.bumpMapWithoutSsBump : 0;
  pass.bump2Requested = bump2;
  pass.bump2Effective = bump2 && materialBump;
  pass.bump2Materials = pass.bump2Effective ? features.bumpMap2.declared : 0;
  pass.detailRequested = detail;
  pass.detailEffective = detail;
  pass.detailMaterials = detail ? features.detail.declared : 0;
  const auto lighting = scanWorldLightingSemantics(inputs, resolvers, pass);
  for (std::size_t index = 0; index < lighting.controls.size(); ++index) {
    const auto& control = lighting.controls[index];
    emit("lighting." + control.name + ".state", worldLightingStateName(control.state));
    emit("lighting." + control.name + ".reason", jsonEscape(control.reason));
  }

  if (listDetail) {
    for (std::size_t index = 0; index < names.size(); ++index) {
      const auto& material = materials[index];
      if (material.detailTexture.empty()) continue;
      const auto vmtPath = VmtParser::resourcePath(material.detailTexture, ".vmt");
      const bool vmt = !reader.read(vmtPath).empty();
      const bool inChain = !reader.readChainOnly(vmtPath).empty();
      // The mode the pass would run, and the mode it will refuse. `$detailscale`
      // is printed because it is baked into the tile: two materials with the same
      // detail texture and different scales do not draw the same pixels.
      emit("detail.item", names[index] + "|" + vmtPath
        + "|vmt=" + (vmt ? "1" : "0")
        + "|chain=" + (inChain ? "1" : "0")
        + "|mode=" + std::to_string(material.detailBlendMode)
        + (material.detailBlendModeUnsupported ? "|refused=1" : "|refused=0")
        + "|factor=" + std::to_string(material.detailBlendFactor)
        + "|scale=" + std::to_string(material.detailScale));
    }
  }
  if (listBump) {
    for (std::size_t index = 0; index < names.size(); ++index) {
      const auto& material = materials[index];
      if (material.bumpMap.empty()) continue;
      const auto path = VmtParser::resourcePath(material.bumpMap, ".vtf");
      emit("bump.item", names[index] + "|" + path
        + "|ssbump=" + (material.ssBump ? "1" : "0")
        + "|resolves=" + (reader.read(path).empty() ? "0" : "1")
        + "|applies=" + ((material.ssBump || !materialBump) ? "0" : "1"));
    }
  }
  if (!cameraMaterial.empty()) {
    // A camera that is guaranteed to contain one material. The `$bumpmap2`
    // branch is the reason this exists: the only material in this map that
    // declares `$bumpmap2` without `$ssbump` has 128 triangles, all PVS-visible,
    // and moving the observer camera to them is the difference between "the
    // branch drew" and "the branch drew where nothing was looking".
    //
    // The pose is derived from the largest triangle the material owns: the eye
    // sits `distance` units along its normal (so a floor is looked at from
    // above), pitched almost straight down. Yaw is 0 because a top-down view's
    // yaw only rotates the frame; it is printed so the gate passes exactly what
    // was measured rather than reconstructing it.
    std::string wanted = cameraMaterial;
    std::transform(wanted.begin(), wanted.end(), wanted.begin(),
      [](unsigned char character) { return static_cast<char>(std::toupper(character)); });
    const BspTriangle* best = nullptr;
    double bestArea = 0.0;
    std::size_t owned = 0;
    for (const auto& triangle : map.triangles) {
      std::string name = triangle.material;
      std::transform(name.begin(), name.end(), name.begin(),
        [](unsigned char character) { return static_cast<char>(std::toupper(character)); });
      if (name != wanted) continue;
      ++owned;
      const double ax = triangle.b.x - triangle.a.x, ay = triangle.b.y - triangle.a.y,
                   az = triangle.b.z - triangle.a.z;
      const double bx = triangle.c.x - triangle.a.x, by = triangle.c.y - triangle.a.y,
                   bz = triangle.c.z - triangle.a.z;
      const double cx = ay * bz - az * by, cy = az * bx - ax * bz, cz = ax * by - ay * bx;
      const double area = cx * cx + cy * cy + cz * cz;
      if (area > bestArea) { bestArea = area; best = &triangle; }
    }
    if (!best) {
      emit("camera.status", "no-triangles");
      check("camera.material.present", false, cameraMaterial + " has no triangle in this map");
    } else {
      const double cx = (best->a.x + best->b.x + best->c.x) / 3.0;
      const double cy = (best->a.y + best->b.y + best->c.y) / 3.0;
      const double cz = (best->a.z + best->b.z + best->c.z) / 3.0;
      // The geometric normal, from the winding. BspTriangle carries no normal
      // field -- the renderer derives it -- so the probe derives it the same way.
      // It is flipped to point up when it points down: a floor whose winding is
      // reversed should still be looked at from above, and the eye's side is the
      // only thing this pose depends on.
      const double ax = best->b.x - best->a.x, ay = best->b.y - best->a.y,
                   az = best->b.z - best->a.z;
      const double bx = best->c.x - best->a.x, by = best->c.y - best->a.y,
                   bz = best->c.z - best->a.z;
      double nx = ay * bz - az * by, ny = az * bx - ax * bz, nz = ax * by - ay * bx;
      const double length = std::sqrt(nx * nx + ny * ny + nz * nz);
      if (length > 1e-9) { nx /= length; ny /= length; nz /= length; }
      if (nz < 0.0) { nx = -nx; ny = -ny; nz = -nz; }
      const double distance = 96.0;
      char eye[96] = {0};
      std::snprintf(eye, sizeof(eye), "%.3f %.3f %.3f", cx + nx * distance,
        cy + ny * distance, cz + nz * distance);
      // The pose, from the normal the eye was placed along. `demoViewForward`
      // (demo_header.cpp) is
      //     forward = (cos(pitch)cos(yaw), cos(pitch)sin(yaw), -sin(pitch))
      // and the camera must look back at the centroid, so forward = -n:
      //     -sin(pitch) = -nz  ->  pitch = asin(nz)
      //     yaw         = atan2(-ny, -nx)
      // Until 2026-10-19 this pair was the literal "-89" and "0", which looks
      // straight UP for a floor (forward.z = +1) and is why two debug-camera
      // captures of this material framed terrain and could not have framed it.
      const double pitch = std::asin(std::max(-1.0, std::min(1.0, nz))) * 180.0 / 3.14159265358979323846;
      const double yaw = std::atan2(-ny, -nx) * 180.0 / 3.14159265358979323846;
      char pose[64] = {0};
      std::snprintf(pose, sizeof(pose), "%.1f %.1f", pitch, yaw);
      emit("camera.material", cameraMaterial);
      emit("camera.triangles", std::to_string(owned));
      emit("camera.normal", std::to_string(nx) + " " + std::to_string(ny) + " "
        + std::to_string(nz));
      emit("camera.eye", eye);
      emit("camera.pitch", std::to_string(pitch));
      emit("camera.yaw", std::to_string(yaw));
      emit("camera.pose", pose);
      emit("camera.status", "ok");
    }
  }
  return failures == 0 ? 0 : 1;
}

} // namespace

int main(int argc, char** argv) {
  bool selfTest = false, listDetail = false, listBump = false;
  bool materialBump = false, bump2 = false, detail = false, decodeStats = false;
  std::string cameraMaterial;
  std::filesystem::path mapPath, tfRoot;
  std::string resource;
  for (int index = 1; index < argc; ++index) {
    const std::string arg = argv[index];
    if (arg == "--self-test") { selfTest = true; continue; }
    if (arg == "--map" && index + 1 < argc) { mapPath = std::filesystem::u8path(argv[++index]); continue; }
    if (arg == "--tf-root" && index + 1 < argc) { tfRoot = std::filesystem::u8path(argv[++index]); continue; }
    if (arg == "--resource" && index + 1 < argc) { resource = argv[++index]; continue; }
    if (arg == "--material-bump" && index + 1 < argc) { materialBump = std::string(argv[++index]) == "on"; continue; }
    if (arg == "--bump2" && index + 1 < argc) { bump2 = std::string(argv[++index]) == "on"; continue; }
    if (arg == "--detail" && index + 1 < argc) { detail = std::string(argv[++index]) == "on"; continue; }
    if (arg == "--list-detail") { listDetail = true; continue; }
    if (arg == "--list-bump") { listBump = true; continue; }
    if (arg == "--camera" && index + 1 < argc) { cameraMaterial = argv[++index]; continue; }
    if (arg == "--stats") { decodeStats = true; continue; }
    std::cerr << "usage: world_feature_probe --self-test |"
                 " --map <map.bsp> --tf-root <dir> [--material-bump on|off] [--bump2 on|off]"
                 " [--detail on|off] [--list-detail] [--list-bump] [--camera <material>] |"
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
    // T27: how far a normal map actually is from neutral. A decode that lands on
    // (128,128,255) everywhere is a normal map that cannot tilt anything, and
    // "the feature is on and no pixel moved" then has a measured cause instead
    // of being reported as a broken branch. Mean |c - 128| over RGB, and the
    // fraction of texels whose channels deviate by more than 8.
    if (decodeStats && !bytes.empty()) {
      VtfTexture texture;
      if (texture.parse(bytes) && !texture.header().isCubemap()) {
        const auto rgba = texture.decodeRgba(bytes);
        if (!rgba.empty()) {
          double sum = 0.0;
          std::size_t texels = 0, strong = 0;
          for (std::size_t at = 0; at + 3 < rgba.size(); at += 4u) {
            int worst = 0;
            for (int channel = 0; channel < 3; ++channel) {
              const int value = static_cast<int>(rgba[at + static_cast<std::size_t>(channel)]);
              const int deviation = std::abs(value - 128);
              sum += deviation;
              if (deviation > worst) worst = deviation;
            }
            if (worst > 8) ++strong;
            ++texels;
          }
          emitNumber("vtf.width", static_cast<long long>(texture.header().width));
          emitNumber("vtf.height", static_cast<long long>(texture.header().height));
          emitNumber("vtf.texels", static_cast<long long>(texels));
          emitReal("vtf.meanDeviation", texels ? sum / (3.0 * static_cast<double>(texels)) : 0.0);
          emitReal("vtf.strongFraction",
            texels ? static_cast<double>(strong) / static_cast<double>(texels) : 0.0);
        }
      }
    }
  }
  if (!mapPath.empty()) {
    if (tfRoot.empty()) { std::cerr << "--map needs --tf-root\n"; return 2; }
    result |= runMap(mapPath, tfRoot, materialBump, bump2, detail, listDetail, listBump,
      cameraMaterial);
  }
  if (!selfTest && mapPath.empty() && resource.empty()) {
    std::cerr << "nothing to do: pass --self-test, --map or --resource\n";
    return 2;
  }
  return result;
}
