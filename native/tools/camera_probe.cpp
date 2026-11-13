#include "camera_bookmarks.h"
#include "camera_director.h"
#include "keybinds.h"

#include <Windows.h>

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

// WHY THIS FILE EXISTS
//   T50 makes two claims a human cannot check by looking: "a paused demo tick
//   does not move while the camera does" and "a bookmarked pose comes back
//   exactly". Both are claims about numbers, so both get a probe that reads the
//   numbers, and camera-ui-check.sh pins the probe line.
//
//   The probe is also where the settings file is exercised for real: it writes
//   [camera] and [keys] to a private file under the temp directory, reads them
//   back, and compares the two writes byte for byte. That is the same code path
//   the product takes at exit, so "the bookmark survived a restart" is measured
//   without needing a restart.

namespace {

std::uint32_t g_failures = 0;

void check(bool ok, const char* what) {
  if (!ok) {
    ++g_failures;
    std::cerr << "FAIL " << what << "\n";
  }
}

class Readings {
public:
  void set(const std::string& key, const std::string& value) {
    for (auto& entry : entries_) {
      if (entry.first == key) { entry.second = value; return; }
    }
    entries_.emplace_back(key, value);
  }
  void set(const std::string& key, std::uint32_t value) { set(key, std::to_string(value)); }
  std::string line() const {
    std::string result;
    for (const auto& entry : entries_) {
      if (!result.empty()) result += " ";
      result += entry.first + "=" + entry.second;
    }
    return result;
  }
private:
  std::vector<std::pair<std::string, std::string>> entries_;
};

tf2::native::CameraPose makePose(float x, float y, float z, float yaw, float pitch, float distance) {
  tf2::native::CameraPose pose;
  pose.focusX = x; pose.focusY = y; pose.focusZ = z;
  pose.yaw = yaw; pose.pitch = pitch; pose.distance = distance;
  pose.valid = true;
  return pose;
}

// 1. Pause freezes the demo tick; the camera keeps moving; resume advances.
void checkPauseFreezesTickNotCamera(Readings& readings) {
  tf2::native::CameraDirector director;
  // Ten unpaused frames at 66 ticks/s worth of progress: the tick moves.
  for (std::int32_t tick = 0; tick < 10; ++tick) director.noteDemoTick(false, tick);
  check(director.advancedDemoFrames() == 9, "unpaused frames advance the tick");
  check(director.pauseViolations() == 0, "no violation while unpacked");
  const std::int32_t frozenTick = director.lastTick();

  // Pause. The tick must not move; the CAMERA must. Both are asserted here,
  // separately, because the task asks for two readings and a single combined
  // "nothing moved" assertion would pass for the wrong reason.
  director.setPaused(true);
  const std::uint32_t frozenBefore = director.frozenDemoFrames();
  const std::uint32_t movesBefore = director.cameraMoves();
  const std::uint32_t pausedMovesBefore = director.cameraMovesWhilePaused();
  for (int frame = 0; frame < 20; ++frame) {
    director.noteDemoTick(true, frozenTick);
  }
  check(director.lastTick() == frozenTick, "a paused demo tick stays fixed");
  check(director.frozenDemoFrames() - frozenBefore == 20,
    "every paused frame is counted as frozen, not merely uncounted");
  check(director.pauseViolations() == 0, "no pause violation while the tick holds");
  check(director.cameraMoves() == movesBefore, "a paused camera does not move by itself");

  // Now the camera moves while paused: this is the half a "nothing moved"
  // assertion would miss.
  const auto poseBefore = director.pose();
  director.orbit(40.0f, 12.0f);
  check(!tf2::native::poseEqual(director.pose(), poseBefore),
    "an orbit changes the pose while paused");
  check(director.lastTick() == frozenTick, "the orbit did not move the demo tick");
  check(director.cameraMovesWhilePaused() > pausedMovesBefore,
    "camera motion during pause is counted as such");
  director.zoom(2.0f);
  check(!tf2::native::poseEqual(director.pose(), poseBefore), "a zoom changes the pose while paused");
  check(director.lastTick() == frozenTick, "the zoom did not move the demo tick");
  readings.set("pauseFrozenTick", frozenTick >= 0 ? 1u : 0u);
  readings.set("pauseCameraMoves", director.cameraMovesWhilePaused());
  readings.set("pauseFrozenFrames", director.frozenDemoFrames() - frozenBefore);

  // Resume: the tick advances again from exactly where it stopped.
  director.setPaused(false);
  const std::uint32_t advancedBefore = director.advancedDemoFrames();
  for (std::int32_t tick = frozenTick + 1; tick <= frozenTick + 5; ++tick) {
    director.noteDemoTick(false, tick);
  }
  check(director.advancedDemoFrames() - advancedBefore == 5,
    "resuming advances the tick again");
  check(director.lastTick() == frozenTick + 5, "the tick continues from where it stopped");
  check(director.pauseViolations() == 0, "resume produced no violation");
  readings.set("pauseResumeAdvance", director.advancedDemoFrames() - advancedBefore);

  // The counter must be able to notice a broken freeze: feed the director a
  // tick that moved while paused and require it to be counted.
  tf2::native::CameraDirector broken;
  broken.noteDemoTick(true, 100);
  broken.noteDemoTick(true, 101);
  check(broken.pauseViolations() == 1, "a tick that moves while paused is counted as a violation");
  readings.set("pauseViolationDetected", broken.pauseViolations());

  // The rule itself, read as a function.
  check(!tf2::native::CameraDirector::demoClockAdvances(true)
    && tf2::native::CameraDirector::demoClockAdvances(false),
    "the freeze rule is a function of the paused flag alone");
}

// 2. Free-fly / reset-to-player / reset-view.
void checkCameraOperations(Readings& readings) {
  tf2::native::CameraDirector director;
  director.applyPose(makePose(0.1f, 0.2f, 0.3f, 1.0f, 0.5f, 2.0f),
    tf2::native::CameraMoveSource::Player);
  const auto base = director.pose();

  director.setFreeFly(true);
  check(director.freeFly(), "free-fly can be switched on");
  const std::uint32_t stepsBefore = director.freeFlySteps();
  // Free-fly is an orbit with no target feeding it: the same delta that orbits
  // first-person must move the pose here too.
  director.orbit(-80.0f, 40.0f);
  check(director.freeFlySteps() >= stepsBefore, "free-fly steps are counted");
  check(!tf2::native::poseEqual(director.pose(), base), "free-fly moves the pose");
  director.setFreeFly(false);
  check(!director.freeFly(), "free-fly can be switched off");

  // reset-to-player: latched, applied once, and counted on both sides.
  const std::uint32_t requestsBefore = director.resetToPlayerRequests();
  check(!director.takeResetToPlayer(), "reset-to-player is not pending before it is asked for");
  director.requestResetToPlayer();
  check(director.resetToPlayerRequests() == requestsBefore + 1, "the request is counted");
  check(director.takeResetToPlayer(), "a pending reset-to-player applies");
  check(!director.takeResetToPlayer(), "the latch clears after one application");
  check(director.resetToPlayerApplies() == 1, "one request applies exactly once");
  readings.set("resetToPlayer", 1u);

  // reset-view: back to the shipped orbit defaults, and the bookmarks survive
  // it. The second half is the one a user notices: "reset view" that deleted
  // saved positions would be a data loss disguised as a view command.
  tf2::native::CameraDirector resetting;
  resetting.applyPose(makePose(0.4f, -0.2f, 0.6f, 2.0f, 1.2f, 9.0f),
    tf2::native::CameraMoveSource::Bookmark);
  resetting.setFreeFly(true);
  resetting.resetView();
  check(resetting.pose().yaw == tf2::native::kDefaultCameraYaw, "reset-view restores the default yaw");
  check(resetting.pose().pitch == tf2::native::kDefaultCameraPitch, "reset-view restores the default pitch");
  check(resetting.pose().distance == tf2::native::kDefaultCameraDistance,
    "reset-view restores the default distance");
  check(resetting.pose().focusZ == tf2::native::kDefaultFocusZ, "reset-view restores the default focus height");
  check(!resetting.freeFly(), "reset-view leaves free-fly");
  check(resetting.resetViewCount() == 1, "reset-view is counted");
  readings.set("resetView", 1u);
}

// 3. Three named bookmarks: save / name / delete / jump, exact pose.
void checkBookmarks(Readings& readings, const std::filesystem::path& settingsFile) {
  tf2::native::CameraBookmarkStore store;
  check(store.savedCount() == 0, "a fresh store has no bookmarks");

  // fov values deliberately differ between the three slots: an implementation
  // that shared one fov across the store, or that ignored the argument, would
  // pass with three identical numbers and fail here.
  struct Case { std::size_t slot; const char* name; tf2::native::CameraPose pose; float fov; };
  const Case cases[3] = {
    { 1, "tower", makePose(0.123456789f, -0.987654321f, 0.456789012f, 1.23456789f, 0.654321098f, 9.87654321f), 100.0f },
    { 2, "spawn door", makePose(-0.5f, 0.25f, 0.75f, -3.0f, 0.3f, 1.5f), 85.0f },
    { 3, "mid high", makePose(0.0f, 0.0f, 0.9f, 0.0f, 1.4f, 11.5f), 60.0f },
  };
  for (const auto& item : cases) {
    check(store.save(item.slot, item.name, item.pose, "third-person", 1234, item.fov),
      "a bookmark saves");
  }
  check(store.savedCount() == 3, "three bookmarks are stored");
  readings.set("bookmarksSaved", store.savedCount());

  // Exact restoration, straight from memory.
  std::uint32_t exact = 0;
  for (const auto& item : cases) {
    if (store.saved(item.slot) && tf2::native::poseEqual(store.at(item.slot).pose, item.pose)) ++exact;
  }
  check(exact == 3, "every saved pose comes back exactly");
  readings.set("bookmarkPoseExact", exact);
  // The projection is part of the saved camera (contract 1.1 `bookmarkN.fov`),
  // so it gets its own exact count rather than hiding inside the pose one.
  std::uint32_t fovExact = 0;
  for (const auto& item : cases) {
    if (store.saved(item.slot) && store.at(item.slot).fov == item.fov) ++fovExact;
  }
  check(fovExact == 3, "every saved fov comes back exactly");
  readings.set("bookmarkFovExact", fovExact);
  // Underscores, not spaces: the readings line is space-separated, so a name
  // with a space inside a value would split into two tokens for any reader.
  readings.set("bookmarkNames", std::string("tower|spawn_door|mid_high"));

  // A save with no name gets the default name instead of an empty label.
  check(store.save(4, "", makePose(0.1f, 0.1f, 0.1f, 0.1f, 0.1f, 1.0f), "free-look", 7),
    "a bookmark saves without a typed name");
  check(store.at(4).name == tf2::native::defaultBookmarkName(4),
    "an unnamed bookmark falls back to its default name");
  check(store.at(4).fov == tf2::native::kDefaultBookmarkFov,
    "a bookmark saved without an fov keeps the shipped default");
  check(store.remove(4), "the unnamed bookmark deletes");
  check(!store.saved(4), "a deleted bookmark is gone");
  check(!store.remove(4), "deleting an empty slot is refused, not silently ignored");

  // Out-of-range slots are refused rather than clamped.
  check(!store.save(0, "zero", makePose(0, 0, 0, 0, 0, 1), "first-person", 0),
    "slot 0 is refused");
  check(!store.save(9, "nine", makePose(0, 0, 0, 0, 0, 1), "first-person", 0),
    "slot 9 is refused");
  check(!store.saved(9), "an out-of-range slot reads as not saved");

  // Rename keeps the pose.
  const auto beforeRename = store.at(2).pose;
  check(store.rename(2, "spawn door (red)"), "a bookmark renames");
  check(store.at(2).name == "spawn door (red)", "the new name is stored");
  check(tf2::native::poseEqual(store.at(2).pose, beforeRename), "a rename does not touch the pose");

  // --- the settings file: the durable half --------------------------------
  check(store.saveToSettings(settingsFile), "bookmarks persist");
  tf2::native::CameraBookmarkStore reloaded;
  check(reloaded.loadFromSettings(settingsFile), "bookmarks reload from the settings file");
  check(reloaded.savedCount() == 3, "exactly three bookmarks came back");
  std::uint32_t fileExact = 0;
  for (const auto& item : cases) {
    if (reloaded.saved(item.slot) && tf2::native::poseEqual(reloaded.at(item.slot).pose, store.at(item.slot).pose)) {
      ++fileExact;
    }
  }
  check(fileExact == 3, "every pose survives a write/read cycle exactly");
  readings.set("bookmarkFilePoseExact", fileExact);
  std::uint32_t fileFovExact = 0;
  for (const auto& item : cases) {
    if (reloaded.saved(item.slot) && reloaded.at(item.slot).fov == item.fov) ++fileFovExact;
  }
  check(fileFovExact == 3, "every fov survives a write/read cycle exactly");
  readings.set("bookmarkFileFovExact", fileFovExact);
  check(reloaded.at(2).name == "spawn door (red)", "the renamed bookmark keeps its name on disk");
  check(reloaded.at(1).mode == "third-person", "the camera mode survives on disk");

  // Two writes of the same store are byte-identical: the file format has no
  // ordering or formatting drift, which is what makes a byte comparison of a
  // reloaded file meaningful.
  const auto firstBytes = [&](const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
  };
  check(reloaded.saveToSettings(settingsFile), "the reloaded store re-saves");
  const std::string fileOnce = firstBytes(settingsFile);
  check(reloaded.saveToSettings(settingsFile), "the reloaded store saves a second time");
  check(fileOnce == firstBytes(settingsFile), "two saves of the same bookmarks are byte-identical");
  readings.set("bookmarkFileStable", fileOnce == firstBytes(settingsFile) ? 1u : 0u);

  // Deleting is durable: a slot removed before the save is absent after the
  // reload, not resurrected from the old file.
  check(reloaded.remove(3), "a reloaded bookmark deletes");
  check(reloaded.saveToSettings(settingsFile), "the deletion persists");
  tf2::native::CameraBookmarkStore afterDelete;
  check(afterDelete.loadFromSettings(settingsFile), "the file reloads after a deletion");
  check(afterDelete.savedCount() == 2, "the deleted bookmark does not come back");
  check(!afterDelete.saved(3), "the deleted slot is empty");
  readings.set("bookmarkDeletePersists", 1u);

  // A corrupt pose is refused rather than defaulted: a bookmark whose numbers
  // cannot be read must not jump the camera somewhere the user never saved.
  const auto corrupt = std::filesystem::temp_directory_path() / L"tf2-t50-corrupt-settings.ini";
  {
    std::ofstream file(corrupt, std::ios::binary);
    file << "[camera]\nbookmark1.saved=1\nbookmark1.name=broken\n"
         << "bookmark1.focusX=not-a-number\nbookmark1.focusY=0\nbookmark1.focusZ=0.5\n"
         << "bookmark1.yaw=0\nbookmark1.pitch=0.5\nbookmark1.distance=3\n";
  }
  tf2::native::CameraBookmarkStore corruptStore;
  check(corruptStore.loadFromSettings(corrupt), "a corrupt settings file still loads");
  check(!corruptStore.saved(1), "a bookmark with an unreadable pose is refused");
  std::error_code cleanup;
  std::filesystem::remove(corrupt, cleanup);
  readings.set("bookmarkCorruptRefused", corruptStore.refusedCount() > 0 ? 1u : 0u);

  // Jump accounting: the director counts a jump as a bookmark application so the
  // trace can separate "the key fired" from "the pose changed".
  tf2::native::CameraDirector director;
  director.applyPose(store.at(1).pose, tf2::native::CameraMoveSource::Bookmark);
  check(director.bookmarkApplies() == 1, "a bookmark jump is counted as one");
  check(tf2::native::poseEqual(director.pose(), store.at(1).pose),
    "the director holds exactly the bookmarked pose");
  // And the pose is deterministic: re-applying the same bookmark is a no-op on
  // the pose (this is what makes two captures of it byte-identical).
  const std::uint32_t changes = director.poseChanges();
  director.applyPose(store.at(1).pose, tf2::native::CameraMoveSource::Bookmark);
  check(director.poseChanges() == changes, "re-applying the same bookmark changes no pose");
  readings.set("bookmarkJumpStable", director.bookmarkApplies() == 2 ? 1u : 0u);
}

// 4. Hotkeys: parsing, conflicts, missing bindings, reset, text-field focus.
void checkKeybinds(Readings& readings, const std::filesystem::path& settingsFile) {
  using namespace tf2::native;

  // --- the grammar -------------------------------------------------------
  check(formatChord(parseChord("F9")) == "F9", "F9 round-trips");
  check(formatChord(parseChord("ctrl+F9")) == "Ctrl+F9", "modifiers are case-insensitive");
  check(formatChord(parseChord("Shift+Ctrl+F1")) == "Ctrl+Shift+F1",
    "any modifier order re-emits canonically");
  check(formatChord(parseChord("")) == "", "unbound formats as the empty string");
  check(!parseChord("").bound(), "the empty string parses as unbound");
  check(!parseChord("Ctrl+Shift+").bound(), "a trailing plus is not a chord");
  check(!parseChord("F25").bound(), "F25 is outside the function-key range");
  check(parseChord("VK:192").vk == 192, "the VK: escape hatch parses");
  check(formatChord(parseChord("VK:192")) == "VK:192", "an unnamed key round-trips through VK:");
  const char* roundTrip[] = { "A", "Z", "0", "9", "F1", "F24", "Numpad5", "Space",
    "PageUp", "Plus", "Minus", "Ctrl+Alt+Shift+F12", "Alt+H" };
  std::uint32_t roundTrips = 0;
  for (const char* text : roundTrip) {
    if (formatChord(parseChord(text)) == text) ++roundTrips;
  }
  check(roundTrips == sizeof(roundTrip) / sizeof(roundTrip[0]), "every canonical name round-trips");
  readings.set("chordRoundTrips", roundTrips);

  // --- defaults ----------------------------------------------------------
  KeyBindTable table = defaultKeyBindings();
  check(table.registered(camera_actions::kFreeFly), "the free-fly action is registered");
  check(table.actionKey(camera_actions::kFreeFly) == parseChord("F9"), "free-fly defaults to F9");
  check(table.actionKey(camera_actions::kResetToPlayer) == parseChord("F10"),
    "reset-to-player defaults to F10");
  check(table.actionKey(camera_actions::kResetView) == parseChord("F11"), "reset-view defaults to F11");
  check(table.actionKey(camera_actions::jumpBookmark(1)) == parseChord("F1"),
    "the jump keys keep the legacy preset positions (F1..F8)");
  check(table.actionKey(camera_actions::saveBookmark(1)) == parseChord("Ctrl+F1"),
    "the save keys keep the legacy Ctrl+F1..F8");
  check(table.actionKey(camera_actions::deleteBookmark(8)) == parseChord("Ctrl+Shift+F8"),
    "the delete keys keep the legacy Ctrl+Shift+F1..F8");
  check(table.actions().size() == 27, "24 bookmark actions plus three camera actions");
  readings.set("keyActions", static_cast<std::uint32_t>(table.actions().size()));

  // --- conflict detection ------------------------------------------------
  check(table.conflicts(parseChord("F1")).size() == 1, "a key bound once reports a single owner");
  const std::uint32_t conflictsBefore = table.conflictCount();
  check(!table.set(camera_actions::kFreeFly, parseChord("F1")),
    "binding a key another action holds is refused");
  check(table.conflictCount() == conflictsBefore + 1, "the refusal is counted as a conflict");
  check(table.actionKey(camera_actions::kFreeFly) == parseChord("F9"),
    "a refused assignment leaves the table unchanged");
  check(table.conflictsFor(camera_actions::kFreeFly).empty(),
    "the refused action has no conflicts of its own");
  check(!table.set(camera_actions::kResetView, parseChord("F9")),
    "a second collision on the same key is also refused");
  check(table.set(camera_actions::kResetView, parseChord("Alt+F11")),
    "a free key is accepted");
  check(table.conflictsFor(camera_actions::kResetView).empty(), "the new binding has no conflict");
  check(!table.set("camera.notAnAction", parseChord("F12")), "an unregistered action is refused");
  readings.set("keyConflictsRefused", table.conflictCount() - conflictsBefore);

  // --- the SHAARED table: a second feature registers into it ---------------
  // Contract 2 promises T51 that its hud.* actions go into THIS table, so a
  // chord one feature wants and the other already holds is refused without
  // either side knowing the other's action list. Tested here, in pure logic,
  // because the alternative is discovering it in a build where two features have
  // silently claimed F9 and the one that loses is decided by map iteration order.
  {
    KeyBindTable shared = defaultKeyBindings();
    // T51's three actions, with the ids AND default chords from contract 1.2
    // (hud.toggle=H, hud.preview=Ctrl+P, hud.reset=Ctrl+Shift+H). Spelled here
    // exactly as T51 registers them: a test that registered a name the product
    // will never have would pass while proving nothing about the real table.
    shared.registerDefault("hud.toggle", parseChord("H"));
    shared.registerDefault("hud.preview", parseChord("Ctrl+P"));
    shared.registerDefault("hud.reset", parseChord("Ctrl+Shift+H"));
    check(shared.registered("hud.toggle"), "a second feature can register its actions");
    check(shared.actions().size() == 30, "the table holds both features' actions");
    // Now the collision: the HUD wants a key the camera holds.
    const std::uint32_t before = shared.conflictCount();
    check(!shared.set("hud.toggle", parseChord("F1")),
      "a HUD binding on a camera key is refused");
    check(shared.conflictCount() == before + 1, "the cross-feature refusal is counted");
    check(shared.actionKey(camera_actions::jumpBookmark(1)) == parseChord("F1"),
      "the camera action keeps its key");
    check(shared.actionKey("hud.toggle") == parseChord("H"),
      "the refused HUD action keeps its own key");
    // And the reverse direction: the camera must not take a key the HUD holds.
    check(!shared.set(camera_actions::kFreeFly, parseChord("Ctrl+P")),
      "a camera binding on a HUD key is refused too");
    check(shared.actionKey(camera_actions::kFreeFly) == parseChord("F9"),
      "the camera action keeps its key after the reverse refusal");
    // A conflict query names the owner, which is what a UI has to show.
    const auto owners = shared.conflicts(parseChord("F1"));
    check(owners.size() == 1 && owners[0] == camera_actions::jumpBookmark(1),
      "the conflict query names the action that owns the key");
    // resetAll restores BOTH features' defaults and does not disturb the
    // registrations: a second feature's registration is a default, not a rebind.
    shared.set("hud.preview", parseChord("Alt+F12"));
    shared.resetAll();
    check(shared.actionKey("hud.preview") == parseChord("Ctrl+P"),
      "resetAll restores a second feature's default as well");
    check(shared.actionKey(camera_actions::kFreeFly) == parseChord("F9"),
      "resetAll leaves the camera defaults exactly as compiled");
    check(shared.actions().size() == 30, "resetAll does not drop any action");
    readings.set("keySharedTable", 1u);
  }

  // --- missing bindings --------------------------------------------------
  table.unbind(camera_actions::kResetView);
  check(table.unbound(camera_actions::kResetView), "an unbound action reads as unbound");
  const KeyDispatch missing = table.dispatch(parseChord("Alt+F11"));
  check(missing.result == KeyDispatchResult::Unbound && missing.action.empty(),
    "dispatching a released key reports unbound, never a fallback action");
  check(table.actionForKey(parseChord("Alt+F11")) == nullptr,
    "a released key resolves to no action");
  readings.set("keyUnboundDispatch", 1u);

  // --- dispatch ----------------------------------------------------------
  const KeyDispatch jump = table.dispatch(parseChord("F1"));
  check(jump.result == KeyDispatchResult::Ok && jump.action == camera_actions::jumpBookmark(1),
    "F1 dispatches the first jump action");
  const KeyDispatch unknown = table.dispatch(parseChord("Ctrl+Alt+Z"));
  check(unknown.result == KeyDispatchResult::Unbound, "an unbound chord dispatches nothing");
  readings.set("keyDispatchOk", table.dispatchOkCount());

  // --- the three resets ---------------------------------------------------
  table.set(camera_actions::kFreeFly, parseChord("Ctrl+Alt+F9"));
  check(table.actionKey(camera_actions::kFreeFly) == parseChord("Ctrl+Alt+F9"),
    "a rebind takes effect");
  check(table.resetAction(camera_actions::kFreeFly), "resetAction runs");
  check(table.actionKey(camera_actions::kFreeFly) == parseChord("F9"),
    "resetAction restores this action's default");
  check(table.unbound(camera_actions::kResetView), "resetAction leaves another action alone");
  table.resetAll();
  check(!table.unbound(camera_actions::kResetView), "resetAll restores the unbound action too");
  check(table.actionKey(camera_actions::kResetView) == parseChord("F11"),
    "resetAll restores the compiled defaults");
  table.unbindAll();
  std::uint32_t allUnbound = 0;
  for (const auto& action : table.actions()) {
    if (table.unbound(action)) ++allUnbound;
  }
  check(allUnbound == table.actions().size(), "unbindAll releases every action");
  check(table.actionForKey(parseChord("F1")) == nullptr, "nothing resolves after unbindAll");
  table.resetAll();
  readings.set("keyResetCount", table.resetCount());

  // --- persistence --------------------------------------------------------
  table.set(camera_actions::kFreeFly, parseChord("Ctrl+Alt+F9"));
  table.unbind(camera_actions::kResetToPlayer);
  check(table.saveToSettings(settingsFile), "the key table persists");
  KeyBindTable reloaded = defaultKeyBindings();
  check(reloaded.loadFromSettings(settingsFile), "the key table reloads");
  check(reloaded.actionKey(camera_actions::kFreeFly) == parseChord("Ctrl+Alt+F9"),
    "a rebind survives the file");
  check(reloaded.unbound(camera_actions::kResetToPlayer),
    "an explicit unbind survives the file (it is not restored to the default)");
  check(reloaded.actionKey(camera_actions::kResetView) == parseChord("F11"),
    "an action absent from the file keeps its default");
  const auto firstRead = reloaded.actions().size();
  check(reloaded.saveToSettings(settingsFile), "the reloaded table saves");
  KeyBindTable twice = defaultKeyBindings();
  twice.loadFromSettings(settingsFile);
  check(twice.actionKey(camera_actions::kFreeFly) == parseChord("Ctrl+Alt+F9"),
    "the second cycle reads the same binding");
  check(firstRead == twice.actions().size(), "the action count is stable across a cycle");
  readings.set("keyFileRoundTrip", 1u);

  // A file binding one chord to two actions: the second is refused, so the
  // product never has two owners for one key. The chord is one no default
  // claims (Alt+F5), so the first line really is accepted and the second really
  // is the one refused -- with a chord that already had an owner, both lines
  // would be refused and the assertion would pass for the wrong reason.
  const auto duplicate = std::filesystem::temp_directory_path() / L"tf2-t50-duplicate-keys.ini";
  {
    std::ofstream file(duplicate, std::ios::binary);
    file << "[keys]\ncamera.freeFly=Alt+F5\ncamera.resetView=Alt+F5\n";
  }
  KeyBindTable dup = defaultKeyBindings();
  check(dup.loadFromSettings(duplicate), "a file with a duplicate loads");
  check(dup.actionKey(camera_actions::kFreeFly) == parseChord("Alt+F5"),
    "the first line of the duplicate pair is accepted");
  check(dup.actionKey(camera_actions::kResetView) == parseChord("F11"),
    "the second action keeps its default instead of sharing the key");
  check(dup.conflictsFor(camera_actions::kFreeFly).empty(),
    "the accepted action owns the key alone");
  std::error_code cleanup;
  std::filesystem::remove(duplicate, cleanup);
  readings.set("keyDuplicateRefused", 1u);

  // --- text-field focus ---------------------------------------------------
  check(shouldDispatchHotkey(false, true), "with no text field, a character key dispatches");
  check(shouldDispatchHotkey(false, false), "with no text field, a function key dispatches");
  check(!shouldDispatchHotkey(true, true),
    "with a text field focused, a character key does NOT dispatch");
  check(!shouldDispatchHotkey(true, false),
    "with a text field focused, a bound function key does NOT dispatch either");
  readings.set("keyFocusSuppresses", 1u);
}

} // namespace

int main() {
  using namespace tf2::native;
  Readings readings;

  const auto scratch = std::filesystem::temp_directory_path() / L"tf2-t50-camera-probe";
  std::error_code error;
  std::filesystem::remove_all(scratch, error);
  std::filesystem::create_directories(scratch, error);
  if (error) {
    std::cerr << "FAIL cannot create the scratch directory\n";
    return 1;
  }
  const auto bookmarkFile = scratch / L"bookmarks.ini";
  const auto keyFile = scratch / L"keys.ini";

  checkPauseFreezesTickNotCamera(readings);
  checkCameraOperations(readings);
  checkBookmarks(readings, bookmarkFile);
  checkKeybinds(readings, keyFile);

  std::error_code cleanup;
  std::filesystem::remove_all(scratch, cleanup);

  if (g_failures != 0) {
    std::cout << "FAIL camera_probe failures=" << g_failures << "\n";
    return 1;
  }
  std::cout << "PASS camera_probe pause=freeze-tick camera=continues bookmarks=3 keys=conflict\n";
  std::cout << readings.line() << "\n";
  return 0;
}
