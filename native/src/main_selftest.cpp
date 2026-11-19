// main_selftest.cpp -- N24 phase 1 (2026-11-19): code motion only.
//
// The renderer resize/render self-test was the only ~190-line unit left in
// main.cpp that was both self-contained (three parameters, no globals, no
// captured locals, no statics) and already outside every gate's text anchors,
// so it is the cheapest place to prove the T76 seam. The function below is
// copied verbatim from main.cpp:2706-2894 as that file stood before this move;
// the only additions are this header comment, the includes and the namespace
// wrapper. See docs/N24-MAIN-SPLIT-PLAN-2026-11-16.md section 7.
//
// The product contract it carries is unchanged: --renderer-resize-selftest
// answers with exit 0 on pass, 14 when Renderer::initialize fails and 15 when
// any check fails, and the trace text still starts with "renderer-selftest:".

#include "main_internal.h"

#include <cstdio>
#include <string>

#include "native_renderer.h"

namespace tf2::native::main_internal {

// ---- F0-c (2026-11-11): the renderer resize/render self-test --------------
//
// What it is for: QA3-01 (a resize that releases the render target without
// unbinding it) and QA3-03 (a failed HUD upload whose counters still report the
// primitives it meant to draw) are both invisible to a run that has no device
// failure and no resize. Neither can be provoked from a command line today, so
// the failure is made reachable instead: the renderer carries a fault plan (see
// setResourceFaultPlan in native_renderer.h) and this self-test drives it.
//
// What it covers: Renderer::initialize, Renderer::resize, Renderer::draw and the
// HUD pass, on the window wWinMain already created. That window is the product's
// own, created with the same class and the same --window-size handling, so the
// client size and the swap chain describe the same surface a product run would.
//
// What it does NOT cover: everything that makes a product run a product run --
// settings persistence, the demo/tf-root load path, the UI controls, audio, the
// camera script. It returns before loadSettings() is even called. A green
// self-test therefore says the renderer survives three resizes and reports a
// failed HUD upload honestly; it says nothing about playback.
//
// Exit codes: 0 pass, 14 initialize failed, 15 at least one check failed.
int runRendererSelftest(tf2::native::Renderer& renderer, HWND window, bool faultSpecIgnored) {
  int checks = 0;
  int failures = 0;
  auto hex = [](HRESULT value) {
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "0x%08lX", static_cast<unsigned long>(value));
    return std::string(buffer);
  };
  // Every check lands in the trace and on stderr: the trace is what a gate
  // greps, stderr is what a human running the exe by hand sees. Neither is
  // allowed to be the only copy.
  auto expect = [&](bool ok, const char* name, const std::string& detail) {
    ++checks;
    if (!ok) ++failures;
    traceFmt("renderer-selftest: %s %s %s", ok ? "ok" : "FAIL", name, detail.c_str());
    std::fprintf(stderr, "renderer-selftest: %s %s %s\n", ok ? "ok" : "FAIL", name,
      detail.c_str());
    std::fflush(stderr);
  };

  traceFmt("renderer-selftest: begin fault-spec-ignored=%d", faultSpecIgnored ? 1 : 0);

  if (!renderer.initialize(window)) {
    traceFmt("renderer-selftest: RESULT=FAIL checks=1 fail=1 exit=14 initialize stage=%s hr=%s",
      renderer.initStage(), hex(renderer.lastError()).c_str());
    std::fprintf(stderr, "renderer-selftest: RESULT=FAIL checks=1 fail=1 exit=14\n");
    std::fflush(stderr);
    return 14;
  }
  expect(renderer.lastError() == S_OK, "initialize.lastError",
    "hr=" + hex(renderer.lastError()) + " stage=" + renderer.initStage());

  RECT initialClient{};
  GetClientRect(window, &initialClient);
  const UINT initialWidth = static_cast<UINT>(initialClient.right);
  const UINT initialHeight = static_cast<UINT>(initialClient.bottom);

  // Three steps, none of them the size the window was created at: Renderer::resize
  // returns early when the size did not change, and an early return would make
  // "the unbind never ran" and "there was nothing to unbind" the same reading.
  UINT sizes[3][2] = { {800u, 600u}, {1280u, 720u}, {640u, 480u} };
  for (int step = 0; step < 3; ++step) {
    if (sizes[step][0] == initialWidth && sizes[step][1] == initialHeight) ++sizes[step][0];
  }

  std::vector<tf2::native::Renderer::HudPrimitive> primitives(3);
  for (std::size_t index = 0; index < primitives.size(); ++index) {
    primitives[index].kind = tf2::native::Renderer::HudPrimitiveKind::Rect;
    primitives[index].x = 8.0f + 40.0f * static_cast<float>(index);
    primitives[index].y = 8.0f;
    primitives[index].w = 32.0f;
    primitives[index].h = 18.0f;
    primitives[index].colour[0] = 1.0f;
    primitives[index].colour[1] = 1.0f;
    primitives[index].colour[2] = 1.0f;
    primitives[index].colour[3] = 1.0f;
    primitives[index].filled = (index % 2u) == 0u;
  }
  renderer.setHudEnabled(true);
  renderer.setHudPrimitives(primitives);

  // ---- phase 1: three resizes, each followed by a full frame --------------
  for (int step = 0; step < 3; ++step) {
    const UINT width = sizes[step][0];
    const UINT height = sizes[step][1];
    const std::size_t unbindBefore = renderer.resizeUnbindCount();
    // Read before the resize: this is the precondition QA3-01 needs -- a view the
    // immediate context is still holding. If it reads 0 the resize below is
    // releasing nothing and no error code could appear, so the reading belongs in
    // the detail of the assertion rather than as an assertion of its own.
    const bool boundBefore = renderer.renderTargetBound();
    renderer.resize(width, height);
    const std::string resizeDetail = "want=" + std::to_string(width) + "x" +
      std::to_string(height) + " hr=" + hex(renderer.lastError()) +
      " unbind=" + std::to_string(renderer.resizeUnbindCount() - unbindBefore) +
      " boundBefore=" + (boundBefore ? "1" : "0");
    // QA3-01 shape: the render target was released while the immediate context
    // still had it bound. A runtime that rejects that reports 0x887A0001 here;
    // one that unbinds internally reports S_OK and the unbind count below is the
    // only reading left. So the health check stays, but the assertion is the count:
    // pinning the error value would fail the day the runtime got kinder (D-104).
    expect(renderer.lastError() == S_OK, "resize.lastError", resizeDetail);
    expect(renderer.resizeUnbindCount() == unbindBefore + 1, "resize.unbind", resizeDetail);

    const bool drew = renderer.draw(0.05f, 0.06f, 0.08f);
    const std::string drawDetail = "step=" + std::to_string(step) + " hr=" +
      hex(renderer.lastError()) + " hudPrimitives=" +
      std::to_string(renderer.hudDrawnPrimitives()) + " hudRects=" +
      std::to_string(renderer.hudDrawnRects());
    expect(drew, "draw.returned", drawDetail);
    expect(renderer.lastError() == S_OK, "draw.lastError", drawDetail);
    expect(renderer.hudDrawnPrimitives() == primitives.size(), "draw.hudPrimitives", drawDetail);
    expect(renderer.hudDrawnRects() == primitives.size(), "draw.hudRects", drawDetail);
  }

  // ---- phase 2: a HUD allocation the seam refuses -------------------------
  // More primitives than the buffer was built for, so the next draw has to
  // create a buffer -- the one call this phase makes fail.
  renderer.setResourceFaultPlan("hud.createbuffer");
  std::vector<tf2::native::Renderer::HudPrimitive> many(300, primitives[0]);
  for (std::size_t index = 0; index < many.size(); ++index) {
    many[index].x = 4.0f + 3.0f * static_cast<float>(index % 100u);
    many[index].y = 4.0f + 3.0f * static_cast<float>(index / 100u);
    many[index].w = 2.0f;
    many[index].h = 2.0f;
  }
  renderer.setHudPrimitives(many);
  const std::size_t refusalsBefore = renderer.resourceRefusals();
  const std::size_t mapRefusalsBefore = renderer.resourceMapRefusals();
  const bool drewArmed = renderer.draw(0.05f, 0.06f, 0.08f);
  const std::string armedDetail = "refused=" +
    std::to_string(renderer.resourceRefusals() - refusalsBefore) + " mapRefused=" +
    std::to_string(renderer.resourceMapRefusals() - mapRefusalsBefore) + " hudPrimitives=" +
    std::to_string(renderer.hudDrawnPrimitives()) + " hudRects=" +
    std::to_string(renderer.hudDrawnRects());
  expect(drewArmed, "fault.draw.returned", armedDetail);
  expect(renderer.resourceRefusals() == refusalsBefore + 1, "fault.refusalRecorded", armedDetail);
  expect(renderer.resourceMapRefusals() == mapRefusalsBefore, "fault.noMapRefusal", armedDetail);
  // The reading that makes QA3-03 checkable: a HUD pass that drew nothing must
  // not report the primitives it handed to a buffer that was never created.
  expect(renderer.hudDrawnPrimitives() == 0, "fault.hudPrimitivesHonest", armedDetail);
  expect(renderer.hudDrawnRects() == 0, "fault.hudRectsHonest", armedDetail);

  // ---- phase 3: disarm, and the HUD has to come back ----------------------
  renderer.setResourceFaultPlan(std::string());
  renderer.setHudPrimitives(primitives);
  const std::size_t refusalsAfter = renderer.resourceRefusals();
  const std::size_t mapRefusalsAfter = renderer.resourceMapRefusals();
  // On a tree where the refusal also took the old buffer, the draw below would
  // call Map on a null resource and take the process down before the summary
  // line prints -- a product run does exactly that. Ask first, name it, and stop.
  const bool bufferReady = renderer.hudVertexBufferReady();
  const std::string recoveryDetail = "ready=" + std::to_string(bufferReady ? 1 : 0) +
    " refused=" + std::to_string(renderer.resourceRefusals() - refusalsAfter) +
    " mapRefused=" + std::to_string(renderer.resourceMapRefusals() - mapRefusalsAfter) +
    " hudPrimitives=" + std::to_string(renderer.hudDrawnPrimitives()) + " hudRects=" +
    std::to_string(renderer.hudDrawnRects());
  expect(bufferReady, "fault.survivedRefusal",
    bufferReady ? recoveryDetail
                : recoveryDetail + " (the refused create took the old buffer; a"
                  " real draw here would map null and fault)");
  const bool drewBack = bufferReady && renderer.draw(0.05f, 0.06f, 0.08f);
  expect(drewBack, "disarm.draw.returned", recoveryDetail);
  expect(renderer.resourceRefusals() == refusalsAfter, "disarm.noFurtherRefusal", recoveryDetail);
  // The buffer the armed phase refused to replace has to still be there. A pass
  // that lost it would fail its next map, and that is the other half of QA3-02:
  // the refusal took the old buffer with it.
  expect(renderer.resourceMapRefusals() == mapRefusalsAfter, "disarm.noFurtherMapRefusal",
    recoveryDetail);
  expect(renderer.hudDrawnPrimitives() == primitives.size(), "disarm.hudPrimitives",
    recoveryDetail);
  expect(renderer.hudDrawnRects() == primitives.size(), "disarm.hudRects", recoveryDetail);

  const int exitCode = failures == 0 ? 0 : 15;
  traceFmt("renderer-selftest: RESULT=%s checks=%d fail=%d unbind=%zu refused=%zu "
    "mapRefused=%zu client=%ux%u exit=%d",
    failures == 0 ? "PASS" : "FAIL", checks, failures, renderer.resizeUnbindCount(),
    renderer.resourceRefusals(), renderer.resourceMapRefusals(), initialWidth, initialHeight,
    exitCode);
  std::fprintf(stderr,
    "renderer-selftest: RESULT=%s checks=%d fail=%d unbind=%zu refused=%zu mapRefused=%zu exit=%d\n",
    failures == 0 ? "PASS" : "FAIL", checks, failures, renderer.resizeUnbindCount(),
    renderer.resourceRefusals(), renderer.resourceMapRefusals(), exitCode);
  std::fflush(stderr);
  renderer.shutdown();
  return exitCode;
}

}  // namespace tf2::native::main_internal
