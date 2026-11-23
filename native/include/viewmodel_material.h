#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "asset_root.h"
#include "model_loader.h"
#include "vmt_material.h"

// The ViewModel material chain, product side: MDL slot table -> `$cdmaterials`
// -> VMT -> `$basetexture` -> VTF -> decodable pixels.
//
// WHY THIS IS ITS OWN MODULE
//   The chain itself was proven by the T2.5 probe (see
//   `evidence/viewmodel/material-readings.txt`), and the probe is the frozen
//   witness for it. What the product needs on top of that is a *runtime* answer
//   per slot plus one decision the probe never had to make: which slot the
//   picture actually uses, which comes from the MDL mesh table rather than from
//   the slot order. Keeping both in one module is what makes the two comparable
//   -- the gate asserts the product's slot table against the probe's frozen
//   readings for the same model, and that assertion is only meaningful if they
//   share the reader and the status vocabulary.
//
// THE VOCABULARY IS FROZEN
//   `MissingMaterial` / `MissingTexture` / `InvalidPath` / `OversizedTexture` are
//   the probe's four negative classes, spelled the same way here. A consumer that
//   renames them breaks the cross-check for no gain.

namespace tf2::native {

// Where a resource was actually read from. `Rejected` is not "not found": it
// means the asset root refused the path because it escapes the root, which is a
// different defect and is counted separately.
enum class ViewModelResourceSource { None, Loose, Vpk, Rejected };

const char* viewModelResourceSourceName(ViewModelResourceSource source);

struct ViewModelResourceRead {
  std::vector<std::uint8_t> bytes;
  ViewModelResourceSource source = ViewModelResourceSource::None;
};

// The product's read order for a material resource: the loose file first, then
// every archive that holds the key. Identical to the probe's, deliberately --
// two readers that disagree about precedence would make a green gate meaningless.
ViewModelResourceRead readViewModelResource(const AssetRoot& root, const std::string& resource);

enum class ViewModelMaterialStatus {
  MissingMaterial,
  MissingTexture,
  InvalidPath,
  OversizedTexture,
  Resolved,
};

const char* viewModelMaterialStatusName(ViewModelMaterialStatus status);

// One `mstudiotexture_t` slot, resolved as far as the resource set allows.
//
// `vtfPath` is the GPU cache key and is the *normalized* resource path the
// reader used (`materials/.../x.vtf`), never the VMT's raw `$basetexture` value:
// on this install `v_scattergun.vmt` writes that value with backslashes
// (`models\weapons\v_scattergun\v_scattergun`), and caching on the raw string
// would give the same texture two keys depending on which VMT named it.
struct ViewModelMaterialSlot {
  std::size_t slot = 0;
  std::string name;
  std::string candidates;
  ViewModelMaterialStatus status = ViewModelMaterialStatus::MissingMaterial;
  std::string reason;
  std::string vmtPath;
  std::string shader;
  std::string baseTexture;
  std::string vtfPath;
  std::size_t vmtBytes = 0;
  std::size_t vtfBytes = 0;
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  std::uint32_t format = 0;
  bool pixelsDecoded = false;
  bool oversized = false;
  // Declared by the resource set, not applied by phase 1 unless the reading says
  // so. `alpha` is the texture's own alpha (format, header flags, or the VMT's
  // translucent/alphatest keys); `translucent`/`alphaTest` are the two shader
  // keys that decide whether the engine *uses* it.
  bool alpha = false;
  bool translucent = false;
  bool alphaTest = false;
  bool bump = false;
  bool phong = false;
  bool selfIllum = false;
  std::string oversizeReason;
  // Retained so the decode pass can run one slot at a time instead of holding
  // every RGBA buffer at once. Sized by the VTF, not by the decode.
  std::vector<std::uint8_t> vtfBytesData;
};

// The per-model answer: every slot's state, plus which slot the drawn mesh uses.
struct ViewModelMaterialTable {
  std::vector<ViewModelMaterialSlot> slots;
  // From `ModelMetadata::meshMaterials` for the descriptor the mesh was built
  // from. -1 is a real answer: the caller then has to say it fell back rather
  // than report a slot it did not read.
  std::int32_t drawnSlot = -1;
  std::string drawnSlotSource; // "mdl-mesh-table" / "unresolved"
  std::string drawnSlotReason;
  bool meshTableAvailable = false;
  std::size_t modelStride = 0;
  std::size_t meshStride = 0;
  std::string modelStrideSource;
  bool modelStrideExercised = false;
  bool meshStrideExercised = false;
  std::size_t meshCount = 0;
  std::size_t descriptorsValidated = 0;

  // --- the nine runtime readings -----------------------------------------
  // `materialSlots` and `materialResolved` are over the model's declared slot
  // table. `texturesUploaded` and `texturesReused` are over the GPU cache, and
  // they are deliberately *not* the same denominator: a slot can resolve and
  // still not be uploaded (nothing draws it), and the pair is what says so.
  std::size_t materialSlots() const { return slots.size(); }
  std::size_t materialResolved() const;
  std::size_t fallbackSlots() const;
  std::size_t alphaSlots() const;
  std::size_t translucentSlots() const;
  std::size_t alphaTestSlots() const;
  std::size_t phongSlots() const;
  std::size_t bumpSlots() const;
  std::size_t selfillumSlots() const;
};

// Resolve the slot table and the drawn slot. Reads no pixels: the decode is a
// separate pass so the caller can upload one slot at a time.
ViewModelMaterialTable resolveViewModelMaterialTable(const AssetRoot& root,
  const ModelMetadata& metadata, std::int32_t seenDescriptorIndex);

// Decode one slot's pixels. Returns false and fills `error` when the VTF parsed
// but produced no pixels (a cubemap, say) -- the same case the probe separates
// from a missing file.
bool decodeViewModelMaterialSlot(const ViewModelMaterialSlot& slot,
  std::vector<std::uint8_t>& rgba, std::string& error);

// Hand one decoded slot to the GPU cache. Returns true when a texture was
// created, false when the cache already had the key or the upload failed.
using ViewModelMaterialUploader = std::function<bool(const std::string& key,
  std::uint32_t width, std::uint32_t height, const std::vector<std::uint8_t>& rgba)>;

struct ViewModelMaterialDecodeSummary {
  std::size_t decoded = 0;
  std::size_t decodeFailed = 0;
  std::size_t uploaded = 0;
  std::size_t decodedBytes = 0;
  std::string firstFailure;
};

// Decode every slot that read a VTF, **one at a time**, and drop the VTF bytes
// afterwards. Two things this buys, both deliberate:
//
//   * `materialResolved` means what the frozen probe means by `pixels_decoded`:
//     the JPEG-style "the file existed" claim is not enough, a slot counts as
//     resolved only when the decoder produced pixels from it.
//   * memory. `c_scattergun` alone decodes to 32 MiB of RGBA. A loop that kept
//     every buffer alive would need that for one model and several times that for
//     `v_medigun_medic`'s seven slots, for a table whose only job is to be
//     counted.
//
// `uploadSlot` names the one slot whose pixels go to the GPU (the drawn slot),
// -2 uploads every decoded slot used by a multi-range mesh, and -1 uploads none.
// The rest are decoded and discarded. That asymmetry is the
// whole reason `texturesUploaded` and `materialResolved` are separate readings.
ViewModelMaterialDecodeSummary decodeViewModelMaterialSlots(ViewModelMaterialTable& table,
  std::int32_t uploadSlot, const ViewModelMaterialUploader& uploader);

// ---------------------------------------------------------------------------
// T2 interface freeze (2026-10-14): Phong, Bump and Selfillum
// ---------------------------------------------------------------------------
//
// `ViewModelMaterialSlot::{phong,bump,selfIllum}` are the three declarations the
// T2.5 chain already carries, and every one of them is a *declaration*: the slot
// says the VMT contains `$phong 1`, not that a specular lobe reaches a pixel.
// The task's own rule is that the two are different statements, and this half of
// the module exists to make the second one measurable.
//
// The split is deliberate:
//   * the readers below are pure and answer declared / parsed / reason. They
//     read the VMT's own text, so the probe and the product share one
//     implementation of "what did this material ask for".
//   * uploaded / applied are facts about a frame, so they belong to the
//     renderer's own sample state and are read back from the pass that drew.
// A gate that asserts the two halves against each other is only non-circular
// because the reader is shared.

// The same five words the water surface uses, spelled the same way, so a reader
// of one trace does not have to learn a second vocabulary.
enum class ViewModelEffectState { Unavailable, Declared, Parsed, Active, Fallback };
const char* viewModelEffectStateName(ViewModelEffectState state);

// The six the task names. `declared` and `parsed` come from the reader;
// `uploaded` and `applied` are filled by the caller that owns the GPU and the
// shader branch, which is why they are plain fields here rather than something
// the reader computes.
struct ViewModelEffectReport {
  ViewModelEffectState state = ViewModelEffectState::Unavailable;
  bool declared = false;
  bool parsed = false;
  bool uploaded = false;
  bool applied = false;
  bool fallback = false;
  std::string reason;
};

// $phong / $phongboost / $phongexponent / $phongfresnelranges / $phongtint.
//
// Every one of the five is optional in a shipped material and every one of them
// can be malformed, so the reader names which of the two it saw rather than
// defaulting silently: a material with `$phong 1` and no exponent is declared
// and parsed with the engine's own default, and one with `$phongexponent
// notanumber` is declared and NOT parsed, which are different rows.
struct ViewModelPhongReading : ViewModelEffectReport {
  float exponent = 5.0f;      // Source's default for VertexLitGeneric
  float boost = 1.0f;
  // [min, max] of the Fresnel range. A range with min > max is refused by name.
  float fresnel[2] = {0.0f, 0.4f};
  // [r,g,b] of $phongtint, or the engine default white. 0 when the key is absent
  // is reported as absent rather than as black, so the renderer cannot multiply
  // a highlight away and call it a tint.
  float tint[3] = {1.0f, 1.0f, 1.0f};
  bool tintDeclared = false;
  bool exponentDeclared = false;
  bool boostDeclared = false;
  bool fresnelDeclared = false;
};

// $normalmap / $normalmapalphaenvmapmask / $bumpmap.
//
// The legacy `$bumpmap` and the current `$normalmap` name the same texture and
// a shipped material may carry either. Both are reported, and neither is allowed
// to overwrite the albedo cache: the renderer keeps one key per texture role,
// which is the reading that stops a normal map from being uploaded as albedo.
struct ViewModelBumpReading : ViewModelEffectReport {
  std::string bumpMapPath;      // legacy key, normalized
  std::string normalMapPath;   // current key, normalized
  // Which of the two the pass will bind. Both present is not an error; the
  // choice is recorded so a reader can tell the two files apart.
  std::string boundPath;
  bool alphaEnvMapMask = false;
  bool reusedAlbedoSlot = false;
};

// $selfillum / $selfillummask / $selfillumtint.
//
// The mask is a separate texture from the albedo, and a material that declares
// `$selfillum 1` with no mask is a real material: Source's own default masks by
// the albedo's alpha. That default is what `maskFromAlpha` reports, so the
// fallback is a stated decision rather than a missing texture read as zero.
struct ViewModelSelfillumReading : ViewModelEffectReport {
  bool maskDeclared = false;
  bool maskFromAlpha = false;
  std::string maskPath;
  float tint[3] = {1.0f, 1.0f, 1.0f};
  bool tintDeclared = false;
};

// The three readers. `baseTexture` is the slot's own resolved albedo path and
// is passed only so a relative texture name can be resolved against it; a
// material that names its textures absolutely (every shipped one does) never
// needs it.
ViewModelPhongReading readViewModelPhong(const VmtMaterial& material);
ViewModelBumpReading readViewModelBump(const VmtMaterial& material);
ViewModelSelfillumReading readViewModelSelfillum(const VmtMaterial& material);

// What the draw did with one range, per effect. This is the range-level judge:
// it takes the three readings a range's material produced plus the facts the
// renderer knows about that range (a texture was bound, the branch compiled and
// ran) and says which ranges the effect actually reached.
//
// It is separate from the readers because it answers a question the readers
// cannot: a material can declare Phong and be drawn by a range whose texture
// never resolved, and `applied` is then false for a reason that has nothing to
// do with the VMT.
struct ViewModelRangeEffects {
  bool phongApplied = false;
  bool bumpApplied = false;
  bool selfillumApplied = false;
  bool anyApplied = false;
  std::string reason;
};

ViewModelRangeEffects evaluateViewModelRangeEffects(
  const ViewModelPhongReading& phong, const ViewModelBumpReading& bump,
  const ViewModelSelfillumReading& selfillum, bool textureBound, bool shaderReady);

} // namespace tf2::native
