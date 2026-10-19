#include "world_lighting.h"

#include <cstdio>
#include <unordered_set>

namespace tf2::native {
namespace {

std::string countPair(std::size_t part, std::size_t whole) {
  char buffer[64] = {0};
  std::snprintf(buffer, sizeof(buffer), "%zu/%zu", part, whole);
  return buffer;
}

// The sentence every pass switch shares. It states the arithmetic the shader
// performs, because "dynamic lighting" and "specular" are words, and a reader
// who wants to refute the row needs the expression it is talking about.
std::string passScope(std::size_t materials, std::size_t triangles) {
  return std::to_string(materials) + " visible materials / "
    + std::to_string(triangles) + " visible triangles";
}

} // namespace

const char* worldLightingKindName(WorldLightingKind kind) {
  switch (kind) {
    case WorldLightingKind::PassSwitch: return "pass-switch";
    case WorldLightingKind::MaterialKey: return "material-key";
  }
  return "unknown";
}

const char* worldLightingStateName(WorldLightingState state) {
  switch (state) {
    case WorldLightingState::Applied: return "applied";
    case WorldLightingState::Disabled: return "disabled";
    case WorldLightingState::Refused: return "refused";
    case WorldLightingState::NotDeclared: return "not-declared";
  }
  return "unknown";
}

WorldLightingReading scanWorldLightingSemantics(
    const std::vector<WorldMaterialFeatureInput>& materials,
    const WorldFeatureResolvers& resolvers,
    const WorldLightingPassState& pass) {
  WorldLightingReading reading;
  // One classification, two views: the T19 feature scan already counts the four
  // material keys and writes the reasons this ledger reuses. A second walk that
  // re-derived them is the copy-of-the-rule the feature file warns about.
  const auto features = scanWorldMaterialFeatures(materials, resolvers);

  std::size_t visibleMaterials = 0;
  std::size_t visibleTriangles = 0;
  std::size_t bumpTriangles = 0;
  std::size_t bump2Triangles = 0;
  std::size_t ssBumpTriangles = 0;
  std::size_t detailTriangles = 0;
  std::unordered_set<std::string> phongExponents;
  for (const auto& entry : materials) {
    if (!entry.vmt) continue;
    const VmtMaterial& vmt = *entry.vmt;
    const bool visible = entry.visibleTriangles > 0;
    if (visible) {
      ++visibleMaterials;
      visibleTriangles += entry.visibleTriangles;
      if (vmt.phong) ++reading.phongVisible;
    }
    if (vmt.phong) {
      ++reading.phongDeclared;
      if (reading.firstPhongMaterial.empty()) reading.firstPhongMaterial = entry.material;
      const auto exponent = vmt.fields.find("$phongexponent");
      if (exponent != vmt.fields.end() && !exponent->second.empty()) {
        ++reading.phongExponentDeclared;
        phongExponents.insert(exponent->second);
        if (reading.firstPhongExponent.empty()) reading.firstPhongExponent = exponent->second;
      }
    }
    if (visible && !vmt.bumpMap.empty()) bumpTriangles += entry.visibleTriangles;
    if (visible && !vmt.bumpMap2.empty()) bump2Triangles += entry.visibleTriangles;
    if (visible && vmt.ssBump) ssBumpTriangles += entry.visibleTriangles;
    if (visible && !vmt.detailTexture.empty()) detailTriangles += entry.visibleTriangles;
  }
  reading.phongExponentDistinct = phongExponents.size();

  // ---- 0. dynamicLighting -------------------------------------------------
  {
    WorldLightingControl& control = reading.controls[0];
    control.name = "dynamicLighting";
    control.kind = WorldLightingKind::PassSwitch;
    control.requested = pass.dynamicLightingRequested;
    control.effective = pass.dynamicLightingEffective;
    control.resolved = control.effective ? 1u : 0u;
    control.visibleMaterials = visibleMaterials;
    control.visibleTriangles = visibleTriangles;
    if (control.effective) {
      control.state = WorldLightingState::Applied;
    } else if (control.requested) {
      control.state = WorldLightingState::Refused;
    } else {
      control.state = WorldLightingState::Disabled;
    }
    control.reason = "a pass constant, not a material key: the pixel shader replaces the lightmap-only"
      " factor with 0.62+0.38*dot(normal, lightDirection) for " + passScope(visibleMaterials, visibleTriangles)
      + "; no VMT declares it, so every surface is shaded by the same guessed direction and the baked"
      " lightmap's own direct light is counted twice"
      + std::string(control.effective ? "; applied"
        : (control.requested ? "; requested but no constant was uploaded" : "; off"));
  }

  // ---- 1. specular --------------------------------------------------------
  {
    WorldLightingControl& control = reading.controls[1];
    control.name = "specular";
    control.kind = WorldLightingKind::PassSwitch;
    control.requested = pass.specularRequested;
    control.effective = pass.specularEffective;
    control.resolved = control.effective ? 1u : 0u;
    control.visibleMaterials = visibleMaterials;
    control.visibleTriangles = visibleTriangles;
    if (control.effective) {
      control.state = WorldLightingState::Applied;
    } else if (control.requested) {
      control.state = WorldLightingState::Refused;
    } else {
      control.state = WorldLightingState::Disabled;
    }
    control.reason = "a pass constant: one lobe (boost 0.12, exponent 32) for "
      + passScope(visibleMaterials, visibleTriangles);
    if (reading.phongVisible == 0) {
      control.reason += "; no visible material declares $phong, so the lobe is added to materials"
        " that never asked for one";
    } else {
      control.reason += "; " + std::to_string(reading.phongVisible)
        + " of those materials declare $phong and " + std::to_string(reading.phongExponentDeclared)
        + " declare $phongexponent (" + std::to_string(reading.phongExponentDistinct)
        + " distinct value(s)"
        + (reading.firstPhongExponent.empty() ? std::string()
          : ", first=" + reading.firstPhongExponent) + "), and the pass reads none of them"
        + (reading.firstPhongMaterial.empty() ? std::string()
          : "; first=$phong material " + reading.firstPhongMaterial);
    }
    control.reason += control.effective ? "; applied"
      : (control.requested ? "; requested but no constant was uploaded" : "; off");
  }

  // ---- 2. bump ------------------------------------------------------------
  {
    WorldLightingControl& control = reading.controls[2];
    control.name = "bump";
    control.kind = WorldLightingKind::PassSwitch;
    control.declared = features.bumpMap.declared;
    control.visibleDeclared = features.bumpMap.visibleDeclared;
    control.resolved = features.bumpMap.resourcesResolved;
    control.missing = features.bumpMap.resourcesMissing;
    control.visibleMaterials = features.bumpMap.visibleDeclared;
    control.visibleTriangles = bumpTriangles;
    control.requested = pass.bumpRequested;
    control.effective = pass.bumpEffective;
    control.firstMaterial = features.bumpMap.firstMaterial;
    control.firstResource = pass.bumpOwnerTexture.empty()
      ? features.bumpMap.firstResource : pass.bumpOwnerTexture;
    control.parsed = features.bumpMap.declared > 0;
    if (control.effective) {
      control.state = WorldLightingState::Applied;
    } else if (control.declared > 0 || control.requested) {
      control.state = WorldLightingState::Refused;
    } else {
      control.state = WorldLightingState::NotDeclared;
    }
    if (control.effective) {
      const std::string source = pass.bumpTextureSource == "material"
        ? "named by the slot's owner " + pass.bumpOwnerMaterial
        : (pass.bumpTextureSource == "forced"
          ? "bound by a diagnostic switch, not by a material; the slot's owner "
            + (pass.bumpOwnerMaterial.empty() ? std::string("-") : pass.bumpOwnerMaterial)
            + " declares no $bumpmap"
          : "bound, with no recorded source");
      control.reason = "on: the pass has one bump sampler and it holds "
        + (pass.bumpOwnerTexture.empty() ? std::string("$bumpmap") : pass.bumpOwnerTexture)
        + " (" + source + "); " + std::to_string(control.declared)
        + " materials declare $bumpmap and name " + std::to_string(features.distinctBumpPaths)
        + " distinct textures, so the surfaces that did not name this one are shaded by another"
        " material's normal map";
    } else if (control.requested) {
      control.reason = "requested but refused: no bump texture is bound; the slot's owner "
        + (pass.bumpOwnerMaterial.empty() ? std::string("-") : pass.bumpOwnerMaterial)
        + (pass.bumpOwnerDeclared ? " declares a $bumpmap whose VTF did not decode"
                                  : " declares no $bumpmap")
        + ", while " + std::to_string(control.visibleDeclared) + " visible materials name "
        + std::to_string(features.distinctBumpPaths) + " distinct $bumpmap textures and the pass"
        " has one sampler slot";
    } else {
      control.reason = "off, and refused when on: the world pass has one bump sampler slot, but the"
        " materials name " + std::to_string(features.distinctBumpPaths) + " distinct $bumpmap"
        " textures across " + std::to_string(control.visibleDeclared) + " visible materials, so no"
        " material's own normal map is applied";
    }
  }

  // ---- 3. $bumpmap2 -------------------------------------------------------
  {
    WorldLightingControl& control = reading.controls[3];
    control.name = "$bumpmap2";
    control.kind = WorldLightingKind::MaterialKey;
    control.declared = features.bumpMap2.declared;
    control.visibleDeclared = features.bumpMap2.visibleDeclared;
    control.resolved = features.bumpMap2.resourcesResolved;
    control.missing = features.bumpMap2.resourcesMissing;
    control.visibleMaterials = features.bumpMap2.visibleDeclared;
    control.visibleTriangles = bump2Triangles;
    control.parsed = control.declared > 0;
    control.firstMaterial = features.bumpMap2.firstMaterial;
    control.firstResource = features.bumpMap2.firstResource;
    control.state = control.declared > 0 ? WorldLightingState::Refused
                                       : WorldLightingState::NotDeclared;
    control.reason = features.bumpMap2.declared == 0
      ? "no material declares $bumpmap2"
      : features.bumpMap2.reason;
  }

  // ---- 4. %detailtype -----------------------------------------------------
  {
    WorldLightingControl& control = reading.controls[4];
    control.name = "%detailtype";
    control.kind = WorldLightingKind::MaterialKey;
    control.declared = features.detail.declared;
    control.visibleDeclared = features.detail.visibleDeclared;
    control.resolved = features.detail.resourcesResolved;
    control.missing = features.detail.resourcesMissing;
    control.visibleMaterials = features.detail.visibleDeclared;
    control.visibleTriangles = detailTriangles;
    control.firstMaterial = features.detail.firstMaterial;
    control.firstResource = features.detail.firstResource;
    // The key lives in the detail material, so it is parsed only when at least
    // one detail VMT resolved AND named it. "0 read" therefore has two possible
    // causes, and the reason separates them.
    control.parsed = features.detailTypesRead > 0;
    control.state = control.declared > 0 ? WorldLightingState::Refused
                                       : WorldLightingState::NotDeclared;
    if (control.declared == 0) {
      control.reason = "no material declares $detail";
    } else {
      control.reason = features.detail.reason + "; "
        + (control.resolved == 0
          ? std::to_string(control.missing) + " of " + std::to_string(control.declared)
            + " referenced detail VMTs are absent from this install, so %detailtype cannot be read"
            " here; the read path is exercised by the probe's fixture"
          : std::to_string(control.resolved) + " detail VMT(s) read, "
            + std::to_string(features.detailTypesRead) + " distinct %detailtype value(s)"
            + (features.firstDetailType.empty() ? std::string()
              : " (first=" + features.firstDetailType + ")"));
    }
  }

  // ---- 5. $ssbump ---------------------------------------------------------
  {
    WorldLightingControl& control = reading.controls[5];
    control.name = "$ssbump";
    control.kind = WorldLightingKind::MaterialKey;
    control.declared = features.ssBump.declared;
    control.visibleDeclared = features.ssBump.visibleDeclared;
    control.resolved = features.ssBump.resourcesResolved;
    control.missing = features.ssBump.resourcesMissing;
    control.visibleMaterials = features.ssBump.visibleDeclared;
    control.visibleTriangles = ssBumpTriangles;
    control.parsed = control.declared > 0;
    control.firstMaterial = features.ssBump.firstMaterial;
    control.firstResource = features.ssBump.firstResource;
    control.state = control.declared > 0 ? WorldLightingState::Refused
                                       : WorldLightingState::NotDeclared;
    control.reason = features.ssBump.declared == 0
      ? "no material declares $ssbump"
      : features.ssBump.reason;
  }

  std::size_t applied = 0, refused = 0, disabled = 0, notDeclared = 0;
  for (const auto& control : reading.controls) {
    switch (control.state) {
      case WorldLightingState::Applied: ++applied; break;
      case WorldLightingState::Refused: ++refused; break;
      case WorldLightingState::Disabled: ++disabled; break;
      case WorldLightingState::NotDeclared: ++notDeclared; break;
    }
  }
  char summary[512] = {0};
  std::snprintf(summary, sizeof(summary),
    "controls=%zu applied=%zu refused=%zu disabled=%zu notDeclared=%zu"
    " phong=%zu(visible %zu, exponents %zu/%zu) bumpOwner=%s",
    reading.controls.size(), applied, refused, disabled, notDeclared,
    reading.phongDeclared, reading.phongVisible, reading.phongExponentDeclared,
    reading.phongExponentDistinct,
    pass.bumpOwnerMaterial.empty() ? "-" : pass.bumpOwnerMaterial.c_str());
  reading.summary = summary;
  return reading;
}

} // namespace tf2::native
