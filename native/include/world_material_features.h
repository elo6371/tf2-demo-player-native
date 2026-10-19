#pragma once

// T19 (2026-10-19): what the reference maps' materials ask the shader for, and
// what the shader does about it.
//
// WHY THIS FILE EXISTS
// --------------------
// The world pass has one bump sampler, one uv per vertex and one albedo tile.
// TF2's materials ask for more than that: measured on cp_snakewater_final1's
// 155 distinct materials, 51 declare `$bumpmap`, 51 declare `$detail`, 41
// declare `$ssbump`, 3 name `%detailtype` (in the material `$detail` points at)
// and 1 declares `$bumpmap2`. Before this file none of those numbers existed,
// so "the world is drawn with one material's bump map" and "every material's
// bump map is applied" read the same from a frame.
//
// The scan is shared by the probe and the product for the usual reason: a gate
// that re-derives the classification would be checking a copy of the rule. The
// resource lookups are injected, so the same scan runs against the real install
// and against a fixture whose only material is the one under test.
//
// WHAT IT DOES NOT DO
// -------------------
// It does not draw anything, and it does not claim a feature is implemented.
// Each count carries the named refusal the pass's current shape implies -- the
// task's rule is that an unsupported feature is refused by name, not left
// silent. Implementing detail layers or a per-material bump needs a second
// scaled uv and a per-material sampler, which is a material-system change and
// not something this scan can pretend to have made.

#include "vmt_material.h"

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace tf2::native {

// One material as the scan sees it. `vmt` is null when the material's VMT did
// not resolve or did not parse -- a separate fact from "it declares nothing",
// which is why the pointer is nullable rather than a default-constructed value.
struct WorldMaterialFeatureInput {
  std::string material;
  const VmtMaterial* vmt = nullptr;
  std::size_t triangles = 0;
  std::size_t visibleTriangles = 0;
};

struct WorldMaterialFeatureCount {
  std::size_t declared = 0;
  // Declared *and* carrying at least one pvs-visible triangle: the subset that
  // can change the reference frame.
  std::size_t visibleDeclared = 0;
  std::size_t resourcesResolved = 0;
  std::size_t resourcesMissing = 0;
  // The refusal, named, with the measured counts in it. Empty when no material
  // declared the key.
  std::string reason;
  std::string firstMaterial;
  std::string firstResource;
};

struct WorldMaterialFeatureReading {
  WorldMaterialFeatureCount bumpMap;
  WorldMaterialFeatureCount bumpMap2;
  WorldMaterialFeatureCount ssBump;
  WorldMaterialFeatureCount detail;
  // Distinct `$bumpmap` paths across the materials. One is what the pass's
  // single bump sampler can be; more than one is the measurement behind the
  // refusal below.
  std::size_t distinctBumpPaths = 0;
  std::size_t detailTypesRead = 0;
  std::string firstDetailType;
  std::string summary;
};

// The two lookups the scan needs, injected so the probe and the product use
// their own store (loose, VPK, BSP pakfile) rather than a third copy.
struct WorldFeatureResolvers {
  // "Does this resource path resolve?"
  std::function<bool(const std::string&)> resourceExists;
  // "Give me the bytes of this resource path", for the detail material's own
  // `%detailtype`. May be empty, in which case the type is simply not read.
  std::function<std::vector<std::uint8_t>(const std::string&)> readResource;
};

WorldMaterialFeatureReading scanWorldMaterialFeatures(
  const std::vector<WorldMaterialFeatureInput>& materials,
  const WorldFeatureResolvers& resolvers);

} // namespace tf2::native
