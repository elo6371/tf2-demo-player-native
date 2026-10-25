// T71/T72 review workspace + bookmark probe. Console app; prints a trace the
// review-workspace-check.sh gate parses. Everything runs in a scratch directory
// so a probe run never touches the user's real workspace store.
#include <windows.h>

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "camera_director.h"
#include "install_manifest.h"
#include "review_bookmarks.h"
#include "review_workspace.h"
#include "workspace_types.h"

namespace fs = std::filesystem;
using namespace tf2::native;

namespace {

int g_failures = 0;
int g_fields = 0;
int g_fieldMismatches = 0;

std::string fmtF32(float value) {
  char buffer[64];
  std::snprintf(buffer, sizeof(buffer), "%.9g", static_cast<double>(value));
  return buffer;
}

std::string fmtF64(double value) {
  char buffer[64];
  std::snprintf(buffer, sizeof(buffer), "%.17g", value);
  return buffer;
}

std::string percent(const std::string& text) { return workspacePercentEncode(text); }

void fieldLine(const std::string& name, const std::string& expected, const std::string& actual) {
  ++g_fields;
  const bool match = expected == actual;
  if (!match) ++g_fieldMismatches;
  std::printf("review-workspace: field name=%s expected=%s actual=%s match=%d\n", name.c_str(),
              percent(expected).c_str(), percent(actual).c_str(), match ? 1 : 0);
}

void fieldS(const std::string& name, const std::string& expected, const std::string& actual) {
  fieldLine(name, expected, actual);
}
void fieldI(const std::string& name, long long expected, long long actual) {
  fieldLine(name, std::to_string(expected), std::to_string(actual));
}
void fieldB(const std::string& name, bool expected, bool actual) {
  fieldLine(name, expected ? "1" : "0", actual ? "1" : "0");
}
void fieldF32(const std::string& name, float expected, float actual) {
  fieldLine(name, fmtF32(expected), fmtF32(actual));
}
void fieldF64(const std::string& name, double expected, double actual) {
  fieldLine(name, fmtF64(expected), fmtF64(actual));
}

void check(const std::string& name, bool condition, const std::string& detail = std::string()) {
  if (!condition) ++g_failures;
  std::printf("review-workspace: expect name=%s ok=%d", name.c_str(), condition ? 1 : 0);
  if (!detail.empty()) std::printf(" detail=%s", percent(detail).c_str());
  std::printf("\n");
}

void step(const std::string& name, const std::string& result, const std::string& extra = std::string()) {
  std::printf("review-workspace: step=%s result=%s", name.c_str(), result.c_str());
  if (!extra.empty()) std::printf(" %s", extra.c_str());
  std::printf("\n");
}

bool writeBytes(const fs::path& path, const std::string& text) {
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  if (!output) return false;
  output.write(text.data(), static_cast<std::streamsize>(text.size()));
  return output.good();
}

bool readBytes(const fs::path& path, std::string& out) {
  std::ifstream input(path, std::ios::binary);
  if (!input) return false;
  out.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
  return true;
}

std::string makeDemoBytes(char seed, std::size_t size) {
  std::string out;
  out.reserve(size);
  unsigned state = static_cast<unsigned>(seed) * 2654435761u + 12345u;
  for (std::size_t i = 0; i < size; ++i) {
    state = state * 1664525u + 1013904223u;
    out.push_back(static_cast<char>((state >> 16) & 0xFF));
  }
  return out;
}

WorkspaceCameraRef makeCamera(float x, float y, float z, float yaw, float pitch, float distance,
                              float fov, const std::string& mode, std::int32_t tick) {
  WorkspaceCameraRef camera;
  camera.valid = true;
  camera.pose.valid = true;
  camera.pose.focusX = x;
  camera.pose.focusY = y;
  camera.pose.focusZ = z;
  camera.pose.yaw = yaw;
  camera.pose.pitch = pitch;
  camera.pose.distance = distance;
  camera.fov = fov;
  camera.mode = mode;
  camera.cameraTick = tick;
  camera.bookmarkSlot = 3;
  camera.cameraReason.clear();
  return camera;
}

WorkspaceCameraRef unavailableCamera() {
  WorkspaceCameraRef camera;
  camera.valid = false;
  camera.cameraReason = "no-renderer";
  return camera;
}

struct FakePorts : ReviewJumpPorts {
  bool seekOk = true;
  bool applyOk = true;
  int seekCalls = 0;
  int applyCalls = 0;
  int restoreCalls = 0;
  std::int32_t lastSeek = -1;
  WorkspaceCameraRef lastCamera;

  bool seek(std::int32_t tick) override {
    ++seekCalls;
    lastSeek = tick;
    return seekOk;
  }
  bool applyCamera(const WorkspaceCameraRef& camera) override {
    ++applyCalls;
    lastCamera = camera;
    return applyOk;
  }
  void restoreCamera() override { ++restoreCalls; }
};

WorkspaceDocument makePopulatedDocument(const fs::path& demoPath, const std::string& hash) {
  WorkspaceDocument doc;
  doc.name = "Bagel Review";
  doc.demoHashAlgo = kWorkspaceHashAlgo;
  doc.demoHash = hash;
  doc.demoPathHint = fs::absolute(demoPath).string();
  doc.updatedUtc = 1700000100;
  doc.nextBookmarkId = 1;
  doc.autosave = true;

  doc.camera.valid = true;
  doc.camera.pose.valid = true;
  doc.camera.pose.focusX = 1.5f;
  doc.camera.pose.focusY = -2.25f;
  doc.camera.pose.focusZ = 512.125f;
  doc.camera.pose.yaw = 45.5f;
  doc.camera.pose.pitch = -12.25f;
  doc.camera.pose.distance = 320.0f;
  doc.camera.fov = 95.5f;
  doc.camera.mode = "third-person";
  doc.camera.cameraTick = 1234;
  doc.camera.targetKind = "player";
  doc.camera.targetEntindex = 7;
  doc.camera.targetName = "Soldier";
  doc.camera.targetReason.clear();
  doc.camera.cameraReason.clear();
  for (int i = 0; i < 8; ++i) {
    WorkspaceCameraSlot slot;
    slot.slot = i;
    slot.saved = (i % 2) == 0;
    slot.name = "Slot " + std::to_string(i);
    slot.pose.valid = slot.saved;
    slot.pose.focusX = 10.0f + static_cast<float>(i);
    slot.pose.focusY = 20.0f + static_cast<float>(i) * 0.5f;
    slot.pose.focusZ = 30.0f - static_cast<float>(i);
    slot.pose.yaw = 90.0f + static_cast<float>(i);
    slot.pose.pitch = -5.0f - static_cast<float>(i);
    slot.pose.distance = 250.0f + static_cast<float>(i) * 10.0f;
    slot.fov = 90.0f + static_cast<float>(i);
    slot.mode = "first-person";
    slot.tick = 100 + i;
    doc.camera.slots.push_back(slot);
  }

  doc.hud.enabled = true;
  doc.hud.scalePercent = 125;
  doc.hud.root = "hud/root";
  doc.hud.layout = "compact";
  doc.hud.panels.push_back({"health", true, 10.0f, 20.0f, 300.0f, 40.0f});
  doc.hud.panels.push_back({"chat", false, 10.0f, 700.0f, 500.0f, 200.0f});
  doc.hud.panels.push_back({"minimap", true, 1500.0f, 40.0f, 256.0f, 256.0f});

  doc.minimap.collapsed = false;
  doc.minimap.zoom = 1.5f;
  doc.minimap.panX = -12.5f;
  doc.minimap.panY = 8.25f;
  doc.minimap.mapName = "koth_bagel";
  doc.minimap.available = true;
  doc.minimap.unavailableReason.clear();

  doc.eventFilter.text = "soldier";
  doc.eventFilter.eventName = "player_death";
  doc.eventFilter.player = "userid:7";
  doc.logsTfUrl = "https://logs.tf/123456";
  doc.playback.restorable = true;
  doc.playback.tick = 987;
  doc.playback.speed = 0.5;
  doc.playback.paused = true;
  doc.playback.reverse = false;
  doc.playback.reason.clear();

  doc.extraKeys.push_back({"workspace", "futureFlag", "on"});
  WorkspaceExtraSection extra;
  extra.name = "workspace.future";
  extra.keys.push_back({"alpha", "1"});
  extra.keys.push_back({"note", "kept raw"});
  doc.extraSections.push_back(std::move(extra));
  return doc;
}

void compareDocuments(const WorkspaceDocument& a, const WorkspaceDocument& b) {
  fieldI("schema", a.schemaVersion, b.schemaVersion);
  fieldS("name", a.name, b.name);
  fieldS("demoHashAlgo", a.demoHashAlgo, b.demoHashAlgo);
  fieldS("demoHash", a.demoHash, b.demoHash);
  fieldS("demoPathHint", a.demoPathHint, b.demoPathHint);
  fieldI("updatedUtc", a.updatedUtc, b.updatedUtc);
  fieldI("nextBookmarkId", a.nextBookmarkId, b.nextBookmarkId);
  fieldB("autosave", a.autosave, b.autosave);

  fieldB("camera.valid", a.camera.valid, b.camera.valid);
  fieldF32("camera.focusX", a.camera.pose.focusX, b.camera.pose.focusX);
  fieldF32("camera.focusY", a.camera.pose.focusY, b.camera.pose.focusY);
  fieldF32("camera.focusZ", a.camera.pose.focusZ, b.camera.pose.focusZ);
  fieldF32("camera.yaw", a.camera.pose.yaw, b.camera.pose.yaw);
  fieldF32("camera.pitch", a.camera.pose.pitch, b.camera.pose.pitch);
  fieldF32("camera.distance", a.camera.pose.distance, b.camera.pose.distance);
  fieldF32("camera.fov", a.camera.fov, b.camera.fov);
  fieldS("camera.mode", a.camera.mode, b.camera.mode);
  fieldI("camera.cameraTick", a.camera.cameraTick, b.camera.cameraTick);
  fieldS("camera.targetKind", a.camera.targetKind, b.camera.targetKind);
  fieldI("camera.targetEntindex", a.camera.targetEntindex, b.camera.targetEntindex);
  fieldS("camera.targetName", a.camera.targetName, b.camera.targetName);
  fieldS("camera.targetReason", a.camera.targetReason, b.camera.targetReason);
  fieldS("camera.cameraReason", a.camera.cameraReason, b.camera.cameraReason);
  fieldI("camera.slotCount", static_cast<long long>(a.camera.slots.size()),
         static_cast<long long>(b.camera.slots.size()));
  for (std::size_t i = 0; i < a.camera.slots.size() && i < b.camera.slots.size(); ++i) {
    const std::string prefix = "camera.slot." + std::to_string(i) + ".";
    const auto& sa = a.camera.slots[i];
    const auto& sb = b.camera.slots[i];
    fieldI(prefix + "slot", sa.slot, sb.slot);
    fieldB(prefix + "saved", sa.saved, sb.saved);
    fieldS(prefix + "name", sa.name, sb.name);
    fieldF32(prefix + "focusX", sa.pose.focusX, sb.pose.focusX);
    fieldF32(prefix + "focusY", sa.pose.focusY, sb.pose.focusY);
    fieldF32(prefix + "focusZ", sa.pose.focusZ, sb.pose.focusZ);
    fieldF32(prefix + "yaw", sa.pose.yaw, sb.pose.yaw);
    fieldF32(prefix + "pitch", sa.pose.pitch, sb.pose.pitch);
    fieldF32(prefix + "distance", sa.pose.distance, sb.pose.distance);
    fieldF32(prefix + "fov", sa.fov, sb.fov);
    fieldS(prefix + "mode", sa.mode, sb.mode);
    fieldI(prefix + "tick", sa.tick, sb.tick);
  }

  fieldB("hud.enabled", a.hud.enabled, b.hud.enabled);
  fieldI("hud.scale", a.hud.scalePercent, b.hud.scalePercent);
  fieldS("hud.root", a.hud.root, b.hud.root);
  fieldS("hud.layout", a.hud.layout, b.hud.layout);
  fieldI("hud.panelCount", static_cast<long long>(a.hud.panels.size()),
         static_cast<long long>(b.hud.panels.size()));
  for (std::size_t i = 0; i < a.hud.panels.size() && i < b.hud.panels.size(); ++i) {
    const std::string prefix = "hud.panel." + std::to_string(i) + ".";
    fieldS(prefix + "id", a.hud.panels[i].id, b.hud.panels[i].id);
    fieldB(prefix + "visible", a.hud.panels[i].visible, b.hud.panels[i].visible);
    fieldF32(prefix + "x", a.hud.panels[i].x, b.hud.panels[i].x);
    fieldF32(prefix + "y", a.hud.panels[i].y, b.hud.panels[i].y);
    fieldF32(prefix + "w", a.hud.panels[i].width, b.hud.panels[i].width);
    fieldF32(prefix + "h", a.hud.panels[i].height, b.hud.panels[i].height);
  }

  fieldB("minimap.collapsed", a.minimap.collapsed, b.minimap.collapsed);
  fieldF32("minimap.zoom", a.minimap.zoom, b.minimap.zoom);
  fieldF32("minimap.panX", a.minimap.panX, b.minimap.panX);
  fieldF32("minimap.panY", a.minimap.panY, b.minimap.panY);
  fieldS("minimap.mapName", a.minimap.mapName, b.minimap.mapName);
  fieldB("minimap.available", a.minimap.available, b.minimap.available);
  fieldS("minimap.reason", a.minimap.unavailableReason, b.minimap.unavailableReason);

  fieldS("eventFilter.text", a.eventFilter.text, b.eventFilter.text);
  fieldS("eventFilter.eventName", a.eventFilter.eventName, b.eventFilter.eventName);
  fieldS("eventFilter.player", a.eventFilter.player, b.eventFilter.player);
  fieldS("logs.tfUrl", a.logsTfUrl, b.logsTfUrl);

  fieldB("playback.restorable", a.playback.restorable, b.playback.restorable);
  fieldI("playback.tick", a.playback.tick, b.playback.tick);
  fieldF64("playback.speed", a.playback.speed, b.playback.speed);
  fieldB("playback.paused", a.playback.paused, b.playback.paused);
  fieldB("playback.reverse", a.playback.reverse, b.playback.reverse);
  fieldS("playback.reason", a.playback.reason, b.playback.reason);

  fieldI("extraKeyCount", static_cast<long long>(a.extraKeys.size()),
         static_cast<long long>(b.extraKeys.size()));
  for (std::size_t i = 0; i < a.extraKeys.size() && i < b.extraKeys.size(); ++i) {
    const std::string prefix = "extraKey." + std::to_string(i) + ".";
    fieldS(prefix + "section", a.extraKeys[i].section, b.extraKeys[i].section);
    fieldS(prefix + "key", a.extraKeys[i].key, b.extraKeys[i].key);
    fieldS(prefix + "value", a.extraKeys[i].value, b.extraKeys[i].value);
  }
  fieldI("extraSectionCount", static_cast<long long>(a.extraSections.size()),
         static_cast<long long>(b.extraSections.size()));
  for (std::size_t i = 0; i < a.extraSections.size() && i < b.extraSections.size(); ++i) {
    fieldS("extraSection." + std::to_string(i) + ".name", a.extraSections[i].name,
           b.extraSections[i].name);
    fieldI("extraSection." + std::to_string(i) + ".keyCount",
           static_cast<long long>(a.extraSections[i].keys.size()),
           static_cast<long long>(b.extraSections[i].keys.size()));
    for (std::size_t k = 0; k < a.extraSections[i].keys.size() && k < b.extraSections[i].keys.size(); ++k) {
      fieldS("extraSection." + std::to_string(i) + ".key." + std::to_string(k),
             a.extraSections[i].keys[k].first + "=" + a.extraSections[i].keys[k].second,
             b.extraSections[i].keys[k].first + "=" + b.extraSections[i].keys[k].second);
    }
  }
}

std::vector<std::uint64_t> idsOf(const std::vector<WorkspaceBookmarkRecord>& rows) {
  std::vector<std::uint64_t> ids;
  for (const auto& row : rows) ids.push_back(row.id);
  return ids;
}

std::string joinIds(const std::vector<std::uint64_t>& ids) {
  std::string out;
  for (std::size_t i = 0; i < ids.size(); ++i) {
    if (i != 0) out += ',';
    out += std::to_string(ids[i]);
  }
  return out;
}

std::string joinEventKeys(const std::vector<WorkspaceBookmarkRecord>& rows) {
  std::string out;
  for (std::size_t i = 0; i < rows.size(); ++i) {
    if (i != 0) out += ',';
    out += ReviewBookmarkStore::eventKey(rows[i]);
  }
  return out;
}

bool ticksAscending(const std::vector<WorkspaceBookmarkRecord>& rows) {
  for (std::size_t i = 1; i < rows.size(); ++i) {
    if (rows[i].tick < rows[i - 1].tick) return false;
  }
  return true;
}

int run(const fs::path& scratch) {
  std::error_code error;
  const fs::path root = scratch / ("run-" + std::to_string(GetCurrentProcessId()));
  fs::remove_all(root, error);
  fs::create_directories(root, error);
  if (error) {
    std::printf("review-workspace: probe done ok=0 reason=mkdir\n");
    return 2;
  }

  const fs::path demoA = root / "demoA.dem";
  const fs::path demoB = root / "demoB.dem";
  if (!writeBytes(demoA, makeDemoBytes('A', 4096)) || !writeBytes(demoB, makeDemoBytes('B', 4096))) {
    std::printf("review-workspace: probe done ok=0 reason=demo-write\n");
    return 2;
  }
  std::string hashError;
  const std::string hashA = installer::sha256File(demoA, &hashError);
  const std::string hashB = installer::sha256File(demoB, &hashError);
  if (hashA.size() != 64 || hashB.size() != 64 || hashA == hashB) {
    std::printf("review-workspace: probe done ok=0 reason=demo-hash\n");
    return 2;
  }
  std::printf("review-workspace: demo hashA=%s hashB=%s\n", hashA.c_str(), hashB.c_str());

  const fs::path wsPath = root / "workspace-a.ini";

  // ---- create + populate + save -------------------------------------------
  WorkspaceStore store;
  store.setExplicitPath(wsPath);
  std::string reason;
  const WorkspaceNewStatus created = store.create(demoA, hashA, reason);
  step("new", workspaceNewStatusName(created), "path=" + percent(wsPath.string()));
  check("new_created", created == WorkspaceNewStatus::Created, reason);
  check("new_file_exists", fs::exists(wsPath));

  WorkspaceDocument expected = makePopulatedDocument(demoA, hashA);
  expected.name = "Bagel Review";
  store.document() = expected;
  const WorkspaceSaveStatus saved = store.save(store.document(), reason);
  step("save", workspaceSaveStatusName(saved), "count=0");
  check("save_ok", saved == WorkspaceSaveStatus::Saved, reason);

  std::string bytes1;
  check("save_bytes_readable", readBytes(wsPath, bytes1));

  // ---- load + field-by-field roundtrip -----------------------------------
  WorkspaceStore reloaded;
  reloaded.setExplicitPath(wsPath);
  const WorkspaceOpenResult opened = reloaded.open(demoA, hashA);
  step("load", workspaceOpenStateName(opened.state),
       "loaded=" + std::to_string(opened.loaded ? 1 : 0) + " storedHash=" + opened.storedHash);
  check("load_opened", opened.state == WorkspaceOpenState::Opened && opened.loaded, opened.reason);
  compareDocuments(expected, reloaded.document());
  check("roundtrip_fields", g_fieldMismatches == 0,
        "fields=" + std::to_string(g_fields) + " mismatches=" + std::to_string(g_fieldMismatches));

  const WorkspaceSaveStatus resaved = reloaded.save(reloaded.document(), reason);
  std::string bytes2;
  check("resave_ok", resaved == WorkspaceSaveStatus::Saved, reason);
  check("resave_bytes_readable", readBytes(wsPath, bytes2));
  check("resave_bytes_equal", bytes1 == bytes2);
  step("resave", bytes1 == bytes2 ? "ok" : "mismatch", "bytes=" + std::to_string(bytes1.size()));

  // ---- moved demo path ---------------------------------------------------
  const fs::path movedDir = root / "moved";
  fs::create_directories(movedDir, error);
  const fs::path demoMoved = movedDir / "demoA-renamed.dem";
  check("moved_copy", fs::copy_file(demoA, demoMoved, fs::copy_options::overwrite_existing, error));
  WorkspaceStore movedStore;
  movedStore.setExplicitPath(wsPath);
  const WorkspaceOpenResult moved = movedStore.open(demoMoved, hashA);
  step("load-moved", workspaceOpenStateName(moved.state),
       "moved=" + std::to_string(moved.movedPath ? 1 : 0));
  check("moved_still_opens", moved.state == WorkspaceOpenState::Opened && moved.loaded);
  check("moved_path_named", moved.movedPath);

  // ---- hash mismatch -----------------------------------------------------
  WorkspaceStore mismatchStore;
  mismatchStore.setExplicitPath(wsPath);
  const WorkspaceOpenResult mismatch = mismatchStore.open(demoA, hashB);
  step("load-hash-mismatch", workspaceOpenStateName(mismatch.state),
       "storedHash=" + mismatch.storedHash + " actualHash=" + mismatch.actualHash);
  check("hash_mismatch_named", mismatch.state == WorkspaceOpenState::HashMismatch && !mismatch.loaded,
        mismatch.reason);
  std::string mismatchBytesBefore;
  check("hash_mismatch_bytes_before", readBytes(wsPath, mismatchBytesBefore));
  WorkspaceDocument mismatchDoc = expected;
  const WorkspaceSaveStatus mismatchSave = mismatchStore.save(mismatchDoc, reason);
  std::string mismatchBytesAfter;
  check("hash_mismatch_save_refused", mismatchSave == WorkspaceSaveStatus::RefusedNotLoaded, reason);
  check("hash_mismatch_bytes_after", readBytes(wsPath, mismatchBytesAfter));
  check("hash_mismatch_file_intact", mismatchBytesBefore == mismatchBytesAfter);

  // ---- corrupt file ------------------------------------------------------
  const fs::path corruptPath = root / "corrupt.ini";
  check("corrupt_written", writeBytes(corruptPath, "[workspace]\nschema=1\ndemoHash=zz\n"));
  WorkspaceStore corruptStore;
  corruptStore.setExplicitPath(corruptPath);
  const WorkspaceOpenResult corrupt = corruptStore.open(demoA, hashA);
  step("load-corrupt", workspaceOpenStateName(corrupt.state), "reason=" + percent(corrupt.reason));
  check("corrupt_named", corrupt.state == WorkspaceOpenState::Corrupt && !corrupt.loaded, corrupt.reason);
  std::string corruptBefore;
  check("corrupt_bytes_before", readBytes(corruptPath, corruptBefore));
  WorkspaceDocument corruptDoc = expected;
  const WorkspaceSaveStatus corruptSave = corruptStore.save(corruptDoc, reason);
  std::string corruptAfter;
  check("corrupt_save_refused", corruptSave == WorkspaceSaveStatus::RefusedNotLoaded, reason);
  check("corrupt_bytes_after", readBytes(corruptPath, corruptAfter));
  check("corrupt_file_intact", corruptBefore == corruptAfter);

  // ---- old schema + migrate ---------------------------------------------
  // The migration fixture carries one bookmark record so that "migrated" must
  // rewrite the record hash and recordVersion as well as the header.
  WorkspaceDocument recordDoc = expected;
  {
    WorkspaceBookmarkRecord seed;
    seed.id = 1;
    seed.tick = 100;
    seed.title = "Seed";
    seed.demoHash = hashA;
    seed.identityReason = "no-player-context";
    seed.camera = unavailableCamera();
    seed.createdAtUtc = seed.updatedAtUtc = 1700000100;
    recordDoc.bookmarks.push_back(seed);
    recordDoc.nextBookmarkId = 2;
  }
  std::string oldText = WorkspaceStore::serialize(recordDoc);
  auto replaceOnce = [](std::string& text, const std::string& from, const std::string& to) {
    const std::size_t pos = text.find(from);
    if (pos == std::string::npos) return false;
    text.replace(pos, from.size(), to);
    return true;
  };
  bool patched = replaceOnce(oldText, "version=1\n", "version=0\n");
  patched = replaceOnce(oldText, "schema=1\n", "schema=0\n") && patched;
  patched = replaceOnce(oldText, "recordVersion=1\n", "recordVersion=0\n") && patched;
  const fs::path oldPath = root / "old-schema.ini";
  check("old_schema_written", patched && writeBytes(oldPath, oldText));
  WorkspaceStore oldStore;
  oldStore.setExplicitPath(oldPath);
  const WorkspaceOpenResult oldOpen = oldStore.open(demoA, hashA);
  step("load-old-schema", workspaceOpenStateName(oldOpen.state), "reason=" + percent(oldOpen.reason));
  check("old_schema_named", oldOpen.state == WorkspaceOpenState::OldSchema && !oldOpen.loaded,
        oldOpen.reason);
  const bool migrated = oldStore.migrate(hashA, reason);
  step("migrate", migrated ? "ok" : "refused", "reason=" + percent(reason));
  check("migrate_ok", migrated, reason);
  WorkspaceStore migratedStore;
  migratedStore.setExplicitPath(oldPath);
  const WorkspaceOpenResult migratedOpen = migratedStore.open(demoA, hashA);
  check("migrate_reopens", migratedOpen.state == WorkspaceOpenState::Opened && migratedOpen.loaded,
        migratedOpen.reason);
  std::string migratedText;
  check("migrated_bytes", readBytes(oldPath, migratedText));
  check("migrate_header_rewritten", migratedText.find("version=1\n") != std::string::npos &&
                                        migratedText.find("schema=1\n") != std::string::npos);
  check("migrate_records_rewritten",
        migratedText.find("recordVersion=1\n") != std::string::npos &&
            migratedText.find("demoHash=" + hashA) != std::string::npos);

  // Header-only migration is not a migration: a v1 header with an old record
  // hash must be refused as corrupt.
  std::string headerOnly = WorkspaceStore::serialize(recordDoc);
  const std::string recordHash = "demoHash=" + hashA;
  const std::size_t lastRecordHash = headerOnly.rfind(recordHash);
  check("header_only_patched", lastRecordHash != std::string::npos);
  if (lastRecordHash != std::string::npos) {
    headerOnly.replace(lastRecordHash, recordHash.size(), "demoHash=" + std::string(64, 'a'));
  }
  const fs::path headerOnlyPath = root / "header-only.ini";
  check("header_only_written", writeBytes(headerOnlyPath, headerOnly));
  WorkspaceStore headerOnlyStore;
  headerOnlyStore.setExplicitPath(headerOnlyPath);
  const WorkspaceOpenResult headerOnlyOpen = headerOnlyStore.open(demoA, hashA);
  step("load-header-only", workspaceOpenStateName(headerOnlyOpen.state),
       "reason=" + percent(headerOnlyOpen.reason));
  check("header_only_corrupt", headerOnlyOpen.state == WorkspaceOpenState::Corrupt, headerOnlyOpen.reason);

  // ---- unknown future version -------------------------------------------
  std::string futureText = bytes1;
  replaceOnce(futureText, "version=1\n", "version=99\n");
  replaceOnce(futureText, "schema=1\n", "schema=99\n");
  const fs::path futurePath = root / "future.ini";
  check("future_written", writeBytes(futurePath, futureText));
  WorkspaceStore futureStore;
  futureStore.setExplicitPath(futurePath);
  const WorkspaceOpenResult futureOpen = futureStore.open(demoA, hashA);
  step("load-unknown-version", workspaceOpenStateName(futureOpen.state),
       "reason=" + percent(futureOpen.reason));
  check("unknown_version_named", futureOpen.state == WorkspaceOpenState::OldSchema, futureOpen.reason);
  const bool futureMigrated = futureStore.migrate(hashA, reason);
  step("migrate-unknown", futureMigrated ? "ok" : "refused", "reason=" + percent(reason));
  check("unknown_version_migrate_refused", !futureMigrated && reason == "unsupported-version", reason);

  // ---- bookmarks ---------------------------------------------------------
  WorkspaceStore ws;
  ws.setExplicitPath(wsPath);
  check("bookmarks_reopen", ws.open(demoA, hashA).state == WorkspaceOpenState::Opened);
  ReviewBookmarkStore bookmarks;
  bookmarks.setDemo(hashA, 1000);
  bookmarks.setCursorTick(100);
  bookmarks.setNowUtc(1700000200);
  std::string bookmarkReason;
  const ReviewLoadState bookmarkLoaded = bookmarks.load(ws.document(), bookmarkReason);
  step("bookmark-load", reviewLoadStateName(bookmarkLoaded), "count=0");
  check("bookmark_load_loaded", bookmarkLoaded == ReviewLoadState::Loaded, bookmarkReason);

  std::uint64_t id = 0;
  const WorkspaceCameraRef camera1 = makeCamera(1.0f, 2.0f, 3.0f, 10.0f, -20.0f, 300.0f, 90.0f,
                                                "first-person", 100);
  ReviewAddStatus add = bookmarks.addManual("Alpha", "first note", camera1, id, bookmarkReason);
  step("bookmark-add", reviewAddStatusName(add), "key=none tick=100 id=" + std::to_string(id));
  check("bookmark_add_manual", add == ReviewAddStatus::Added && id == 1 && bookmarks.count() == 1,
        bookmarkReason);

  bookmarks.setCursorTick(50);
  add = bookmarks.addManual("Zulu", "inserted later, earlier tick", unavailableCamera(), id,
                            bookmarkReason);
  step("bookmark-add", reviewAddStatusName(add), "key=none tick=50 id=" + std::to_string(id));
  check("bookmark_add_second", add == ReviewAddStatus::Added && id == 2, bookmarkReason);

  ReviewEventInput death;
  death.name = "player_death";
  death.eventId = 5;
  death.tick = 300;
  death.packetOrdinal = 2;
  death.messageBit = 5;
  death.players.push_back({"player", 2, 7, "Soldier", ""});
  death.players.push_back({"player", 3, 8, "Medic", ""});
  add = bookmarks.addEvent(death, "Death at 300", "first event on this tick", camera1, id,
                           bookmarkReason);
  step("bookmark-add-event", reviewAddStatusName(add),
       "key=300:2:5 tick=300 id=" + std::to_string(id) + " eventId=5");
  check("bookmark_add_event", add == ReviewAddStatus::Added && id == 3, bookmarkReason);

  ReviewEventInput hurt = death;
  hurt.name = "player_hurt";
  hurt.eventId = 6;
  hurt.messageBit = 9;
  add = bookmarks.addEvent(hurt, "Hurt at 300", "second event on this tick", unavailableCamera(), id,
                           bookmarkReason);
  step("bookmark-add-event", reviewAddStatusName(add),
       "key=300:2:9 tick=300 id=" + std::to_string(id) + " eventId=6");
  check("bookmark_add_event_same_tick", add == ReviewAddStatus::Added && id == 4, bookmarkReason);

  ReviewEventInput death2 = death;
  death2.packetOrdinal = 3;
  death2.messageBit = 5;
  death2.players.clear();
  add = bookmarks.addEvent(death2, "Third at 300", "no players in event", unavailableCamera(), id,
                           bookmarkReason);
  step("bookmark-add-event", reviewAddStatusName(add),
       "key=300:3:5 tick=300 id=" + std::to_string(id) + " eventId=5");
  check("bookmark_add_event_third", add == ReviewAddStatus::Added && id == 5, bookmarkReason);

  add = bookmarks.addEvent(death, "duplicate", "", camera1, id, bookmarkReason);
  step("bookmark-add-event", reviewAddStatusName(add),
       "key=300:2:5 existing=" + std::to_string(id));
  check("duplicate_event_already_exists",
        add == ReviewAddStatus::AlreadyExists && id == 3 && bookmarks.counters().duplicateEvent == 1,
        bookmarkReason);

  add = bookmarks.addManual("   ", "empty", camera1, id, bookmarkReason);
  step("bookmark-add", reviewAddStatusName(add), "key=none");
  check("empty_title_refused", add == ReviewAddStatus::RefusedEmptyTitle, bookmarkReason);

  bookmarks.setSeekPending(true);
  add = bookmarks.addManual("Seeking", "", camera1, id, bookmarkReason);
  step("bookmark-add", reviewAddStatusName(add), "key=none");
  check("seek_pending_refused", add == ReviewAddStatus::RefusedSeekPending, bookmarkReason);
  bookmarks.setSeekPending(false);

  ReviewBookmarkStore noDemo;
  noDemo.setNowUtc(1700000200);
  add = noDemo.addManual("No demo", "", camera1, id, bookmarkReason);
  check("no_demo_refused", add == ReviewAddStatus::RefusedNoDemo, bookmarkReason);

  ReviewBookmarkStore notLoaded;
  notLoaded.setDemo(hashA, 1000);
  add = notLoaded.addManual("Not loaded", "", camera1, id, bookmarkReason);
  check("not_loaded_refused", add == ReviewAddStatus::RefusedNotLoaded, bookmarkReason);

  // Sort: ascending tick, same tick by packet then bit.
  const auto tickSorted = bookmarks.sorted(ReviewSort::Tick);
  const std::string tickIds = joinIds(idsOf(tickSorted));
  step("list", "ok", "order=tick ids=" + tickIds);
  check("sort_ascending", ticksAscending(tickSorted) && tickIds == "2,1,3,4,5", tickIds);
  check("same_tick_keys_differ", ReviewBookmarkStore::eventKey(tickSorted[2]) == "300:2:5" &&
                                     ReviewBookmarkStore::eventKey(tickSorted[3]) == "300:2:9" &&
                                     ReviewBookmarkStore::eventKey(tickSorted[4]) == "300:3:5",
        joinEventKeys(tickSorted));

  // Search / filter.
  ReviewQuery query;
  query.text = "alpha";
  check("query_text", joinIds(idsOf(bookmarks.query(query, ReviewSort::Tick))) == "1");
  query = ReviewQuery{};
  query.eventName = "player_hurt";
  check("filter_event", joinIds(idsOf(bookmarks.query(query, ReviewSort::Tick))) == "4");
  query = ReviewQuery{};
  query.player = "userid:7";
  check("filter_player_userid", joinIds(idsOf(bookmarks.query(query, ReviewSort::Tick))) == "3,4");
  query = ReviewQuery{};
  query.player = "entindex:3";
  check("filter_player_entindex", joinIds(idsOf(bookmarks.query(query, ReviewSort::Tick))) == "3,4");
  query = ReviewQuery{};
  query.tickMin = 200;
  query.tickMax = 400;
  check("filter_tick_range", joinIds(idsOf(bookmarks.query(query, ReviewSort::Tick))) == "3,4,5");
  query = ReviewQuery{};
  query.eventName = "manual";
  check("filter_manual", joinIds(idsOf(bookmarks.query(query, ReviewSort::Tick))) == "2,1");

  // Update: title/note only; tick and createdAt immutable.
  const WorkspaceBookmarkRecord beforeUpdate = *bookmarks.find(1);
  const ReviewUpdateStatus updated = bookmarks.update(1, "Alpha edited", "second note", bookmarkReason);
  const WorkspaceBookmarkRecord* afterUpdate = bookmarks.find(1);
  step("bookmark-update", reviewUpdateStatusName(updated), "id=1 tickUnchanged=1");
  check("update_ok", updated == ReviewUpdateStatus::Updated && afterUpdate != nullptr &&
                         afterUpdate->title == "Alpha edited" && afterUpdate->note == "second note");
  check("update_immutable", afterUpdate != nullptr && afterUpdate->tick == beforeUpdate.tick &&
                                afterUpdate->createdAtUtc == beforeUpdate.createdAtUtc &&
                                afterUpdate->demoHash == beforeUpdate.demoHash);
  check("update_empty_title_refused",
        bookmarks.update(1, "  ", "x", bookmarkReason) == ReviewUpdateStatus::RefusedEmptyTitle &&
            bookmarks.find(1)->title == "Alpha edited");
  check("update_missing", bookmarks.update(999, "x", "", bookmarkReason) ==
                              ReviewUpdateStatus::NotFound);

  // Delete.
  const ReviewDeleteStatus removed = bookmarks.remove(2, bookmarkReason);
  step("bookmark-delete", reviewDeleteStatusName(removed), "id=2 count=4");
  check("delete_ok", removed == ReviewDeleteStatus::Removed && bookmarks.count() == 4);
  check("delete_missing", bookmarks.remove(2, bookmarkReason) == ReviewDeleteStatus::NotFound);

  // Jump.
  FakePorts ports;
  const ReviewJumpStatus jump1 = bookmarks.jump(3, ports, bookmarkReason);
  step("bookmark-jump", reviewJumpStatusName(jump1),
       "key=" + bookmarks.lastJumpEventKey() + " tick=" + std::to_string(bookmarks.lastJumpTick()));
  check("jump_applied", jump1 == ReviewJumpStatus::Applied && ports.lastSeek == 300 &&
                            bookmarks.lastJumpTick() == 300 && ports.applyCalls == 1);
  const std::string key1 = bookmarks.lastJumpEventKey();
  const ReviewJumpStatus jump2 = bookmarks.jump(4, ports, bookmarkReason);
  const std::string key2 = bookmarks.lastJumpEventKey();
  const ReviewJumpStatus jump3 = bookmarks.jump(5, ports, bookmarkReason);
  const std::string key3 = bookmarks.lastJumpEventKey();
  step("bookmark-jump", reviewJumpStatusName(jump3),
       "key=" + key3 + " tick=" + std::to_string(bookmarks.lastJumpTick()));
  check("same_tick_jump_separate",
        jump2 == ReviewJumpStatus::Applied && jump3 == ReviewJumpStatus::Applied && key1 != key2 &&
            key2 != key3 && key1 == "300:2:5" && key2 == "300:2:9" && key3 == "300:3:5",
        key1 + "|" + key2 + "|" + key3);

  // Jump out of range: cursor and camera must not move.
  WorkspaceDocument rangeDoc = expected;
  WorkspaceBookmarkRecord farRecord;
  farRecord.id = 1;
  farRecord.tick = 5000;
  farRecord.title = "Far";
  farRecord.demoHash = hashA;
  farRecord.identityReason = "no-player-context";
  farRecord.camera = unavailableCamera();
  farRecord.createdAtUtc = farRecord.updatedAtUtc = 1700000200;
  rangeDoc.bookmarks.push_back(farRecord);
  rangeDoc.nextBookmarkId = 2;
  ReviewBookmarkStore rangeStore;
  rangeStore.setDemo(hashA, 1000);
  rangeStore.setCursorTick(10);
  check("range_load", rangeStore.load(rangeDoc, bookmarkReason) == ReviewLoadState::Loaded);
  FakePorts rangePorts;
  const ReviewJumpStatus rangeJump = rangeStore.jump(1, rangePorts, bookmarkReason);
  step("bookmark-jump", reviewJumpStatusName(rangeJump), "key=none tick=5000");
  check("jump_out_of_range_keeps", rangeJump == ReviewJumpStatus::RefusedOutOfRange &&
                                       rangePorts.seekCalls == 0 && rangePorts.applyCalls == 0 &&
                                       rangeStore.lastJumpTick() == -1,
        bookmarkReason);

  // Playback refusal rolls the applied camera back.
  FakePorts refusalPorts;
  refusalPorts.seekOk = false;
  const ReviewJumpStatus playbackJump = bookmarks.jump(3, refusalPorts, bookmarkReason);
  step("bookmark-jump", reviewJumpStatusName(playbackJump), "key=" + ReviewBookmarkStore::eventKey(*bookmarks.find(3)) + " tick=300 rollback=1");
  check("jump_playback_rollback", playbackJump == ReviewJumpStatus::RefusedPlayback &&
                                      refusalPorts.applyCalls == 1 && refusalPorts.restoreCalls == 1 &&
                                      refusalPorts.seekCalls == 1,
        bookmarkReason);

  // Camera refusal: the camera is restored, the cursor never moves.
  FakePorts cameraPorts;
  cameraPorts.applyOk = false;
  const ReviewJumpStatus cameraJump = bookmarks.jump(3, cameraPorts, bookmarkReason);
  check("jump_camera_refused", cameraJump == ReviewJumpStatus::RefusedCamera &&
                                   cameraPorts.applyCalls == 1 && cameraPorts.restoreCalls == 1 &&
                                   cameraPorts.seekCalls == 0,
        bookmarkReason);

  // Jump refusals from bad states.
  check("jump_missing", bookmarks.jump(999, ports, bookmarkReason) == ReviewJumpStatus::RefusedNotFound);
  ReviewBookmarkStore hashMismatch;
  hashMismatch.setDemo(hashA, 1000);
  hashMismatch.noteOpenState(ReviewLoadState::HashMismatch, "demo-hash-mismatch");
  check("jump_hash_mismatch",
        hashMismatch.jump(1, ports, bookmarkReason) == ReviewJumpStatus::RefusedHashMismatch);
  ReviewBookmarkStore neverLoaded;
  neverLoaded.setDemo(hashA, 1000);
  check("jump_not_loaded",
        neverLoaded.jump(1, ports, bookmarkReason) == ReviewJumpStatus::RefusedNotLoaded);

  // Persist bookmarks through the workspace and reopen.
  check("bookmark_save_to_doc", bookmarks.save(ws.document(), bookmarkReason), bookmarkReason);
  ws.document().updatedUtc = 1700000300;
  const WorkspaceSaveStatus bookmarkSaved = ws.save(ws.document(), reason);
  step("bookmark-save", workspaceSaveStatusName(bookmarkSaved), "count=4");
  check("bookmark_save_ok", bookmarkSaved == WorkspaceSaveStatus::Saved, reason);

  WorkspaceStore reopened;
  reopened.setExplicitPath(wsPath);
  const WorkspaceOpenResult reopenedOpen = reopened.open(demoA, hashA);
  check("bookmark_reopen", reopenedOpen.state == WorkspaceOpenState::Opened && reopenedOpen.loaded,
        reopenedOpen.reason);
  ReviewBookmarkStore restoredBookmarks;
  restoredBookmarks.setDemo(hashA, 1000);
  restoredBookmarks.setNowUtc(1700000400);
  const ReviewLoadState restoredState = restoredBookmarks.load(reopened.document(), bookmarkReason);
  check("bookmarks_persist",
        restoredState == ReviewLoadState::Loaded && restoredBookmarks.count() == 4 &&
            joinIds(idsOf(restoredBookmarks.sorted(ReviewSort::Tick))) == "1,3,4,5",
        bookmarkReason);
  check("bookmarks_persist_eventkeys",
        joinEventKeys(restoredBookmarks.sorted(ReviewSort::Tick)) == "none,300:2:5,300:2:9,300:3:5",
        joinEventKeys(restoredBookmarks.sorted(ReviewSort::Tick)));
  check("bookmarks_persist_note", restoredBookmarks.find(1) != nullptr &&
                                       restoredBookmarks.find(1)->note == "second note");
  FakePorts restoredPorts;
  check("bookmarks_jump_after_reload",
        restoredBookmarks.jump(4, restoredPorts, bookmarkReason) == ReviewJumpStatus::Applied &&
            restoredBookmarks.lastJumpTick() == 300);

  // No-identity bookmark survives as an explicitly named unavailable identity.
  const WorkspaceBookmarkRecord* third = restoredBookmarks.find(5);
  check("no_identity_unavailable", third != nullptr && third->players.empty() &&
                                       !third->identityReason.empty() &&
                                       third->identityReason == "no-player-context",
        third == nullptr ? "missing" : third->identityReason);
  check("no_identity_trace_named",
        third != nullptr && ReviewBookmarkStore::identityTrace(*third) != "available");

  // ---- autosave / rename / close ----------------------------------------
  WorkspaceStore lifecycle;
  lifecycle.setExplicitPath(wsPath);
  check("lifecycle_open", lifecycle.open(demoA, hashA).state == WorkspaceOpenState::Opened);
  check("autosave_clean", lifecycle.autosave(lifecycle.document(), false, reason) ==
                              WorkspaceAutosaveStatus::SkippedClean, reason);
  lifecycle.document().autosave = false;
  check("autosave_disabled", lifecycle.autosave(lifecycle.document(), true, reason) ==
                                 WorkspaceAutosaveStatus::SkippedDisabled, reason);
  lifecycle.document().autosave = true;
  check("autosave_saved", lifecycle.autosave(lifecycle.document(), true, reason) ==
                              WorkspaceAutosaveStatus::Saved, reason);

  check("rename_empty", lifecycle.rename(lifecycle.document(), "   ", reason) ==
                            WorkspaceRenameStatus::RefusedEmptyName, reason);
  check("rename_too_long", lifecycle.rename(lifecycle.document(), std::string(200, 'x'), reason) ==
                               WorkspaceRenameStatus::RefusedTooLong, reason);
  const WorkspaceRenameStatus renamed =
      lifecycle.rename(lifecycle.document(), "  My Workspace  ", reason);
  step("rename", workspaceRenameStatusName(renamed), "name=" + percent(lifecycle.document().name));
  check("rename_ok", renamed == WorkspaceRenameStatus::Renamed &&
                         lifecycle.document().name == "My Workspace", reason);
  check("rename_save", lifecycle.save(lifecycle.document(), reason) == WorkspaceSaveStatus::Saved,
        reason);

  check("close_unsaved", lifecycle.close(true) == WorkspaceCloseStatus::UnsavedExit);
  check("close_clean", lifecycle.close(false) == WorkspaceCloseStatus::Clean);

  // ---- negative controls -------------------------------------------------
  WorkspaceStore missingStore;
  missingStore.setExplicitPath(root / "does-not-exist.ini");
  const WorkspaceOpenResult missing = missingStore.open(demoA, hashA);
  step("load-missing", workspaceOpenStateName(missing.state), "reason=" + percent(missing.reason));
  check("missing_named", missing.state == WorkspaceOpenState::Missing, missing.reason);

  WorkspaceStore dirStore;
  dirStore.setExplicitPath(root);
  const WorkspaceOpenResult dirOpen = dirStore.open(demoA, hashA);
  step("load-wrong-path", workspaceOpenStateName(dirOpen.state), "reason=" + percent(dirOpen.reason));
  check("wrong_path_named", dirOpen.state == WorkspaceOpenState::Corrupt, dirOpen.reason);

  WorkspaceStore noHashStore;
  noHashStore.setExplicitPath(root / "nohash.ini");
  check("new_no_hash", noHashStore.create(demoA, "", reason) == WorkspaceNewStatus::NoHash, reason);
  WorkspaceStore existingStore;
  existingStore.setExplicitPath(wsPath);
  check("new_already_exists",
        existingStore.create(demoA, hashA, reason) == WorkspaceNewStatus::AlreadyExists, reason);

  WorkspaceStore unloadedStore;
  unloadedStore.setExplicitPath(root / "unloaded.ini");
  WorkspaceDocument unloadedDoc = expected;
  check("save_not_loaded", unloadedStore.save(unloadedDoc, reason) ==
                               WorkspaceSaveStatus::RefusedNotLoaded, reason);

  // Read-only target file: save must be refused by name and leave bytes alone.
  const fs::path readOnlyPath = root / "readonly.ini";
  check("readonly_seed", fs::copy_file(wsPath, readOnlyPath, fs::copy_options::overwrite_existing, error));
  check("readonly_attrib", SetFileAttributesW(readOnlyPath.c_str(), FILE_ATTRIBUTE_READONLY) != 0);
  WorkspaceStore readOnlyStore;
  readOnlyStore.setExplicitPath(readOnlyPath);
  const WorkspaceOpenResult readOnlyOpen = readOnlyStore.open(demoA, hashA);
  check("readonly_open", readOnlyOpen.state == WorkspaceOpenState::Opened, readOnlyOpen.reason);
  std::string readOnlyBefore;
  check("readonly_bytes_before", readBytes(readOnlyPath, readOnlyBefore));
  readOnlyStore.document().updatedUtc += 10;
  const WorkspaceSaveStatus readOnlySave = readOnlyStore.save(readOnlyStore.document(), reason);
  step("save-readonly", workspaceSaveStatusName(readOnlySave), "reason=" + percent(reason));
  std::string readOnlyAfter;
  check("readonly_bytes_after", readBytes(readOnlyPath, readOnlyAfter));
  check("readonly_refused", readOnlySave == WorkspaceSaveStatus::RefusedReadOnly &&
                                readOnlyBefore == readOnlyAfter,
        reason);
  SetFileAttributesW(readOnlyPath.c_str(), FILE_ATTRIBUTE_NORMAL);

  // ---- trace helper spot checks -----------------------------------------
  check("event_key_manual_none",
        ReviewBookmarkStore::eventKey(*restoredBookmarks.find(1)) == "none");
  check("players_trace_named",
        ReviewBookmarkStore::playersTrace(restoredBookmarks.find(3)->players) ==
            "player,2,7,Soldier;player,3,8,Medic");
  check("list_line_has_fields",
        ReviewBookmarkStore::listLine(*restoredBookmarks.find(3)).find("eventKey=300:2:5") !=
            std::string::npos);

  // ---- summary ----------------------------------------------------------
  const ReviewCounters& counters = bookmarks.counters();
  std::printf(
      "review-workspace: summary add=%llu update=%llu delete=%llu refused=%llu loadFailures=%llu "
      "saveFailures=%llu jumpFailures=%llu duplicateEvent=%llu fields=%d fieldMismatches=%d\n",
      static_cast<unsigned long long>(counters.added),
      static_cast<unsigned long long>(counters.updated),
      static_cast<unsigned long long>(counters.deleted),
      static_cast<unsigned long long>(counters.refused),
      static_cast<unsigned long long>(counters.loadFailures),
      static_cast<unsigned long long>(counters.saveFailures),
      static_cast<unsigned long long>(counters.jumpFailures),
      static_cast<unsigned long long>(counters.duplicateEvent), g_fields, g_fieldMismatches);

  std::printf("review-workspace: probe done ok=%d failures=%d\n", g_failures == 0 ? 1 : 0,
              g_failures);
  return g_failures == 0 ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc >= 2 && std::strcmp(argv[1], "--workspace-test") == 0) {
    fs::path scratch = fs::temp_directory_path() / "review-workspace-probe";
    if (argc >= 3) scratch = fs::path(argv[2]);
    return run(scratch);
  }
  std::printf("review-workspace: probe usage: --workspace-test [scratch-dir]\n");
  return 2;
}
