// One definition point for the numeric identifiers of Valve's file formats.
//
// Each value below is the number public/studio.h and public/bspflags.h assign to
// the identifier named in its comment, so a reader can re-check the reference
// tree instead of trusting this file. Nothing here describes behaviour: these
// are only the bits the readers test.
//
// Scope, deliberately:
//   * flags and the one sentinel value (G-1). Structure offsets and strides stay
//     in their readers for now; the animation ones are being rewritten by A1, so
//     they move here after A1 merges, never before.
//   * a value is never edited to make a test pass. A change here means the
//     format changed, and every gate that reads it has to be re-derived.
//
// Reference lines in the mirror: studio.h:373-404 (bone flags), :627-632 (per
// channel flags), :2972-2986 (sequence flags), :2992-3002 (autolayer flags);
// bspflags.h:1-16 (SURF_*).
//
// Where these values are defined again today. Each swaps to the names below in
// one behaviour-preserving refactor; the animation ones wait for A1:
//   native/src/animation_decoder.cpp:27-34    8 definitions, 13 references at
//                                             :298-:847 (chain end, looping,
//                                             the six per-channel flags)
//   native/include/animation_decoder.h:46,48  kStudioDeltaFlag, kStudioPostFlag
//   native/src/animation_binding.cpp:1144,1219,1278,1281   via those two aliases
//   native/tools/model_pose_probe.cpp:170     the 0xFF chain-end sentinel, as 255
//   native/src/bsp_map.cpp:1006,1120-1122     the SURF tests (four sites)
//   root_diagnosis.cpp:37-48                  third copy, in the delivery package
//                                             handoff/v6/rootdiag-v6/; swaps when
//                                             rootdiag lands in this repo
//   tools/bsp_face.py:34-37                   the Python oracle's own SURF_*
//                                             copies: deliberately NOT switched,
//                                             so the two halves do not share one
//                                             mistake

#pragma once

#include <cstdint>

namespace tf2::native::source {

// ---------------------------------------------------------------------------
// studio.h:2972-2986 -- mstudioseqdesc_t::flags, shared by mstudioanimdesc_t.
// Unreferenced members are kept so a later reader does not re-introduce a local
// copy for them.
// ---------------------------------------------------------------------------
inline constexpr std::int32_t kStudioLooping    = 0x0001;  // STUDIO_LOOPING
inline constexpr std::int32_t kStudioSnap       = 0x0002;  // STUDIO_SNAP
inline constexpr std::int32_t kStudioDelta      = 0x0004;  // STUDIO_DELTA
inline constexpr std::int32_t kStudioAutoplay   = 0x0008;  // STUDIO_AUTOPLAY
inline constexpr std::int32_t kStudioPost       = 0x0010;  // STUDIO_POST
inline constexpr std::int32_t kStudioAllZeros   = 0x0020;  // STUDIO_ALLZEROS
inline constexpr std::int32_t kStudioCyclePose  = 0x0080;  // STUDIO_CYCLEPOSE
inline constexpr std::int32_t kStudioRealtime   = 0x0100;  // STUDIO_REALTIME
inline constexpr std::int32_t kStudioLocal      = 0x0200;  // STUDIO_LOCAL
inline constexpr std::int32_t kStudioHidden     = 0x0400;  // STUDIO_HIDDEN
inline constexpr std::int32_t kStudioOverride   = 0x0800;  // STUDIO_OVERRIDE
inline constexpr std::int32_t kStudioActivity   = 0x1000;  // STUDIO_ACTIVITY
inline constexpr std::int32_t kStudioEvent      = 0x2000;  // STUDIO_EVENT
inline constexpr std::int32_t kStudioWorld      = 0x4000;  // STUDIO_WORLD

// ---------------------------------------------------------------------------
// studio.h:2992-3002 -- mstudioautolayer_t::flags. Unused today: the autolayer
// gap in native/docs/design/source-anim-map.md §4 is where these get used.
// ---------------------------------------------------------------------------
inline constexpr std::int32_t kStudioAlPost     = 0x0010;  // STUDIO_AL_POST
inline constexpr std::int32_t kStudioAlSpline   = 0x0040;  // STUDIO_AL_SPLINE
inline constexpr std::int32_t kStudioAlXfade    = 0x0080;  // STUDIO_AL_XFADE
inline constexpr std::int32_t kStudioAlNoBlend  = 0x0200;  // STUDIO_AL_NOBLEND
inline constexpr std::int32_t kStudioAlLocal    = 0x1000;  // STUDIO_AL_LOCAL
inline constexpr std::int32_t kStudioAlPose     = 0x4000;  // STUDIO_AL_POSE

// ---------------------------------------------------------------------------
// studio.h:627-632 -- the 8-bit channel flags of mstudioanim_t::flags. These are
// a different flag space from the sequence flags above, not a subset.
// ---------------------------------------------------------------------------
inline constexpr std::uint8_t kStudioAnimRawPos   = 0x01;  // STUDIO_ANIM_RAWPOS
inline constexpr std::uint8_t kStudioAnimRawRot   = 0x02;  // STUDIO_ANIM_RAWROT
inline constexpr std::uint8_t kStudioAnimAnimPos  = 0x04;  // STUDIO_ANIM_ANIMPOS
inline constexpr std::uint8_t kStudioAnimAnimRot  = 0x08;  // STUDIO_ANIM_ANIMROT
inline constexpr std::uint8_t kStudioAnimDelta    = 0x10;  // STUDIO_ANIM_DELTA
inline constexpr std::uint8_t kStudioAnimRawRot2  = 0x20;  // STUDIO_ANIM_RAWROT2

// ---------------------------------------------------------------------------
// bspflags.h:1-16 -- dtexinfo_t::flags (SURF_*), one bit each in the low 16.
// ---------------------------------------------------------------------------
inline constexpr std::int32_t kSurfLight      = 0x0001;  // SURF_LIGHT
inline constexpr std::int32_t kSurfSky2D      = 0x0002;  // SURF_SKY2D
inline constexpr std::int32_t kSurfSky        = 0x0004;  // SURF_SKY
inline constexpr std::int32_t kSurfWarp       = 0x0008;  // SURF_WARP
inline constexpr std::int32_t kSurfTrans      = 0x0010;  // SURF_TRANS
inline constexpr std::int32_t kSurfNoPortal   = 0x0020;  // SURF_NOPORTAL
inline constexpr std::int32_t kSurfTrigger    = 0x0040;  // SURF_TRIGGER
inline constexpr std::int32_t kSurfNoDraw     = 0x0080;  // SURF_NODRAW
inline constexpr std::int32_t kSurfHint       = 0x0100;  // SURF_HINT
inline constexpr std::int32_t kSurfSkip       = 0x0200;  // SURF_SKIP
inline constexpr std::int32_t kSurfNoLight    = 0x0400;  // SURF_NOLIGHT
inline constexpr std::int32_t kSurfBumpLight  = 0x0800;  // SURF_BUMPLIGHT
inline constexpr std::int32_t kSurfNoShadows  = 0x1000;  // SURF_NOSHADOWS
inline constexpr std::int32_t kSurfNoDecals   = 0x2000;  // SURF_NODECALS
inline constexpr std::int32_t kSurfNoChop     = 0x4000;  // SURF_NOCHOP
inline constexpr std::int32_t kSurfHitbox     = 0x8000;  // SURF_HITBOX

// ---------------------------------------------------------------------------
// studio.h:373-404 -- mstudiobone_t::flags. Only kStudioFixedAlignment is
// referenced today (the root diagnosis probe); the families are defined whole so
// a boneMask implementation does not invent its own numbers.
// ---------------------------------------------------------------------------
inline constexpr std::int32_t kBoneCalculateMask       = 0x0000001F;  // BONE_CALCULATE_MASK
inline constexpr std::int32_t kBonePhysicallySimulated = 0x00000001;  // BONE_PHYSICALLY_SIMULATED
inline constexpr std::int32_t kBonePhysicsProcedural   = 0x00000002;  // BONE_PHYSICS_PROCEDURAL
inline constexpr std::int32_t kBoneAlwaysProcedural    = 0x00000004;  // BONE_ALWAYS_PROCEDURAL
inline constexpr std::int32_t kBoneScreenAlignSphere   = 0x00000008;  // BONE_SCREEN_ALIGN_SPHERE
inline constexpr std::int32_t kBoneScreenAlignCylinder = 0x00000010;  // BONE_SCREEN_ALIGN_CYLINDER
inline constexpr std::int32_t kBoneUsedMask            = 0x0007FF00;  // BONE_USED_MASK
inline constexpr std::int32_t kBoneUsedByAnything      = 0x0007FF00;  // BONE_USED_BY_ANYTHING
inline constexpr std::int32_t kBoneUsedByHitbox        = 0x00000100;  // BONE_USED_BY_HITBOX
inline constexpr std::int32_t kBoneUsedByAttachment    = 0x00000200;  // BONE_USED_BY_ATTACHMENT
inline constexpr std::int32_t kBoneUsedByVertexMask    = 0x0003FC00;  // BONE_USED_BY_VERTEX_MASK
inline constexpr std::int32_t kBoneUsedByVertexLod0    = 0x00000400;  // BONE_USED_BY_VERTEX_LOD0
inline constexpr std::int32_t kBoneUsedByVertexLod1    = 0x00000800;
inline constexpr std::int32_t kBoneUsedByVertexLod2    = 0x00001000;
inline constexpr std::int32_t kBoneUsedByVertexLod3    = 0x00002000;
inline constexpr std::int32_t kBoneUsedByVertexLod4    = 0x00004000;
inline constexpr std::int32_t kBoneUsedByVertexLod5    = 0x00008000;
inline constexpr std::int32_t kBoneUsedByVertexLod6    = 0x00010000;
inline constexpr std::int32_t kBoneUsedByVertexLod7    = 0x00020000;
inline constexpr std::int32_t kBoneUsedByBoneMerge     = 0x00040000;  // BONE_USED_BY_BONE_MERGE
inline constexpr std::int32_t kBoneTypeMask            = 0x00F00000;  // BONE_TYPE_MASK
inline constexpr std::int32_t kBoneFixedAlignment      = 0x00100000;  // BONE_FIXED_ALIGNMENT
inline constexpr std::int32_t kBoneHasSaveFramePos     = 0x00200000;  // BONE_HAS_SAVEFRAME_POS
inline constexpr std::int32_t kBoneHasSaveFrameRot     = 0x00400000;  // BONE_HAS_SAVEFRAME_ROT

// ---------------------------------------------------------------------------
// The bone-chain sentinel. A chain record whose bone byte is 0xFF ends the
// chain; it is not bone 255 (public/bone_setup.cpp walks the chain and stops
// there; TF2's reload clips ship an empty one, the T62 finding). An empty chain
// means "no animated bone in this clip", so the sample falls back to the pose
// the animdesc starts from -- not a decode failure.
// ---------------------------------------------------------------------------
inline constexpr std::uint8_t kStudioAnimChainEnd = 0xFF;

namespace detail {
constexpr bool isSingleBit(std::int32_t value) { return value > 0 && (value & (value - 1)) == 0; }
}  // namespace detail

// SURF_* is exactly the low 16 bits, one bit each (bspflags.h:1-16).
static_assert(detail::isSingleBit(kSurfLight) && detail::isSingleBit(kSurfSky2D)
    && detail::isSingleBit(kSurfSky) && detail::isSingleBit(kSurfWarp)
    && detail::isSingleBit(kSurfTrans) && detail::isSingleBit(kSurfNoPortal)
    && detail::isSingleBit(kSurfTrigger) && detail::isSingleBit(kSurfNoDraw)
    && detail::isSingleBit(kSurfHint) && detail::isSingleBit(kSurfSkip)
    && detail::isSingleBit(kSurfNoLight) && detail::isSingleBit(kSurfBumpLight)
    && detail::isSingleBit(kSurfNoShadows) && detail::isSingleBit(kSurfNoDecals)
    && detail::isSingleBit(kSurfNoChop) && detail::isSingleBit(kSurfHitbox),
    "every SURF_* must be a single bit");
static_assert((kSurfLight | kSurfSky2D | kSurfSky | kSurfWarp | kSurfTrans
    | kSurfNoPortal | kSurfTrigger | kSurfNoDraw | kSurfHint | kSurfSkip
    | kSurfNoLight | kSurfBumpLight | kSurfNoShadows | kSurfNoDecals
    | kSurfNoChop | kSurfHitbox) == 0xFFFF,
    "SURF_* must cover the low 16 bits");

// STUDIO_ANIM_* is exactly the low 6 bits of the channel byte (studio.h:627-632).
static_assert((kStudioAnimRawPos | kStudioAnimRawRot | kStudioAnimAnimPos
    | kStudioAnimAnimRot | kStudioAnimDelta | kStudioAnimRawRot2) == 0x3F,
    "STUDIO_ANIM_* must cover the low 6 bits");

// Sequence flag and per-channel flag are different bits, and confusing them is a
// defect this tree has had (audit M-2). Keep the two families from being
// "tidied" into one.
static_assert(kStudioDelta == 0x0004 && kStudioAnimDelta == 0x0010,
    "STUDIO_DELTA (0x4) and STUDIO_ANIM_DELTA (0x10) are separate flags");

// Bone flag families are partitions of the word, not free bits (studio.h:373-404).
static_assert((kBonePhysicallySimulated | kBonePhysicsProcedural | kBoneAlwaysProcedural
    | kBoneScreenAlignSphere | kBoneScreenAlignCylinder) == kBoneCalculateMask,
    "the BONE_CALCULATE_* bits are the low 5 bits");
static_assert((kBoneUsedByHitbox | kBoneUsedByAttachment | kBoneUsedByVertexMask
    | kBoneUsedByBoneMerge) == kBoneUsedMask,
    "the BONE_USED_BY_* bits must compose BONE_USED_MASK");
static_assert(kBoneUsedByVertexLod0 << 7 == kBoneUsedByVertexLod7,
    "the eight BONE_USED_BY_VERTEX_LOD* bits are consecutive");
static_assert((kBoneFixedAlignment & kBoneTypeMask) == kBoneFixedAlignment
    && (kBoneFixedAlignment & kBoneUsedMask) == 0,
    "BONE_FIXED_ALIGNMENT is a type bit, not a usage bit");

// The SURF bits bsp_map's not-drawn test and its sky/water split depend on
// (native/src/bsp_map.cpp:1120-1122). Pinned because that behaviour was verified
// against these numbers, not derived from them.
static_assert(kSurfTrigger == 0x0040 && kSurfNoDraw == 0x0080,
    "bsp_map's not-drawn test needs SURF_TRIGGER|SURF_NODRAW at 0x0040|0x0080");
static_assert(kSurfSky2D == 0x0002 && kSurfSky == 0x0004 && kSurfWarp == 0x0008,
    "bsp_map's sky and water tests need SURF_SKY2D|SURF_SKY|SURF_WARP");

}  // namespace tf2::native::source
