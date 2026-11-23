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
    control.requested = pass.bumpRequested || pass.materialBumpRequested;
    // T27: two paths can make this row applied, and they are not the same
    // claim. `pass.bumpEffective` is T24's one-slot diagnostic; the per-material
    // path is the renderer's atlas plus a vertex bit. `materialPath` decides
    // which sentence the reason gets.
    const bool materialPath = pass.materialBumpEffective;
    control.effective = pass.bumpEffective || materialPath;
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
    if (materialPath) {
      control.reason = "on: " + std::to_string(pass.materialBumpMaterials)
        + " materials sample their own $bumpmap tile through a per-face tangent basis built from"
        " the BSP texinfo axes; of the " + std::to_string(control.declared)
        + " materials that declare $bumpmap, " + std::to_string(features.ssBumpDeclared)
        + " set $ssbump and are refused by name (a bumpBasis weight is not a tangent-space normal)";
    } else if (pass.bumpEffective) {
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
      control.reason = "off: the world pass has one bump sampler slot"
        " (--world-bump) and the per-material path is off too (--world-material-bump), so the "
        + std::to_string(control.visibleDeclared) + " visible materials that name one of "
        + std::to_string(features.distinctBumpPaths) + " distinct $bumpmap textures are drawn"
        " with no normal map at all";
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
    // T27: layer 2's normal is drawn when its own switch is on AND the material
    // path is on, because it is blended into layer 1's normal and there is
    // nothing to blend toward otherwise. `effective` is the renderer's reading,
    // not this arithmetic: a batch with no bump2 tile leaves it false.
    control.requested = pass.bump2Requested;
    control.effective = pass.bump2Effective;
    if (control.effective) {
      control.state = WorldLightingState::Applied;
    } else if (control.declared > 0 || control.requested) {
      control.state = WorldLightingState::Refused;
    } else {
      control.state = WorldLightingState::NotDeclared;
    }
    if (control.declared == 0) {
      control.reason = "no material declares $bumpmap2";
    } else if (control.effective) {
      control.reason = "on: " + std::to_string(pass.bump2Materials) + " of "
        + std::to_string(features.bumpMap2.declared) + " declared materials draw layer 2; each"
        " lerps its layer-2 normal into the base normal by the same vertex weight that selects"
        " the albedo layer (SDK BUMPMAP2 block: `vNormal.xyz = lerp(vNormal.xyz, vNormal2.xyz,"
        " blendfactor)`), which is why the two layers cannot disagree about where the boundary is";
      // T27: "layer 2 draws" and "layer 2 draws from the resource its VMT names"
      // are separate claims, and this map only supports the first. Saying the
      // forced count here keeps the ledger from reading as the second.
      if (pass.bump2Diagnostic && pass.bump2DiagnosticBase > 0) {
        control.reason += "; " + std::to_string(pass.bump2DiagnosticBase) + " of them set"
          " $ssbump, so their layer-1 normal is refused and the lerp starts from the base"
          " normal this pass already computed -- the diagnostic measures the shader arm, not"
          " Source's bumpBasis";
      } else if (features.bumpMap2SsBump > 0) {
        control.reason += "; " + std::to_string(features.bumpMap2SsBump) + " declared"
          " materials set $ssbump, so their layer-1 normal is refused and the product path has"
          " no base normal to lerp layer 2 into (--world-bump2-texture names a diagnostic base)";
      }
      if (pass.bump2Forced > 0) {
        control.reason += "; " + std::to_string(pass.bump2Forced) + " of them sample the"
          " --world-bump2-texture diagnostic, not the VTF their $bumpmap2 names ("
          + std::to_string(features.bumpMap2.declared - features.bumpMap2SsBump) + " of "
          + std::to_string(features.bumpMap2.declared) + " declared second-layer textures are not"
          " SSBump)";
      }
    } else if (control.requested) {
      control.reason = "requested but refused: $bumpmap2 is blended into layer 1's normal and"
        " --world-material-bump is off, so layer 1's normal is not sampled either";
    } else {
      control.reason = features.bumpMap2.reason;
    }
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
            + " referenced detail VMTs are absent from this install, so the layer's own texture and"
            " blend mode cannot be resolved here; %detailtype is a key in the material that names"
            " the layer, not in the detail VMT, and is read above"
          : std::to_string(control.resolved) + " detail VMT(s) read, "
            + std::to_string(features.detailTypesRead) + " distinct %detailtype value(s)"
            + (features.firstDetailType.empty() ? std::string()
              : " (first=" + features.firstDetailType + ")"));
      // T27: the key and the layer are two different things, and this row is
      // about the key. `%detailtype` names a detail-prop group that VBSP reads
      // at map compile time to emit prop_detail sprites and models from the
      // map's `detail.vbsp`; it never reaches a runtime shader, so no pass has a
      // consumer for it -- hence the sentence is unconditional. Attaching it to
      // `detailEffective` made the row read as if switching the layer off also
      // switched the key's status, which it does not. The count comes first
      // because "1 material names a group" and "0 read" are different claims,
      // and only the first one is true here.
      if (features.detailTypesRead > 0) {
        control.reason += "; " + std::to_string(features.detailTypesRead) + " material"
          + (features.detailTypesRead == 1 ? " names" : "s name") + " a %detailtype group"
          + (features.firstDetailType.empty() ? std::string()
            : " (first=" + features.firstDetailType + ")");
      }
      control.reason += "; %detailtype names a detail-prop group that VBSP consumes at map"
        " compile time (it emits prop_detail sprites and models there), so the key never reaches"
        " a runtime shader and has no consumer here";
      if (pass.detailEffective) {
        control.reason += "; the $detail layer itself draws for "
          + std::to_string(pass.detailMaterials) + " materials";
      }
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
    if (control.declared > 0 && pass.materialBumpEffective) {
      // T27 measured what the refusal costs: the per-material path is live, and
      // this is the subset it cannot take. Both numbers are counted, so the
      // subtraction is checkable rather than asserted.
      control.reason = std::to_string(features.ssBumpDeclared) + " of "
        + std::to_string(features.bumpMap.declared) + " materials that declare $bumpmap set"
        " $ssbump; Source compiles that as `BUMPMAP == 2`, where the texel is NOT remapped with"
        " `* 2 - 1` and is consumed as weights on bumpBasis[0..2] against three lightmap"
        " directions (lightmappedgeneric_ps2_3_x.h). This pass has one lightmap and no basis, so"
        " those materials keep the flat geometric normal while the other "
        + std::to_string(features.bumpMapWithoutSsBump) + " apply their own normal map";
    }
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
