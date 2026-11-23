#pragma once

// T54 -- the Win32 half of the embedded logs.tf viewer.
//
// WHERE THE LINE IS
//   logstf_viewer.h/.cpp own the contract that needs no browser: the URL
//   policy, the panel geometry, the state machine, the runtime probe and the
//   frame-time readings. This file owns the part that needs a window: the child
//   controls (back / forward / reload / address / import / close), the
//   splitter drag, the child window the browser draws into, and the scripted
//   sequence a gate drives through --logstf-* flags.
//
// WHY IT IS A SEPARATE TRANSLATION UNIT
//   main.cpp is 7k lines and every change to it risks every other gate in the
//   tree. The panel needs six call sites there (create, layout, command, mouse,
//   pump, shutdown) plus the flag parsing; everything else lives here.

#include <Windows.h>

#include <cstdint>
#include <string>

namespace tf2::native {

struct LogsPanelOptions {
  bool openAtStartup = false;   // --logstf-open
  std::string url;              // --logstf-url (imported through the policy)
  std::string url2;             // --logstf-url2 (history case only)
  int width = -1;               // --logstf-width (clamped by the planner)
  std::string userData;         // --logstf-user-data (WebView2 profile dir)
  // --logstf-script: one scripted sequence per run, because a gate cannot
  // click. Each name is a fixed list of steps, so the reading a gate asserts is
  // produced by code the product also runs interactively.
  //   empty       open, settle, report (must stay empty: no host, no navigate)
  //   import      open, import --logstf-url, settle, report
  //   history     import url then url2, then back / forward / reload
  //   scroll      import url, then scroll +480 and read the offset, then extent
  //   resize      import url, then width 1 (clamp low) and width 100000 (clamp high)
  //   reopen      import url, close, reopen, settle, report
  //   close       import url, close, report
  std::string script;
  int pumpMilliseconds = 10000;  // --logstf-pump-ms: the settle deadline per step
  bool measureFrames = false;    // --logstf-measure: emit the frame-time window line
  std::string exitReason;        // filled by the script; non-empty means a step failed
};

// Creates the panel's child controls and the (hidden) browser host window.
// Nothing is created for the browser itself: that happens on the first
// navigation, so a run that never opens the panel starts no browser.
void logsPanelAttach(HWND window, HINSTANCE instance, const LogsPanelOptions& options);

// Repositions the panel from the planner's answer. Called from WM_SIZE and
// WM_DPICHANGED next to applyUiLayout.
void logsPanelLayout(HWND window);

// WM_COMMAND forwarding: returns true when the id belonged to the panel.
bool logsPanelCommand(HWND window, int id);

// WM_LBUTTONDOWN / WM_MOUSEMOVE / WM_LBUTTONUP forwarding for the splitter.
// Returns true when the message was consumed (the splitter took the capture).
bool logsPanelMouse(HWND window, UINT message, WPARAM wParam, LPARAM lParam);

// Per-frame work: advance the asynchronous browser creation and push a resize
// if the layout moved. Cheap and inert while the panel is closed.
void logsPanelPump(HWND window);

// One frame interval, in milliseconds, for the responsiveness window.
void logsPanelSampleFrame(double milliseconds);

// The scripted sequence. Called once, after the window exists and before the
// message loop; it pumps messages itself while it waits, so the browser's
// callbacks land.
void logsPanelRunScript(HWND window);

// Trace lines: the runtime answer, the panel state, and (when --logstf-measure)
// the frame-time window. Emitted at the end of a run so they describe the run
// that happened, not the one that was planned.
std::string logsPanelRuntimeLine();
std::string logsPanelSummaryLine();
std::string logsPanelFrameLine();
bool logsPanelOpen();
void logsPanelShutdown();

// True when the panel is open and its browser has content, which is what a
// scripted run reports; exposed so the gate can print it without parsing.
bool logsPanelHasContent();

// The child window the browser draws into (null until the panel is attached).
HWND logsPanelHostWindow();

} // namespace tf2::native
