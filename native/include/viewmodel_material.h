#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "asset_root.h"
#include "model_loader.h"

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
// `uploadSlot` names the one slot whose pixels go to the GPU (the drawn slot, or
// -1 to upload none). The rest are decoded and discarded. That asymmetry is the
// whole reason `texturesUploaded` and `materialResolved` are separate readings.
ViewModelMaterialDecodeSummary decodeViewModelMaterialSlots(ViewModelMaterialTable& table,
  std::int32_t uploadSlot, const ViewModelMaterialUploader& uploader);

} // namespace tf2::native
