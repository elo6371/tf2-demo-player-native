#pragma once

// I6 step 2 (2026-11-23, docs/UI-SHELL-DESIGN-2026-11-22.md section 9): the match
// timeline strip drawn across the bottom of the client.
//
// Section 9 line 229 asks for: one background band per round, the band's top edge in
// the winner's colour (grey while the round is still running), and kill marks with RED
// kills drawn above the midline and BLU kills below it. Section 9 line 237 adds the
// exception track above it: red blocks whose width is how many ticks the anomaly
// lasted. (The Chinese source text is
// left in the design document: a C4819 warning on this machine's code page is a build
// diagnostic, and the gate treats one as a failure.)
// The data layer (demo_match_timeline.h) already puts rounds, kills and anomaly
// counts on ONE server-tick axis and publishes the playback->server offset. This
// file turns that into geometry and nothing else: no Direct3D, no window handle and
// no renderer type, so the composition can be checked from a probe without a
// device. main_hud.cpp is the only caller and converts these shapes into the
// renderer's HudPrimitive list (native_renderer.h), which is the product's single
// HUD drawing path.

#include "demo_match_timeline.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace tf2::native {

// The team colours are shared with the minimap: the strip and the minimap sit in the
// same frame and must agree on what RED looks like, so there is one definition.
inline constexpr float kTeamRedColour[4] = {0.90f, 0.20f, 0.18f, 1.0f};
inline constexpr float kTeamBluColour[4] = {0.20f, 0.42f, 0.95f, 1.0f};
inline constexpr float kTeamNeutralColour[4] = {0.92f, 0.90f, 0.85f, 1.0f};

// One drawable, in client pixels. Text carries no font: the caller picks its UI font
// (the strip has to render Chinese, which the embedded 8x8 ASCII face cannot).
struct TimelineHudShape {
  enum class Kind { Rect = 0, Text = 1 };
  Kind kind = Kind::Rect;
  float x = 0.0f;
  float y = 0.0f;
  float w = 0.0f;
  float h = 0.0f;
  float colour[4] = {1.0f, 1.0f, 1.0f, 1.0f};
  std::string text;
  float pixelSize = 0.0f;
};

// A rectangle already on screen that the strip must not cover (the minimap panel).
// placed = false means there is nothing to avoid.
struct TimelineHudAvoidRect {
  float x = 0.0f;
  float y = 0.0f;
  float w = 0.0f;
  float h = 0.0f;
  bool placed = false;
};

struct TimelineHudBand {
  std::int32_t index = 0;
  std::int32_t winTeam = -1;
  float x = 0.0f;
  float w = 0.0f;
  bool labelPlaced = false;
};

// A3 (section 9): one anomaly on the exception track. The product's detector reports in
// server ticks (demo_motion_detect.h), and so does the strip's axis, so no conversion is
// done here. kind matches MotionAnomalyKind: 0 stall, 1 teleport, 2 sharp turn (a hint,
// drawn thin rather than as a block).
struct TimelineHudAnomaly {
  int kind = 0;
  std::int32_t startTick = 0;
  std::int32_t endTick = 0;
  // A5: the review banner has to name the player and print the reading the detector
  // measured, and neither can be recovered from a tick range. -1 / 0 mean "the
  // detector did not attribute this one to a player", which is every stall.
  std::int32_t entity = -1;
  double metric = 0.0;
};

// Where one anomaly landed on the axis: a block's width is its duration in ticks
// (section 9: the red block's width is the number of ticks it lasted).
struct TimelineHudAnomalyMark {
  int kind = 0;
  std::int32_t startTick = 0;
  std::int32_t endTick = 0;
  float x = 0.0f;
  float w = 0.0f;
  bool hint = false;
};

// A3b (section 9, line 238): the per-tick row. The workspace picks ONE anomaly and
// the row unfolds the ticks around it -- one cell per tick -- so "which exact tick did
// it start" is read off a cell instead of estimated from a two-pixel block. Written in
// server ticks like the lane, because it is the same axis; the caller never converts.
struct TimelineHudFocus {
  bool valid = false;
  int kind = 0;  // same enum as TimelineHudAnomaly::kind: 2 is a hint, not a block
  std::int32_t startTick = 0;
  std::int32_t endTick = 0;
  // Section 9's plus-or-minus 20 ticks: the window is this many ticks on either side of the
  // anomaly's own span, clamped to the axis.
  std::int32_t halfWindow = 20;
};

// One tick of the focused window, in client pixels. anomaly/hint say what the cell is,
// so a gate can ask "the cell for tick N is red" without re-deriving the window.
struct TimelineHudCell {
  std::int32_t serverTick = 0;
  float x = 0.0f;
  float w = 0.0f;
  bool anomaly = false;
  bool hint = false;
};

// A4 (section 9.2 items 1 and 3): the two layers that exist only while the workspace
// follows a player. They are written in SERVER ticks like the lane and the row, so the
// caller never converts; only this file maps them onto the axis.
//
// A presence run is a stretch of the axis with one answer: state 1 = the recording had
// data and he was alive, 2 = data and he was dead, 0 = no data (drawn as nothing -- an
// empty bar under a POV recording is the honest reading of "he had left its view").
struct TimelineHudPresenceRun {
  std::int32_t startTick = 0;
  std::int32_t endTick = 0;
  int state = 0;
};

// One turn the recording measured, at the tick it ended on: degrees per server tick, and
// whether it is over the sharp-turn threshold (which is also what makes a hint amber on
// the exception lane, so the red bars and the amber marks agree by construction).
struct TimelineHudTurnPoint {
  std::int32_t tick = 0;
  double degrees = 0.0;
  bool over = false;
};

struct TimelineHudView {
  bool valid = false;
  std::int32_t entity = -1;
  std::vector<TimelineHudPresenceRun> presence;
  std::vector<TimelineHudTurnPoint> turns;
  std::size_t turnsOver = 0;
  double maxDegrees = 0.0;
  std::size_t aliveTicks = 0;
  std::size_t deadTicks = 0;
  std::size_t noDataTicks = 0;
};

struct TimelineHudStrip {
  bool visible = false;
  float x = 0.0f;      // the strip's own rectangle
  float y = 0.0f;
  float w = 0.0f;
  float h = 0.0f;
  float trackX0 = 0.0f;  // where firstServerTick maps
  float trackX1 = 0.0f;  // where lastServerTick maps
  float trackY = 0.0f;   // the kill midline
  float trackH = 0.0f;
  std::int32_t firstServerTick = 0;
  std::int32_t lastServerTick = 0;
  std::int32_t playheadServerTick = -1;  // -1 = no playhead on the axis
  float playheadX = 0.0f;
  std::size_t shapes = 0;
  std::size_t ticksRed = 0;
  std::size_t ticksBlu = 0;
  std::size_t ticksUnknown = 0;
  float anomalyLaneY = 0.0f;  // the exception track: between the labels and the kills
  float anomalyLaneH = 0.0f;
  std::size_t anomalyBlocks = 0;
  // Marks the axis could not show because they lie outside [first, last]. Counted, not
  // silently dropped: a gate reconciles blocks + hints + before + after against the
  // detector's list, so a mark that vanishes anywhere shows up as an arithmetic miss.
  std::size_t marksBefore = 0;
  std::size_t marksAfter = 0;  // stalls + teleports, drawn as red blocks
  std::size_t anomalyHints = 0;   // sharp turns, drawn as thin marks
  std::size_t stalls = 0;
  std::size_t teleports = 0;
  std::vector<TimelineHudBand> bands;
  std::vector<TimelineHudAnomalyMark> marks;
  // A3b: the per-tick row. Unfocused strips keep focusValid=false and cells=0, which is
  // every strip the A3 gate photographs, so their geometry is unchanged.
  bool focusValid = false;
  int focusKind = 0;
  std::int32_t focusStartTick = 0;
  std::int32_t focusEndTick = 0;
  std::int32_t focusWindowStart = 0;
  std::int32_t focusWindowEnd = 0;
  float cellsY = 0.0f;
  float cellsH = 0.0f;
  float cellW = 0.0f;
  std::size_t cells = 0;
  std::size_t anomalyCells = 0;
  std::size_t hintCells = 0;
  std::vector<TimelineHudCell> cellMarks;
  // A4: what the follow-only layers actually drew. Kept so the trace can print the runs
  // and turns a gate reads, exactly like cellMarks does for A3b's row.
  std::vector<TimelineHudPresenceRun> presenceMarks;
  std::vector<TimelineHudTurnPoint> turnMarks;
  // A4: what the follow-only layers drew. Unfollowed strips keep viewValid=false, so
  // every strip A3's gate photographs draws exactly what it did before.
  bool viewValid = false;
  std::int32_t viewEntity = -1;
  float presenceY = 0.0f;
  float presenceH = 0.0f;
  float curveY = 0.0f;
  float curveH = 0.0f;
  float curveThresholdY = 0.0f;
  std::size_t presenceRuns = 0;
  std::size_t presenceAlive = 0;
  std::size_t presenceDead = 0;
  std::size_t viewTurns = 0;
  std::size_t viewTurnsOver = 0;
  float viewMaxDegrees = 0.0f;
  std::vector<TimelineHudShape> primitives;
};

// clientW/clientH: the client area in pixels. playheadPlaybackTick: the player's
// current playback tick, or -1 when nothing is loaded; it is converted to the server
// axis with the report's own offset, so the caller never does that arithmetic.
// bottomInset: how much of the bottom edge is someone else's (a taskbar-like band the
// shell already reserves); 0 places the strip at the safe margin.
// anomalies: the A3 exception track, already on the server-tick axis. Empty draws the
// lane with no marks, so a run that never scanned keeps the same geometry as one that
// did -- a gate can tell "no anomalies" from "no track" instead of guessing.
// view: the A4 follow-only layers (presence bar, turn-rate curve). Invalid = nobody is
// followed, which is the state every strip A3's gate photographs is in, so those frames
// gain no pixels from A4 and keep their shape counts.
TimelineHudStrip composeTimelineStrip(const MatchTimelineReport& report,
                                      int clientW, int clientH,
                                      std::int32_t playheadPlaybackTick,
                                      const TimelineHudAvoidRect& avoid = TimelineHudAvoidRect{},
                                      float bottomInset = 0.0f,
                                      const std::vector<TimelineHudAnomaly>& anomalies =
                                          std::vector<TimelineHudAnomaly>{},
                                      const TimelineHudFocus& focus = TimelineHudFocus{},
                                      const TimelineHudView& view = TimelineHudView{});

// A3b: where a client point lands on the strip. Inside the per-tick row the answer is
// that cell's own server tick -- the row is one cell per tick, so a click on a cell is a
// click on a tick, not a proportional guess. Anywhere else inside the strip it is the
// axis position under the cursor. False when the strip is hidden or the point is off it,
// and the caller must then leave the transport alone.
bool timelineStripTickAt(const TimelineHudStrip& strip, float clientX, float clientY,
                         std::int32_t& serverTick);

// A4: converts the scan's per-entity motion history into what the strip draws. Lives
// here so the product's HUD and the probe's trace build the layers from ONE conversion
// instead of two copies that could disagree about a state or a tick.
struct MotionViewHistory;
TimelineHudView timelineHudViewFromHistory(const MotionViewHistory& history);

// One machine-readable line per strip plus one per band, so a gate can assert the
// geometry without a screenshot. Text is percent-encoded (one line = one record).
std::vector<std::string> formatTimelineHudLines(const TimelineHudStrip& strip);

// ------------------------------------------------------------------ A5 review
// Section 9.2's last paragraph, 逐条复查: walk the exception list one item at a
// time. Each item plays the three seconds before itself at a quarter speed, counts
// three seconds down while parked on the tick before it, holds one second ON the
// anomalous tick, then plays two seconds at full speed. The buttons are previous,
// replay, next, auto-next; Esc leaves and puts the tick back where it was.
//
// The machine is pure: it is fed wall seconds and the tick the playback ledger
// reports, and it answers with commands the caller hands to the ledger. Nothing in
// it touches Windows, the renderer or the playback object, so the probe drives the
// same states headlessly and the gate asserts the timings without a screenshot.

enum class TimelineReviewPhase {
  Idle = 0,       // not reviewing
  Slow = 1,       // 0.25x, from kLeadTicks before the anomaly to the tick before it
  Countdown = 2,  // parked on that tick, showing 3 then 2 then 1
  Hold = 3,       // parked ON the anomaly's first tick, one second
  Normal = 4,     // 1x for two seconds, starting at that tick
  Wait = 5,       // the item is over; nothing moves until a button says so
};

const char* timelineReviewPhaseName(TimelineReviewPhase phase);

// What the machine asks the playback ledger to do. A phase may emit several in one
// update (seek, then speed, then unpause), and the caller applies them in order.
struct TimelineReviewCommand {
  enum class Kind { Seek = 0, Speed = 1, Pause = 2 };
  Kind kind = Kind::Seek;
  std::int32_t tick = 0;
  double speed = 1.0;
  bool paused = false;
};

struct TimelineReviewView {
  bool active = false;
  int index = -1;
  int count = 0;
  TimelineReviewPhase phase = TimelineReviewPhase::Idle;
  // 3, 2, 1 while counting down; 0 otherwise, so a gate reads the digit off one field.
  int countdown = 0;
  double phaseSeconds = 0.0;
  bool autoNext = false;
  int kind = -1;
  std::int32_t entity = -1;
  double metric = 0.0;
  std::int32_t startTick = 0;
  std::int32_t endTick = 0;
  std::int32_t savedTick = 0;  // where Esc will put the playhead back
};

class TimelineReviewMachine {
 public:
  // The slow run's lookback: 1.5 s of demo time at the 66.67 Hz tick rate. That is six
  // seconds of wall time at 0.25x - a three second lookback would park the viewer in slow
  // motion for twelve, which reads as a hang rather than as a setup.
  static constexpr std::int32_t kLeadTicks = 100;
  static constexpr double kSlowSpeed = 0.25;
  // The slow phase only trusts the cursor once it has been seen behind the anomaly, and
  // this is the wall-clock net: an anomaly on the axis' first tick has nothing behind it
  // to play, so without the net the phase would never park.
  static constexpr double kSlowArmTimeoutSeconds = 20.0;
  static constexpr double kCountdownStep = 1.0;
  static constexpr double kHoldSeconds = 1.0;
  static constexpr double kNormalSeconds = 2.0;

  // Anomalies in the order the strip drew them; hints travel with them because the
  // design's list carries both (9.3's table calls the sharp turn a 提示, still a row).
  void setAnomalies(const std::vector<TimelineHudAnomaly>& anomalies, std::int32_t axisFirst,
                    std::int32_t axisLast);
  void start(std::int32_t currentTick);
  // Open PARKED on item `index`: active, shown in the banner, and with nothing queued, so
  // the transport is untouched. This is what a caller that only wants the banner uses (a
  // capture with no keyboard, and later the list's "open here"); start() is the one that
  // plays, and the difference is exactly the command list.
  void openAt(int index, std::int32_t currentTick);
  void stop();
  // dt is wall seconds; tick is the playback ledger's current server tick.
  void update(double dt, std::int32_t tick);
  void step(int direction);  // -1 previous, +1 next (both wrap at the ends)
  void replay();
  void toggleAuto();
  std::vector<TimelineReviewCommand> takeCommands();
  TimelineReviewView view() const;
  bool active() const { return view_.active; }
  std::size_t itemCount() const { return items_.size(); }
  std::int32_t restoreTick() const { return view_.savedTick; }

 private:
  void enterItem(int index);

  std::vector<TimelineHudAnomaly> items_;
  std::int32_t axisFirst_ = 0;
  std::int32_t axisLast_ = 0;
  TimelineReviewView view_;
  double phaseSeconds_ = 0.0;
  bool slowArmed_ = false;
  std::vector<TimelineReviewCommand> commands_;
};

// The banner and its buttons, in client pixels. Buttons come in TimelineReviewButton
// order so a hit test is a scan, not a table.
enum class TimelineReviewButton { Prev = 0, Replay = 1, Next = 2, Auto = 3, Exit = 4 };
constexpr int kTimelineReviewButtonCount = 5;

struct TimelineReviewOverlay {
  bool active = false;
  float x = 0.0f;
  float y = 0.0f;
  float w = 0.0f;
  float h = 0.0f;
  std::vector<TimelineHudShape> primitives;
  // One entry per button, in TimelineReviewButton order.
  std::vector<TimelineHudShape> buttons;
};

TimelineReviewOverlay composeReviewOverlay(const TimelineReviewView& view, int clientW);
// Which button a client point is over: kTimelineReviewButtonCount when none.
int reviewButtonAt(const TimelineReviewOverlay& overlay, float clientX, float clientY);
// The machine-readable review: line the gate reads, plus one per button.
std::vector<std::string> formatTimelineReviewLines(const TimelineReviewView& view,
                                                   const TimelineReviewOverlay& overlay);

} // namespace tf2::native
