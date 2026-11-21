#pragma once

// G1 Phase B Round 1 (B1): table-driven CLI dispatch skeleton for C2.
// Migrates parseFeatureArgument (main.cpp:3665-3864, 31 flags) and
// claimValueLessSwitch (main.cpp:312-331, 3 flags) with zero behavior change.
// Header-only in B1 to keep the patch within 2 files and avoid CMake churn.

#include "animation_binding.h"
#include "native_renderer.h"
#include "settings_store.h"

#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include <windows.h>

namespace tf2::native::main_internal {

enum class FlagArity : std::uint8_t {
  Zero,
  One,
};

struct CliParseEnv {
  PersistentSettings& persistent;
  bool& logsOpenAtStartup;
  std::string& logsUrl;
  std::string& logsUrl2;
  int& logsWidth;
  std::string& logsUserData;
  std::string& logsScript;
  int& logsPumpMs;
  bool& logsMeasureFrames;
  bool& logsArgumentRejected;
  double& playbackSpeed;
  bool& playbackSpeedRejected;
  bool& outlineRequested;
  bool& outlineGiven;
  OverlayDepthMode& outlineDepthMode;
  bool& outlineDepthGiven;
  bool& outlineArgumentRejected;
  bool& outlineVertexExtent;
  std::filesystem::path& cameraScriptPath;
  bool& cameraScriptRejected;
  std::filesystem::path& hudRoot;
  std::string& hudLayout;
  int& hudScale;
  bool& hudScaleGiven;
  bool& hudConfigRequested;
  bool& hudArgumentRejected;
  bool& hudOnGiven;
  bool& hudOn;
  bool& hudOffCli;
  bool& hudLiveCli;
  bool& hudMinimapCli;
  bool& hudResetRequested;
  bool& hudApplyRequested;
  bool& hudTextTest;
  bool& entityTintMutation;
  bool& worldTileFillTrace;
  bool& entityBindPoseMutation;
  bool& playerAnimationEnabled;
  AnimationBlendMode& animationBlendMode;
  bool& animationBlendArgumentRejected;
  std::string (*narrowAscii)(const wchar_t*);
};

using FlagHandler = bool (*)(CliParseEnv& env, LPWSTR* arguments, int argumentCount, int& i);

struct FlagSpec {
  const wchar_t* name;
  FlagArity arity;
  FlagHandler handler;
};

// Table for parseFeatureArgument (31 unique flags, preserving A-class vs B-class
// missing-argument semantics and verbatim stderr strings).
inline const FlagSpec kFeatureFlagTable[] = {
  {L"--projectile-trajectory", FlagArity::Zero, [](CliParseEnv& env, LPWSTR*, int, int&) {
    // T53: off unless this flag or the settings file says otherwise.
    env.persistent.render.projectileTrajectory = true;
    return true;
  }},
  {L"--trajectory-player", FlagArity::One, [](CliParseEnv& env, LPWSTR* arguments, int argumentCount, int& i) {
    if (i + 1 >= argumentCount) return false;
    // T53: "all" is -1; a number is an entity index. A gate runs both, so the
    // difference between the two filters is a captured pair of frames.
    const std::wstring value = arguments[++i];
    env.persistent.render.selectedPlayerEntity = value == L"all"
        ? -1 : static_cast<std::int32_t>(std::wcstol(value.c_str(), nullptr, 10));
    return true;
  }},
  {L"--trajectory-linger-ms", FlagArity::One, [](CliParseEnv& env, LPWSTR* arguments, int argumentCount, int& i) {
    if (i + 1 >= argumentCount) return false;
    env.persistent.render.trajectoryLingerMs = static_cast<int>(std::wcstol(arguments[++i], nullptr, 10));
    return true;
  }},
  {L"--trajectory-quality", FlagArity::One, [](CliParseEnv& env, LPWSTR* arguments, int argumentCount, int& i) {
    if (i + 1 >= argumentCount) return false;
    const std::wstring value = arguments[++i];
    env.persistent.render.trajectoryQuality = value == L"low"
        ? RenderSettings::TrajectoryQuality::Low : RenderSettings::TrajectoryQuality::High;
    return true;
  }},
  {L"--logstf-open", FlagArity::Zero, [](CliParseEnv& env, LPWSTR*, int, int&) {
    env.logsOpenAtStartup = true;
    return true;
  }},
  {L"--logstf-url", FlagArity::One, [](CliParseEnv& env, LPWSTR* arguments, int argumentCount, int& i) {
    if (i + 1 >= argumentCount) return false;
    env.logsUrl = env.narrowAscii(arguments[++i]);
    env.logsOpenAtStartup = true;
    return true;
  }},
  {L"--logstf-url2", FlagArity::One, [](CliParseEnv& env, LPWSTR* arguments, int argumentCount, int& i) {
    if (i + 1 >= argumentCount) return false;
    env.logsUrl2 = env.narrowAscii(arguments[++i]);
    return true;
  }},
  {L"--logstf-width", FlagArity::One, [](CliParseEnv& env, LPWSTR* arguments, int argumentCount, int& i) {
    if (i + 1 >= argumentCount) return false;
    wchar_t* end = nullptr;
    const long parsed = std::wcstol(arguments[++i], &end, 10);
    if (end && *end == L'\0' && parsed > 0) env.logsWidth = static_cast<int>(parsed);
    else env.logsArgumentRejected = true;
    return true;
  }},
  {L"--logstf-user-data", FlagArity::One, [](CliParseEnv& env, LPWSTR* arguments, int argumentCount, int& i) {
    if (i + 1 >= argumentCount) return false;
    env.logsUserData = env.narrowAscii(arguments[++i]);
    return true;
  }},
  {L"--logstf-script", FlagArity::One, [](CliParseEnv& env, LPWSTR* arguments, int argumentCount, int& i) {
    if (i + 1 >= argumentCount) return false;
    env.logsScript = env.narrowAscii(arguments[++i]);
    env.logsOpenAtStartup = true;
    return true;
  }},
  {L"--logstf-pump-ms", FlagArity::One, [](CliParseEnv& env, LPWSTR* arguments, int argumentCount, int& i) {
    if (i + 1 >= argumentCount) return false;
    wchar_t* end = nullptr;
    const long parsed = std::wcstol(arguments[++i], &end, 10);
    if (end && *end == L'\0' && parsed > 0) env.logsPumpMs = static_cast<int>(parsed);
    else env.logsArgumentRejected = true;
    return true;
  }},
  {L"--logstf-measure", FlagArity::Zero, [](CliParseEnv& env, LPWSTR*, int, int&) {
    env.logsMeasureFrames = true;
    return true;
  }},
  {L"--playback-speed", FlagArity::One, [](CliParseEnv& env, LPWSTR* arguments, int argumentCount, int& i) {
    if (i + 1 >= argumentCount) {
      env.playbackSpeedRejected = true;
      return true;
    }
    wchar_t* end = nullptr;
    const double parsed = std::wcstod(arguments[++i], &end);
    if (end && *end == L'\0' && parsed > 0.0) env.playbackSpeed = parsed;
    else env.playbackSpeedRejected = true;
    return true;
  }},
  {L"--animation-blend", FlagArity::One, [](CliParseEnv& env, LPWSTR* arguments, int argumentCount, int& i) {
    // T105-n17 Step 2/3: which sampler the player body uses. Refused rather
    // than defaulted on a typo, so "the gate asked for posegrid" and "the gate
    // asked for nothing" cannot read the same in the trace.
    if (i + 1 >= argumentCount) {
      env.animationBlendArgumentRejected = true;
      return true;
    }
    const std::wstring value = arguments[++i];
    if (value == L"posegrid") {
      env.animationBlendMode = AnimationBlendMode::PoseGrid;
      // The grid is filled by the driven clock, so asking for the sampler
      // without that path would silently sample nothing.
      env.playerAnimationEnabled = true;
    } else if (value == L"legacy") {
      env.animationBlendMode = AnimationBlendMode::Legacy;
    } else {
      env.animationBlendArgumentRejected = true;
    }
    return true;
  }},
  {L"--outline", FlagArity::One, [](CliParseEnv& env, LPWSTR* arguments, int argumentCount, int& i) {
    if (i + 1 >= argumentCount) {
      env.outlineArgumentRejected = true;
      return true;
    }
    const std::wstring value = arguments[++i];
    if (value == L"on") {
      env.outlineRequested = true;
      env.outlineGiven = true;
    } else if (value == L"off") {
      env.outlineRequested = false;
      env.outlineGiven = true;
    } else {
      env.outlineArgumentRejected = true;
    }
    return true;
  }},
  {L"--outline-depth", FlagArity::One, [](CliParseEnv& env, LPWSTR* arguments, int argumentCount, int& i) {
    if (i + 1 >= argumentCount) {
      env.outlineArgumentRejected = true;
      return true;
    }
    const std::wstring value = arguments[++i];
    if (value == L"xray") {
      env.outlineDepthMode = OverlayDepthMode::XRay;
      env.outlineDepthGiven = true;
    } else if (value == L"tested") {
      env.outlineDepthMode = OverlayDepthMode::DepthTested;
      env.outlineDepthGiven = true;
    } else {
      env.outlineArgumentRejected = true;
    }
    return true;
  }},
  {L"--outline-vertex-extent", FlagArity::Zero, [](CliParseEnv& env, LPWSTR*, int, int&) {
    // T52 (2026-10-23): publish each silhouette's region from the vertices the
    // draw consumes instead of from the entity AABB. Off by default: it
    // transforms every vertex of every silhouette every frame.
    env.outlineVertexExtent = true;
    return true;
  }},
  {L"--camera-script", FlagArity::One, [](CliParseEnv& env, LPWSTR* arguments, int argumentCount, int& i) {
    if (i + 1 >= argumentCount) {
      env.cameraScriptRejected = true;
      return true;
    }
    // T50: the scripted acceptance driver (see runCameraScriptStep). A file of
    // steps, each reported as `camera: accept`; the run quits when the script
    // says so, with the exit code carrying the failure count.
    env.cameraScriptPath = arguments[++i];
    return true;
  }},
  {L"--hud-root", FlagArity::One, [](CliParseEnv& env, LPWSTR* arguments, int argumentCount, int& i) {
    if (i + 1 >= argumentCount) {
      env.hudArgumentRejected = true;
      return true;
    }
    env.hudRoot = arguments[++i];
    if (env.hudRoot.empty()) env.hudArgumentRejected = true;
    return true;
  }},
  {L"--hud-layout", FlagArity::One, [](CliParseEnv& env, LPWSTR* arguments, int argumentCount, int& i) {
    if (i + 1 >= argumentCount) {
      env.hudArgumentRejected = true;
      return true;
    }
    // The document path is stored narrow because that is what HudSettings and
    // the [hud] settings file carry. A wide value that does not round-trip to
    // UTF-8 is refused rather than transliterated.
    if (!tf2::native::wideToUtf8(arguments[++i], env.hudLayout) || env.hudLayout.empty()) {
      env.hudArgumentRejected = true;
    }
    return true;
  }},
  {L"--hud-scale", FlagArity::One, [](CliParseEnv& env, LPWSTR* arguments, int argumentCount, int& i) {
    if (i + 1 >= argumentCount) {
      env.hudArgumentRejected = true;
      return true;
    }
    wchar_t* end = nullptr;
    const long parsed = std::wcstol(arguments[++i], &end, 10);
    if (end && *end == L'\0' && parsed >= 50 && parsed <= 200) {
      env.hudScale = static_cast<int>(parsed);
      env.hudScaleGiven = true;
      env.hudConfigRequested = true;
    } else {
      env.hudArgumentRejected = true;
    }
    return true;
  }},
  {L"--hud-on", FlagArity::Zero, [](CliParseEnv& env, LPWSTR*, int, int&) {
    env.hudOnGiven = true;
    env.hudOn = true;
    env.hudConfigRequested = true;
    return true;
  }},
  {L"--hud-off", FlagArity::Zero, [](CliParseEnv& env, LPWSTR*, int, int&) {
    env.hudOnGiven = true;
    env.hudOn = false;
    env.hudConfigRequested = true;
    env.hudOffCli = true;
    return true;
  }},
  {L"--hud-live", FlagArity::Zero, [](CliParseEnv& env, LPWSTR*, int, int&) {
    // T97: draw the live readout on an evidence frame too. The capture path
    // suppresses it by default so that the pixel gates keep measuring what they
    // were written to measure; this flag is how a gate asks for it.
    env.hudLiveCli = true;
    return true;
  }},
  {L"--overview-open", FlagArity::Zero, [](CliParseEnv&, LPWSTR*, int, int&) {
    // E11: the map-overview screen, on from launch. The hotkey is the
    // interactive door; this one is the door every gate uses, because a gate
    // must not depend on a WM_KEYDOWN reaching the window it captured.
    tf2::native::mapOverviewSetOpen(true);
    return true;
  }},
  {L"--overview-root", FlagArity::One, [](CliParseEnv&, LPWSTR* arguments, int argumentCount, int& i) {
    if (i + 1 >= argumentCount) return false;
    // The demos.tf basemap package (images/*.png + boundaries.json). A different
    // machine passes its own root; the panel names the reason when a map or file
    // is missing instead of drawing a guessed rectangle.
    std::string overviewRoot;
    if (!tf2::native::wideToUtf8(arguments[++i], overviewRoot) || overviewRoot.empty()) {
      std::fprintf(stderr, "--overview-root: bad path\n");
      std::fflush(stderr);
      return false;
    }
    tf2::native::basemapSetRoot(overviewRoot);
    return true;
  }},
  {L"--overview-image", FlagArity::One, [](CliParseEnv&, LPWSTR* arguments, int argumentCount, int& i) {
    if (i + 1 >= argumentCount) return false;
    std::string overviewImage;
    if (!tf2::native::wideToUtf8(arguments[++i], overviewImage) || overviewImage.empty()) {
      std::fprintf(stderr, "--overview-image: bad path\n");
      std::fflush(stderr);
      return false;
    }
    tf2::native::mapOverviewSetImageOverride(overviewImage);
    tf2::native::mapOverviewSetOpen(true);
    return true;
  }},
  {L"--overview-hide", FlagArity::One, [](CliParseEnv&, LPWSTR* arguments, int argumentCount, int& i) {
    if (i + 1 >= argumentCount) return false;
    tf2::native::mapOverviewSetHiddenEntity(_wtoi(arguments[++i]));
    return true;
  }},
  {L"--hud-minimap", FlagArity::Zero, [](CliParseEnv& env, LPWSTR*, int, int&) {
    env.hudMinimapCli = true;
    return true;
  }},
  {L"--hud-reset", FlagArity::Zero, [](CliParseEnv& env, LPWSTR*, int, int&) {
    env.hudResetRequested = true;
    env.hudConfigRequested = true;
    return true;
  }},
  {L"--hud-apply", FlagArity::Zero, [](CliParseEnv& env, LPWSTR*, int, int&) {
    env.hudApplyRequested = true;
    return true;
  }},
  {L"--hud-text-test", FlagArity::Zero, [](CliParseEnv& env, LPWSTR*, int, int&) {
    env.hudTextTest = true;
    return true;
  }},
};

// Table for claimValueLessSwitch (3 flags from main.cpp:312-331). Each exists so a
// gate can show its claim going RED, so each has to be reachable from a command
// line without a branch of its own: --entity-tint-mutation is the negative
// control for the team-tint fix, --world-tile-fill asks for the per-material
// tile-fill ledger, and --entity-bind-pose-mutation withholds the bone matrices
// for the wedge the scout draws over its own chest (value-less on purpose -- it
// either withholds them or it does not, and a value would suggest a middle
// setting that would leave the reader unable to say which picture they saw).
inline const FlagSpec kValueLessFlagTable[] = {
  {L"--entity-tint-mutation", FlagArity::Zero, [](CliParseEnv& env, LPWSTR*, int, int&) {
    env.entityTintMutation = true;
    return true;
  }},
  {L"--entity-bind-pose-mutation", FlagArity::Zero, [](CliParseEnv& env, LPWSTR*, int, int&) {
    env.entityBindPoseMutation = true;
    return true;
  }},
  {L"--world-tile-fill", FlagArity::Zero, [](CliParseEnv& env, LPWSTR*, int, int&) {
    env.worldTileFillTrace = true;
    return true;
  }},
  // Task-9: --world-keep-map-winding is parsed by the pre-scan at main.cpp:3621,
  // because the else-if chain is at MSVC's block-nesting limit and cannot grow an
  // "else if" for it. Parsed is not claimed: the T105 refusal at main.cpp:4741
  // rejects any unclaimed "--" switch with exit 13, so the switch that turns the
  // displacement reversal off could not be used at all -- the run that asks for
  // "winding as stored" stopped before it drew. This row claims the name; the
  // pre-scan above still does the parsing, so the handler has nothing left to do.
  {L"--world-keep-map-winding", FlagArity::Zero, [](CliParseEnv&, LPWSTR*, int, int&) {
    return true;
  }},
  // Task-9: the counterpart of the switch above. The displacement reversal that
  // the keep switch turns off is now the default, so restoring the old rule needs
  // its own name; the pre-scan at main.cpp:3622 parses it and this row only claims
  // it for the T105 refusal, exactly like the row above.
  {L"--world-flip-disp-winding", FlagArity::Zero, [](CliParseEnv&, LPWSTR*, int, int&) {
    return true;
  }},
};

inline bool dispatchFeatureSwitch(CliParseEnv& env, LPWSTR* arguments, int argumentCount, int& i) {
  for (const auto& spec : kFeatureFlagTable) {
    if (std::wcscmp(arguments[i], spec.name) == 0) {
      return spec.handler(env, arguments, argumentCount, i);
    }
  }
  return false;
}

// An unclaimed switch is recorded here rather than returned: the call site sits
// at MSVC's block-nesting limit (C1061), so it cannot gain an `if` block of its
// own, and D-23's record-and-continue is the one behaviour it must keep.
inline void dispatchValueLessSwitch(CliParseEnv& env, LPWSTR* arguments, int argumentCount, int& i,
    std::vector<std::wstring>& unrecognized) {
  for (const auto& spec : kValueLessFlagTable) {
    if (std::wcscmp(arguments[i], spec.name) == 0) {
      spec.handler(env, arguments, argumentCount, i);
      return;
    }
  }
  unrecognized.push_back(arguments[i]);
}

}  // namespace tf2::native::main_internal
