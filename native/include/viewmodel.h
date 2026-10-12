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

// The material chain reading for the request's own model, resolved once at
// build time: MDL texture slot -> $cdmaterials -> .vmt -> $basetexture -> .vtf
// existence, through the same AssetRoot the model itself came from.
//
// THIS IS A READING, NOT A BINDING. The pass still draws flat colour (T2's
// contract, unchanged here); what the count makes possible is telling "this
// model's materials are missing" apart from "this model has no material slots
// at all" -- both of which otherwise read as the same flat-shaded picture. The
// chain stops at the VTF existing and being readable, because pixel decode is
// the material task's own measurement (viewmodel_material_probe) and repeating
// it here would double the cost of every request for a number nothing in this
// pass consumes.
struct ViewModelMaterialReading {
  std::size_t slots = 0;        // texture slots the MDL declares
  std::size_t named = 0;        // slots with at least one candidate path
  std::size_t vmtResolved = 0;  // slots whose .vmt was read and parsed
  std::size_t vtfResolved = 0;  // slots whose $basetexture resolved to a .vtf
  // The first slot that did not reach a VTF, with the resource it stopped on.
  // One named failure is what turns "0 of 2" into a repairable statement.
  std::string firstFailure;
};

// Walks that chain for one already-inspected model: MDL texture slot ->
// $cdmaterials -> .vmt -> $basetexture -> .vtf existence, through `root`.
//
// Exported because the chain has states a shipped model cannot show and a
// source edit would be needed to construct: an MDL whose texture slot names a
// material that is not in the game, and one whose material names a texture that
// is not in the game. A probe can build either by taking a real model's
// inspection and replacing one slot's candidate path -- which is the same
// function the product runs, given different input -- where a gate would have to
// edit the installation to see the same thing.
ViewModelMaterialReading resolveViewModelMaterialChain(
  const AssetRoot& root, const ModelInspection& inspection);

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
  ViewModelMaterialReading materials;
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

// The same request for the model a ViewModel ENTITY names as its own, which is
// `models/weapons/c_models/c_<class>_arms.mdl` on every shipped first-person
// composite. It differs from buildViewModelRequest in exactly one rule: the file
// name is not checked for the `v_` prefix.
//
// That check is dropped deliberately, and the reason is which side is the
// authority. `--viewmodel <path>` is a person naming a model, so the name is
// where the meaning comes from and a `c_*` file there is a mistake worth
// refusing. Here the path came from the entity's own m_nModelIndex, resolved
// through the demo's model_precache -- the name is evidence, not an assertion.
// Refusing a `c_*` name here would refuse the very model the next round has to
// draw (measured 2026-10-12: every candidate ViewModel entity on the POV
// recording resolves to `c_<class>_arms.mdl`, and no `v_*` name appears at all).
//
// Everything else is the same code path on purpose: the companion has to be
// inspected, counted, alignment-selected and mesh-built by the same parser the
// `--viewmodel` path uses, or the two would disagree about the same file.
ViewModelRequest buildViewModelCompanionRequest(const AssetRoot& root, const std::string& path,
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
// Point 3 is why the first round was a refusal contract and not a mapping.
// Applying the entity's index to the drawn model's table is wrong even when the
// value lands inside it: `inRange` measured against the wrong table is a
// coincidence, and the probe reports the two counts side by side
// (`vmmapping-summary ... inRange=37 outOfRange=104`) so the coincidence is
// visible instead of flattering.
//
// THE MAPPING ROUND FOUND THAT NO MAPPING IS NEEDED, AND THAT IS WHY THIS ROUND
// REPLACES THE RULE ABOVE. Follow-up work was expected to build
// entity-sequence -> activity -> `v_*`-sequence. It cannot be built, and the
// investigation (`T2.5-MAPPING-RECON-2026-10-12.md`) says why in five layers:
// `v_*.mdl` is not carried by the demo, not named by `items_game.txt`, and the
// weapon scripts that would name it are encrypted in every shipped build; the
// activity labels that could bridge the two index spaces exist only on the `v_*`
// side (`mstudioseqdesc_t + 8`) and on none of the `c_*_arms` models. The
// premise was wrong, not the work: modern TF2's first-person view is a COMPOSITE
// -- a static weapon body plus a separately animated pair of arms -- and the
// entity's `m_nSequence` is an index into the arms model's own table all along.
// Measured per entity against its own model: 1497 of 1498 observations land in
// range, against 104 of 141 out of range when the `v_*` table is used instead.
//
// So the correct animation path is not a translation, it is a proof:
//
//   ViewModel owner -> weapon entity -> entity m_nModelIndex -> that model
//     -> that model's own sequence table -> bone matrices
//
// and the pass must draw THE MODEL THE ENTITY NAMED. A sequence is accepted only
// when the model being drawn is the entity's own model, so "which table" is
// settled before "where in the table" is asked. The four refusals below are the
// four ways that proof can fail, and they are separate because they are separate
// repairs: a missing entity, a different model, an index past the right table,
// and a skeleton the pose does not cover.
enum class ViewModelAnimationStatus {
  Missing,             // no owning ViewModel entity, no model index, or no sequence
  Ok,                  // the entity's own model is drawn and the sequence indexes it
  ModelMismatch,       // the drawn model is not the model the entity's own index names
  SequenceOutOfRange,  // it is past the end of that model's own table
  BoneMismatch,        // the drawn model's animation bones do not line up
};

const char* viewModelAnimationStatusName(ViewModelAnimationStatus status);

// What the owning ViewModel entity said, read from the demo's own snapshot.
// `entityPresent` and `hasSequence` are separate because they are separate
// failures: "the demo has no ViewModel entity for this player" and "it has one
// and the slot is absent" are different reasons to draw a bind pose, and a
// single flag would make them read the same.
//
// THE PROOF FIELDS ARE VALUES, NOT VERDICTS. `hasOwner`/`ownerEntity` and
// `hasModelIndex`/`modelIndex` carry what the entity actually said, and the
// contract derives the verdicts from them. Carrying a pre-computed
// `ownerMatchesViewEntity` bool instead would make the negative fixture for a
// wrong owner impossible to construct: a caller could set the flag true while
// naming a different entity, and the contract would have nothing to compare.
struct ViewModelAnimationInput {
  bool entityPresent = false;
  int entityIndex = -1;
  std::string entityModelPath;  // what the entity's m_nModelIndex names
  // The owner proof. `viewEntity` is the entity svc_SetView named; `ownerEntity`
  // is the low 11 bits of this entity's m_hOwner handle. They must be equal, and
  // the contract checks it rather than assuming it, because the caller above it
  // (findViewModelAnimationInput) already *selects* on that equality -- and a
  // selection rule that is never re-asserted is a rule no fixture can break.
  bool hasOwner = false;
  int ownerEntity = -1;
  int viewEntity = -1;
  // The model proof. Zero is not a model here: it is the value an entity carries
  // when nothing was set, and it is separately reportable from "the index did not
  // resolve" because only one of the two is repairable by a different precache.
  bool hasModelIndex = false;
  int modelIndex = -1;
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
  // The proof's first conclusion, reported on its own so a reader does not have
  // to re-derive it from two paths that may have been normalised differently.
  bool entityModelMatchesDrawn = false;
  // The second conclusion, kept apart from `status` because the two answer
  // different questions and the recording used to check this one separates them:
  // an index can sit INSIDE the entity's own table and still name no pose, which
  // is `sequenceInRange` true with `status` not Ok. Folded into one reading, "the
  // index is wrong" and "the model has no pose there" would look the same, and
  // the first is a defect while the second is the model's own content.
  bool sequenceInRange = false;
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
// nullptr when that model has none; `drawn` supplies that model's own path, its
// own sequence table and its own bone count -- so "in range" and "covers the
// skeleton" are measured against the model that is on screen and against nothing
// else. `timing` carries the tick the frame is being drawn for, and its sequence
// / cycle / rate fields are overwritten from the input -- so a caller cannot
// sample a sequence the input did not name.
//
// THE CHECKS RUN IN ONE ORDER, AND THE ORDER IS PART OF THE CONTRACT:
//
//   1. the drawn model resolved at all            -> Missing
//   2. an owning ViewModel entity was found       -> Missing
//   3. that entity's owner is the view entity     -> Missing
//   4. its m_nModelIndex is non-zero and resolved -> Missing
//   5. the entity carries a sequence              -> Missing
//   6. the drawn model IS the entity's own model  -> ModelMismatch
//   7. the sequence is inside that model's table  -> SequenceOutOfRange
//   8. a pose can be sampled from that table      -> Missing / BoneMismatch
//   9. the pose covers the drawn skeleton         -> BoneMismatch
//
// Step 6 comes before step 7 because a range measured against the wrong table is
// not a shorter answer, it is a different question. The first round reported the
// POV recording's sequence 29 as `sequenceOutOfRange` -- 29 is past the `v_*`
// table's seven entries -- and that label was an artifact of comparing against a
// table nobody was drawing from. With the proof in place the same frame reads
// `modelMismatch`, which names the actual defect.
ViewModelAnimationOutcome resolveViewModelAnimationInput(
  const ViewModelAnimationInput& input,
  const ViewModelRequest& drawn,
  const AnimationBinding* drawnBinding,
  const AnimationSampleRequest& timing);

} // namespace tf2::native
