#pragma once

#include <array>
#include <optional>
#include <string>
#include <vector>

#include "animation_binding.h"
#include "model_loader.h"

namespace tf2::native {

// Same clamp as native_renderer.h clampViewModelFov. NaN stays NaN because
// both comparisons are false. Kept here so ViewModel code does not include
// the D3D renderer header. The default used when the caller omits a value is 90.
inline constexpr float kDefaultViewModelFov = 90.0f;
inline constexpr float kViewModelFovLow = 40.0f;
inline constexpr float kViewModelFovHigh = 120.0f;

inline float normalizeViewModelFov(float value) {
  return value < kViewModelFovLow ? kViewModelFovLow : (value > kViewModelFovHigh ? kViewModelFovHigh : value);
}

// 80 was the ViewModel FOV default before 2026-10-10. A settings file written by
// an older build carries 80 because that was the default, not because anyone
// chose it, and the file cannot tell the two apart. Reading a stored 80 as "not
// set" is the only way an existing install picks the new default up; the next
// save writes 90. This is deliberately *not* a clamp: 41..120 still round-trips.
inline constexpr int kDefaultViewModelFovInt = 90;
inline constexpr int kLegacyViewModelFovDefaultInt = 80;

inline constexpr int migrateLegacyViewModelFov(int stored) {
  return stored == kLegacyViewModelFovDefaultInt ? kDefaultViewModelFovInt : stored;
}

enum class ViewModelHand { Right, Left };

enum class ViewModelStatus { Ok, Failed };

// A single `mstudioattachment_t`, classified. `Partial` means the transform is
// usable but something about it is suspicious (a basis vector that is not unit
// length); `Invalid` means it must not be used for alignment. One bad attachment
// never fails the whole request: the plan is explicit that a damaged attachment
// disables that alignment and leaves the model's default transform in place,
// rather than making the ViewModel disappear.
enum class ViewModelAttachmentStatus { Valid, Partial, Invalid };

struct ViewModelAttachment {
  std::string name;
  std::int32_t bone = -1;
  std::array<float, 3> origin{};
  std::array<float, 3> axisX{};
  std::array<float, 3> axisY{};
  std::array<float, 3> axisZ{};
  ViewModelAttachmentStatus status = ViewModelAttachmentStatus::Invalid;
  std::string reason;
};

// Which of the companion files actually resolved. A caller has to be able to
// tell "no MDL at all" from "MDL but no VVD" -- both used to read as the single
// string "viewmodel was not found".
struct ViewModelResourceStatus {
  bool mdlReadable = false;
  bool vvdPresent = false;
  bool vtxPresent = false;
  std::string vtxVariant; // dx90 / dx80 / sw, whichever was found
  std::size_t materialCount = 0;
  std::size_t materialResolved = 0;
  bool companionsComplete = false;
  // The draw pass is flat colour for now: the texture table is counted but not
  // bound, so a missing VMT must not be able to read as "material worked".
  bool flatColorFallback = true;
  // Whether the VTX decode produced a drawable mesh. A false here is the
  // difference between "the ViewModel had nothing to draw" and "the draw pass
  // did not run", which are otherwise both `drawn=0`.
  bool meshReady = false;
  std::string meshReason;
};

// Renderer-neutral request. It does not draw and does not upload a mesh.
//
// It is also the cache the plan asks for: the model path, the companion status,
// the mesh, the attachment table, the sequence labels and the bone mapping are
// all resolved once, when the demo or the TF root changes, and then handed to
// the renderer. Nothing here may be rebuilt per frame.
struct ViewModelRequest {
  ViewModelStatus status = ViewModelStatus::Failed;
  std::string reason;
  std::string path;
  ModelAssetResolution resolution = ModelAssetResolution::Missing;
  bool companionsComplete = false;
  float fov = kDefaultViewModelFov;
  ViewModelHand hand = ViewModelHand::Right;
  // Row-major 3x4. Right is identity. Left mirrors X, so +X becomes -X.
  std::array<float, 12> handTransform{};
  std::vector<std::string> sequenceLabels;
  std::vector<ViewModelAttachment> attachments;
  ViewModelResourceStatus resources;
  std::size_t attachmentCount = 0;
  std::size_t attachmentsValid = 0;
  std::size_t attachmentsPartial = 0;
  std::size_t attachmentsInvalid = 0;
  std::string attachmentReason;
  // The attachment the caller aligns to. Chosen by name from an ordered
  // preference list, never "the first attachment in the file": a `v_*.mdl` also
  // carries `muzzle` and `eject_brass`, and aligning the camera to those puts
  // the weapon in the wrong place. `alignmentReason` names what was rejected.
  std::string alignmentName;
  std::int32_t alignmentIndex = -1;
  std::string alignmentReason;
  ModelMeshData mesh;
  std::vector<ModelBone> bones;
};

std::array<float, 12> viewModelHandMatrix(ViewModelHand hand);

// Order in which *placement* attachments are preferred, for this hand. The
// first entry the model actually has, with a usable transform, wins.
//
// Grounded in the shipped models rather than in a name that sounded right:
// `v_scattergun_scout`, `v_bat_scout`, `v_pda_spy` and `v_grenadelauncher_demo`
// each carry `weapon_bone`, and `v_watch_spy` carries `weapon_bone_L` instead --
// the left-hand variant, which is why the hand is an input here. `muzzle`,
// `shell_eject` and `controlpanel0_*` are also attachments on those models, and
// they are deliberately *not* in this list: they mark where an effect spawns,
// not where the weapon sits. Aligning the camera to `muzzle` would put the
// weapon in the wrong place, and picking it because it was first is exactly the
// guess this list exists to prevent.
std::vector<std::string> defaultViewModelAlignmentPreferences(ViewModelHand hand);

// Records the chosen attachment in `request`. Returns its index, or -1 when the
// model has no placement attachment with a usable transform -- in which case the
// request keeps its default transform and `alignmentReason` names the
// attachments that *were* present.
std::int32_t selectViewModelAlignment(ViewModelRequest& request,
  const std::vector<std::string>& preferences);

ViewModelRequest buildViewModelRequest(const AssetRoot& root, const std::string& path,
  ViewModelHand hand, std::optional<float> fov = std::nullopt);

// ---- ViewModel animation input (T2.5) -------------------------------------
//
// T2 drew the ViewModel in its bind pose and reported `animationInput=missing`.
// What may replace that reading is a measurement, not an assumption, and
// native/tools/viewmodel_animation_probe.cpp is where it was taken. It says
// three things:
//
//   1. The input exists. TF2's server creates a networked CTFViewModel per
//      player, and DT_BaseViewModel carries m_nSequence (unsigned 8 bit) and
//      m_flPlaybackRate (float). That is the opposite of the T1 result for
//      CTFPlayer, which networks none of the three fields -- the ViewModel is a
//      different class, so the answer had to be measured rather than inherited.
//   2. One of the three fields the task named is not on the wire at all.
//      DT_BaseViewModel has no m_flCycle. `hasCycle` below is therefore a
//      first-class false rather than an omitted field: a caller cannot mistake
//      its absence for a cycle of zero.
//   3. The sequence indexes a DIFFERENT table than the model the pass draws. The
//      entity names `models/weapons/c_models/c_<class>_arms.mdl` (39..54
//      sequences); the pass draws `models/weapons/v_models/v_<weapon>.mdl` (2..9
//      sequences); observed sequences run 0..34.
//
// Point 3 is why this is a refusal contract and not a mapping. Applying the
// entity's index to the drawn model's table is wrong even when the value lands
// inside it: `inRange` measured against the wrong table is a coincidence, and
// the probe reports the two counts side by side (`vmmapping-summary ...
// inRange=37 outOfRange=104`) so the coincidence is visible instead of
// flattering. The real mapping -- entity sequence space -> activity -> `v_*`
// sequence -- is follow-up work. Until it exists the honest behaviour is: accept
// what is inside the drawn table, refuse what is not, and never present an
// accepted coincidence as a correct pose.
enum class ViewModelAnimationStatus {
  Missing,             // no owning ViewModel entity, or it carried no sequence
  Ok,                  // the sequence is inside the drawn model's own table
  SequenceOutOfRange,  // it is past the end of that table
  BoneMismatch,        // the drawn model's animation bones do not line up
};

const char* viewModelAnimationStatusName(ViewModelAnimationStatus status);

// What the owning ViewModel entity said, read from the demo's own snapshot.
// `entityPresent` and `hasSequence` are separate because they are separate
// failures: "the demo has no ViewModel entity for this player" and "it has one
// and the slot is absent" are different reasons to draw a bind pose, and a
// single flag would make them read the same.
struct ViewModelAnimationInput {
  bool entityPresent = false;
  int entityIndex = -1;
  std::string entityModelPath;  // what the entity's m_nModelIndex names
  bool hasSequence = false;
  int sequence = -1;
  bool hasPlaybackRate = false;
  float playbackRate = 0.0f;
  // DT_BaseViewModel has no m_flCycle; see point 2 above.
  bool hasCycle = false;
  float cycle = 0.0f;
};

struct ViewModelAnimationOutcome {
  ViewModelAnimationStatus status = ViewModelAnimationStatus::Missing;
  std::string reason;
  std::string sequenceLabel;
  int frame = 0;
  int frameCount = 0;
  // True for every status except Ok, and the caller has to honour it: there is
  // no partial outcome, so a bind pose with skinning off is the only thing a
  // refused input may produce.
  bool bindPose = true;
  std::vector<std::array<float, 16>> skinMatrices;
};

// The whole contract as one function, so it can be driven without a window, a
// device or a demo.
//
// `drawnBinding` is the animation binding of the model the pass draws, or
// nullptr when that model has none; `drawn` supplies that model's own sequence
// and bone counts, which is what "in range" is measured against. `timing`
// carries the tick the frame is being drawn for, and its sequence / cycle / rate
// fields are overwritten from the input -- so a caller cannot sample a sequence
// the input did not name.
ViewModelAnimationOutcome resolveViewModelAnimationInput(
  const ViewModelAnimationInput& input,
  const ViewModelRequest& drawn,
  const AnimationBinding* drawnBinding,
  const AnimationSampleRequest& timing);

} // namespace tf2::native
