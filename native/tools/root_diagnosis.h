#pragma once

// ============================================================================
// ROOT DIAGNOSIS -- why does the animated body lie on the floor / stand on its
// head while the animation-off frame of the same tick stands?
//
// This probe answers with numbers, before anyone picks a root policy. It carries
// its OWN reading of the Source animation semantics (CalcAnimation,
// CalcBoneQuaternion, CalcBonePosition, ExtractAnimValue, mstudioanimdesc_t::
// pAnim, QuaternionScale/SM/MA, AngleQuaternion(RadianEuler)) written from the
// public documentation of the format, and compares the product decoder against
// it. No Valve code is copied; each function names the Source function it
// mirrors so a reviewer can check it line by line against bone_setup.cpp.
//
//   C1 euler-consistency  bone.rot (RadianEuler) converted with the reference
//                         formula must equal bone.quat. A mismatch on the root
//                         or a key bone is an Euler-order error that would bend
//                         every ANIMROT channel.
//   C2 render-bind        The render model's bind locals composed along the
//                         parent chain must equal inverse(poseToBone).
//   C3 anim-vs-render     The animation source (<class>_animations.mdl) and the
//                         render model must agree on the bind, the parents and
//                         the sequence DELTA/POST flags. The product maps bones
//                         by name + parent only and keeps the BASE model's
//                         sequence flags, so these disagreements are invisible
//                         to it. C3b: anim bind x render poseToBone == identity.
//   C4 root channel       Per sequence / grid cell / frame: the root record's
//                         flags, channel values and base; the reference result;
//                         the product result; the root's deviation from bind,
//                         classified (ok / ~90 axis / ~180 sign-or-double).
//   C5 variants           The same sample with the root replaced by candidate
//                         interpretations, each composed into a full skeleton
//                         and measured for uprightness (head > pelvis > feet,
//                         torso direction vs the bind up axis, height ratio).
//                         "The pixels changed" is never part of a verdict.
//   C7 mesh frame         LOD0 vertex bbox and head-minus-foot centroid vs the
//                         bind skeleton's axis: which frame the drawn mesh is in.
//   C8 header hull        studiohdr_t hull_min/max: how far the body extends
//                         along the hull's OWN tall axis, as a share of the
//                         hull's length there (span >= 0.5 = standing). The
//                         hull is model space written by studiomdl and nothing
//                         rotates it, so C8 settles "which pose is right"
//                         without assuming an up axis. eyeposition is printed
//                         only: on TF2 players it is not in the hull's frame.
//   C6 delta / H1         Delta animdescs: are the root and the bones outside
//                         the channel chain identity (Source) or bind (the
//                         suspected double application)? The delta is then
//                         applied to a stand pose in the POST and the pre order
//                         and both results are measured.
//
// No renderer, no device, no demo: only the TF root and model paths.
// ============================================================================

namespace tf2::native::probe {

// model_pose_probe --root-diagnose --tf-root <tf> --model <a.mdl>[,<b.mdl>...]
//   [--sequences auto|all|<index|label>,...]  indices/labels of the ANIMATION
//                                             SOURCE's own sequence table.
//                                             auto = stand_/run_/walk_/ref,
//                                             crouch*, and delta grids.
//   [--frames 0,mid,last|<n>,...]             default 0,mid,last
//   [--upright stand_,run_,walk_]             label prefixes held to the
//                                             strict upright rule
//   [--up z|bind|mesh]                        axis 'upright' is measured along:
//                                             model +Z (default since the
//                                             2026-11-24 ruling), the bind
//                                             skeleton, or the LOD0 mesh axis
//   [--base <label>]                          base pose for delta layers
//                                             (default: first stand_ sequence)
//   [--min-up <0..1>]                         default 0.8
//   [--max-dev <deg>]                         root deviation reported as WARN
//                                             above this, default 15
//   [--quiet]                                 summaries only
//   [--dump-bones]                            every bone, every sample
//   [--require-product]                       exit 1 unless the product
//                                             matches the reference too
// Exit 0 = ROOT-DIAG PASS and PRODUCT-MATCH PASS; 1 = the reference reading is
// not sound (C1/C2/upright), or the product mismatches under --require-product;
// 3 = the reference is sound but the product mismatches (no --require-product);
// 2 = usage / input. A bare run can no longer exit 0 while the product is wrong.
int rootDiagnose(int argc, char** argv);

// model_pose_probe --root-diagnose-selftest [--require-product]
// Math checks plus synthetic MDL fixtures (no game files). The fixtures are
// decoded by the reference AND by the product decoder. Exit 0 only when both
// pass; 1 = reference broken (or product mismatch with --require-product);
// 3 = reference fine, product mismatch.
int rootDiagnoseSelfTest(bool requireProduct);

} // namespace tf2::native::probe
