#pragma once

// T24 (2026-10-19): the lighting and shader controls the world pass defers, as
// one ledger with a state and a named reason each.
//
// WHY THIS FILE EXISTS
// --------------------
// T19 named four material keys the world pass cannot draw (`$bumpmap`,
// `$bumpmap2`, `$ssbump`, `$detail`/`%detailtype`). T24's list is longer and of
// two kinds, and the two kinds were indistinguishable from a frame:
//
//   * pass switches -- `dynamicLighting`, `specular`, `bump`. These are fields
//     on RenderSettings, multiplied into one constant buffer for every world
//     triangle. They have no per-material key at all.
//   * material keys -- `$bumpmap2`, `%detailtype`, `$ssbump`. These are parsed
//     from the VMT and refused by name because the pass has one sampler slot,
//     one atlas uv and no tangent basis.
//
// Before this file, "the pass has a specular term" and "the pass applies the
// material's own `$phongexponent`" printed the same trace line, and a switch
// that was requested but never reached the constant buffer was invisible. The
// ledger makes three facts separate for every control:
//
//   declared  -- how many materials ask for it (0 for a pass switch),
//   requested -- what the settings asked the pass to do,
//   effective -- what the pass actually uploaded / bound.
//
// `requested != effective` is a named refusal, not a rounding error. That is the
// distinction a frame cannot show: a bump switch with no texture bound and a
// bump switch with the wrong material's texture both draw the same picture.
//
// WHAT IT DOES NOT DO
// -------------------
// It does not draw, and it does not claim a control reached a pixel. `effective`
// means the constant was uploaded or the sampler was bound; whether the picture
// changed is measured by an A/B capture, not inferred here. The task's rule is
// that a feature stays refused by name until BOTH parsing and rendered behavior
// have evidence, so those two are reported in different places on purpose.

#include "vmt_material.h"
#include "world_material_features.h"

#include <array>
#include <cstddef>
#include <string>
#include <vector>

namespace tf2::native {

enum class WorldLightingKind { PassSwitch, MaterialKey };
const char* worldLightingKindName(WorldLightingKind kind);

enum class WorldLightingState {
  // The pass switch is on and its constant/sampler is uploaded.
  Applied,
  // The pass switch is off. Not a refusal: the default path does not claim it.
  Disabled,
  // Declared or requested, and not drawn. `reason` names the measured cause.
  Refused,
  // Nothing declares it, so there is nothing to refuse. The row still exists,
  // because a missing row and a row reading zero would otherwise look alike.
  NotDeclared,
};
const char* worldLightingStateName(WorldLightingState state);

struct WorldLightingControl {
  std::string name;
  WorldLightingKind kind = WorldLightingKind::MaterialKey;
  WorldLightingState state = WorldLightingState::NotDeclared;
  // Material keys: materials declaring the key, and the PVS-visible subset.
  // Pass switches: 0 -- a switch has no declaration, which is the point.
  std::size_t declared = 0;
  std::size_t visibleDeclared = 0;
  std::size_t resolved = 0;
  std::size_t missing = 0;
  // What the control would shade: visible materials and their visible
  // triangles. For a pass switch this is the whole world; for a material key it
  // is the subset that names the key, which is the difference between "the
  // switch changes every pixel" and "it changes these faces".
  std::size_t visibleMaterials = 0;
  std::size_t visibleTriangles = 0;
  bool requested = false;
  bool effective = false;
  // A material key was read from the VMT (or from the detail material it
  // points at). False for pass switches: there is nothing to parse.
  bool parsed = false;
  std::string firstMaterial;
  std::string firstResource;
  std::string reason;
};

// What the pass actually did last frame. The caller fills this from the values
// it uploaded, not from the settings it read: `*Requested` is the settings
// field, `*Effective` is the constant, and a gap between them is the refusal.
struct WorldLightingPassState {
  bool dynamicLightingRequested = false;
  bool dynamicLightingEffective = false;
  bool specularRequested = false;
  bool specularEffective = false;
  bool bumpRequested = false;
  bool bumpEffective = false;
  // The single bump slot's owner. `bumpOwnerDeclared` says whether that
  // material's VMT names a `$bumpmap`; a switch that is on with no owner is
  // reported as refused rather than as applied.
  bool bumpOwnerDeclared = false;
  std::string bumpOwnerMaterial;
  std::string bumpOwnerTexture;
  // Where the bound texture came from: "material" when the slot's owner named
  // it, "forced" when a diagnostic switch bound it (the reference frame's owner
  // declares none, so the branch cannot be measured otherwise), "none" when
  // nothing is bound. The distinction is in the reason because "the pass samples
  // a bump map" and "the pass samples a map a material asked for" are different
  // claims.
  std::string bumpTextureSource;
  // ---- T27: the per-material feature path --------------------------------
  // `*Effective` is read from the renderer's own state: the atlas exists AND at
  // least one uploaded vertex carries the mask bit. That is the pair that
  // separates "the material asked" from "the shader samples it", and it is why
  // these are not simply copies of the settings field.
  bool materialBumpRequested = false;
  bool materialBumpEffective = false;
  std::size_t materialBumpMaterials = 0;
  bool bump2Requested = false;
  bool bump2Effective = false;
  std::size_t bump2Materials = 0;
  // How many of `bump2Materials` sampled a diagnostic texture rather than the
  // VTF their own `$bumpmap2` names. Counted apart because "layer 2 draws" and
  // "layer 2 draws from its declared resource" are different claims, and on this
  // map only the first is true (4 of 5 declared second-layer textures are
  // `$ssbump`, the fifth is under the map).
  std::size_t bump2Forced = 0;
  // T27: the diagnostic base. When the layer-2 texture is forced, a material
  // whose layer 1 is refused (`$ssbump`) has no per-material normal to lerp
  // toward; the shader uses the base normal the frame already computed and this
  // count says how many materials took that path. It is not Source's BUMPMAP2.
  bool bump2Diagnostic = false;
  std::size_t bump2DiagnosticBase = 0;
  bool detailRequested = false;
  bool detailEffective = false;
  std::size_t detailMaterials = 0;
};

struct WorldLightingReading {
  // Fixed order, so a trace reader and a gate index the same control:
  // 0 dynamicLighting, 1 specular, 2 bump, 3 $bumpmap2, 4 %detailtype,
  // 5 $ssbump.
  static constexpr std::size_t ControlCount = 6;
  std::array<WorldLightingControl, ControlCount> controls;
  // The per-material specular data the pass ignores. `$phong` is the switch a
  // material declares; `$phongexponent` and `$phongboost` are the values that
  // would make the lobe the material's own. Counted here because the specular
  // refusal is only measurable against them.
  std::size_t phongDeclared = 0;
  std::size_t phongVisible = 0;
  std::size_t phongExponentDeclared = 0;
  std::size_t phongExponentDistinct = 0;
  std::string firstPhongMaterial;
  std::string firstPhongExponent;
  std::string summary;
};

// `materials` is the same input the T19 feature scan takes, so the product and
// every probe classify the same set of materials with one rule. `resolvers` is
// how `$bumpmap2`, `$ssbump` and the detail material's `%detailtype` are
// resolved; pass an empty `readResource` and `%detailtype` simply reads 0.
WorldLightingReading scanWorldLightingSemantics(
  const std::vector<WorldMaterialFeatureInput>& materials,
  const WorldFeatureResolvers& resolvers,
  const WorldLightingPassState& pass);

} // namespace tf2::native
