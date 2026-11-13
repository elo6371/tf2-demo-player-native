// viewmodel_fov_probe -- the ViewModel FOV contract, as readings.
//
// WHY THIS IS NOT JUST clampViewModelFov THREE TIMES
//   The first version of this file asserted `clamp(0)==40`, `clamp(180)==120`
//   and `clamp(90)==90`, and printed `"viewModelRender":"unknown"`. It answered
//   the question nobody was asking (does the clamp clamp) while the plan's three
//   real requirements went untested: the default is 90, a stored legacy 80 must
//   not win over that default, and the renderer's clamp and the ViewModel
//   header's clamp must not drift apart -- they are two copies of the same
//   comparison, and two copies is exactly how a default ends up 80 in one place
//   and 90 in the other.
//
// WHAT IS ASSERTED
//   - the three constants are 90 / 40 / 120, not merely "some numbers"
//   - both clamp functions agree on every probe value (they are separate code)
//   - a value below the low bound, above the high bound, and at each boundary
//   - NaN stays NaN: it must not silently become the low bound
//   - a request with no FOV argument carries the default 90
//   - a request with an explicit FOV carries the clamped value
//   - migrateLegacyViewModelFov(80) == 90, and every other in-range value
//     round-trips, so reading 80 as "unset" does not also swallow a real 41..120
//
// The request-level cases go through buildViewModelRequest itself rather than
// re-implementing its defaulting, so "the default is 90" is a reading from the
// product path and not a restatement of the constant.

#include "viewmodel.h"
// Pulled in for clampViewModelFov, which is a *second* copy of the same
// comparison. The two agreeing is one of the things this probe asserts, so it
// has to see both rather than restate one of them.
#include "native_renderer.h"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <limits>
#include <string>

int main() {
  using namespace tf2::native;

  const bool constants =
    kDefaultViewModelFov == 90.0f && kViewModelFovLow == 40.0f && kViewModelFovHigh == 120.0f
    && kDefaultViewModelFovInt == 90 && kLegacyViewModelFovDefaultInt == 80;

  // The renderer header's clamp and this header's clamp are separate functions
  // with separate comparisons. Every value here has to agree in both.
  const float probes[] = {0.0f, 39.0f, 39.999f, 40.0f, 60.0f, 90.0f, 120.0f, 120.001f, 180.0f};
  bool clampsAgree = true;
  bool boundaries = true;
  for (const float value : probes) {
    const float header = normalizeViewModelFov(value);
    const float renderer = clampViewModelFov(value);
    if (std::fabs(header - renderer) > 1e-6f) clampsAgree = false;
    const float expected = value < 40.0f ? 40.0f : (value > 120.0f ? 120.0f : value);
    if (std::fabs(header - expected) > 1e-6f) boundaries = false;
  }
  // NaN has to survive both, because "no value was supplied" and "the value is
  // 40" must not read the same.
  const float nan = std::numeric_limits<float>::quiet_NaN();
  const bool nanPreserved = std::isnan(normalizeViewModelFov(nan)) && std::isnan(clampViewModelFov(nan));

  // The legacy default is the only value that is rewritten; everything else in
  // range is the user's own choice and must come back unchanged.
  const bool migration = migrateLegacyViewModelFov(80) == 90
    && migrateLegacyViewModelFov(40) == 40
    && migrateLegacyViewModelFov(41) == 41
    && migrateLegacyViewModelFov(60) == 60
    && migrateLegacyViewModelFov(90) == 90
    && migrateLegacyViewModelFov(120) == 120;

  std::error_code ignored;
  const auto directory = std::filesystem::temp_directory_path() / "tf2-viewmodel-fov-probe";
  std::filesystem::remove_all(directory, ignored);
  std::filesystem::create_directories(directory, ignored);
  const auto root = AssetRoot::fromPath(directory);

  // A path that does not resolve still carries the FOV it was built with: the
  // FOV is decided before the model is looked up, so the default can be read
  // without assets. That is what makes this a probe and not an integration test.
  const auto omitted = buildViewModelRequest(root, "v_absent.mdl", ViewModelHand::Right);
  const auto explicitMiddle = buildViewModelRequest(root, "v_absent.mdl", ViewModelHand::Right, 90.0f);
  const auto explicitLow = buildViewModelRequest(root, "v_absent.mdl", ViewModelHand::Right, 12.0f);
  const auto explicitHigh = buildViewModelRequest(root, "v_absent.mdl", ViewModelHand::Right, 400.0f);
  const auto explicitNan = buildViewModelRequest(root, "v_absent.mdl", ViewModelHand::Right, nan);
  std::filesystem::remove_all(directory, ignored);

  const bool defaultIsNinety = std::fabs(omitted.fov - 90.0f) < 1e-6f;
  const bool explicitClamped = std::fabs(explicitMiddle.fov - 90.0f) < 1e-6f
    && std::fabs(explicitLow.fov - 40.0f) < 1e-6f
    && std::fabs(explicitHigh.fov - 120.0f) < 1e-6f
    && std::isnan(explicitNan.fov);

  const bool ok = constants && clampsAgree && boundaries && nanPreserved && migration
    && defaultIsNinety && explicitClamped;

  std::cout << "{\"constants\":"
    << (constants ? "true" : "false")
    << ",\"clampsAgree\":" << (clampsAgree ? "true" : "false")
    << ",\"boundaries\":" << (boundaries ? "true" : "false")
    << ",\"nanPreserved\":" << (nanPreserved ? "true" : "false")
    << ",\"legacyEightyMigrates\":" << (migration ? "true" : "false")
    << ",\"defaultIsNinety\":" << (defaultIsNinety ? "true" : "false")
    << ",\"explicitClamped\":" << (explicitClamped ? "true" : "false")
    << ",\"probeValues\":" << (sizeof(probes) / sizeof(probes[0]))
    << ",\"defaultFov\":" << omitted.fov
    << ",\"fovLow\":" << kViewModelFovLow
    << ",\"fovHigh\":" << kViewModelFovHigh
    << ",\"legacyFovDefault\":" << kLegacyViewModelFovDefaultInt
    << ",\"contractOk\":" << (ok ? "true" : "false") << "}\n";
  return ok ? 0 : 1;
}
