// map_lookup_probe -- T9 (2026-10-16): the map search order, asserted without
// a disk.
//
// WHY A PROBE AND NOT A PRODUCT RUN
//   The order is a pure function of the map name. Proving it by launching the
//   product would prove it for the one map that happened to be on disk, would
//   need a 40 s load per case, and could not express a negative like "a name
//   with a path separator is refused before any store is touched" -- a run for
//   that case would just fail to find a file, which is the answer this probe
//   has to tell apart from a refusal. The product walks the SAME function
//   (planMapLookup), so what is asserted here is what the product does.
//
// WHAT IT DOES NOT PROVE
//   That a file exists at a candidate path. "download/maps/cp_gullywash_f9.bsp
//   is searched second" and "download/maps/cp_gullywash_f9.bsp contains a BSP"
//   are different claims; the second one is a product run's job and
//   resource-discovery-check.sh does it there.
//
// Output: one JSON object per check, then a summary. Exit 1 if any check failed.
#include "map_lookup.h"

#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

int checks = 0;
int bad = 0;

void emit(const std::string& key, bool ok, const std::string& detail) {
  ++checks;
  if (!ok) ++bad;
  std::cout << "{\"check\":\"" << key << "\",\"ok\":"
    << (ok ? "true" : "false") << ",\"detail\":\"" << detail << "\"}\n";
}

void checkStr(const std::string& key, const std::string& actual, const std::string& expected) {
  emit(key, actual == expected, "actual=" + actual + " expected=" + expected);
}

// One candidate as a single comparable string: "<store>|<relative>|packed=<0|1>".
std::string candidateText(const tf2::native::MapLookupCandidate& candidate) {
  return candidate.store + "|" + candidate.relativePath
    + "|packed=" + (candidate.packed ? "1" : "0");
}

std::string planText(const tf2::native::MapLookupPlan& plan) {
  std::ostringstream text;
  for (std::size_t i = 0; i < plan.candidates.size(); ++i) {
    if (i) text << " ; ";
    text << candidateText(plan.candidates[i]);
  }
  return text.str();
}

// A refused name: not usable, no candidates, and a reason that is not empty.
// The reason is checked for being PRESENT rather than for an exact sentence:
// the sentence is prose a later reader may reword, while "there is a reason"
// is the contract.
void checkRefused(const std::string& key, const std::string& mapName) {
  const auto plan = tf2::native::planMapLookup(mapName);
  std::ostringstream detail;
  detail << "usable=" << (plan.nameUsable ? "1" : "0")
    << " candidates=" << plan.candidates.size()
    << " reason_present=" << (plan.reason.empty() ? "0" : "1")
    << " reason=\"" << plan.reason << "\"";
  emit(key, !plan.nameUsable && plan.candidates.empty() && !plan.reason.empty(),
    detail.str());
}

} // namespace

int main() {
  // ---- the order, on a real map name -------------------------------------
  {
    const auto plan = tf2::native::planMapLookup("cp_gullywash_f9");
    checkStr("usable", plan.nameUsable ? "yes" : "no", "yes");
    checkStr("candidate count", std::to_string(plan.candidates.size()), "3");
    checkStr("search order", planText(plan),
      "maps|maps/cp_gullywash_f9.bsp|packed=0 ; "
      "download-maps|download/maps/cp_gullywash_f9.bsp|packed=0 ; "
      "vpk|maps/cp_gullywash_f9.bsp|packed=1");
    // The installed copy is searched first: an operator's own tf/maps file must
    // not be shadowed by whatever a server pushed.
    if (plan.candidates.size() == 3) {
      checkStr("first store", plan.candidates[0].store, "maps");
      checkStr("second store", plan.candidates[1].store, "download-maps");
      checkStr("third store", plan.candidates[2].store, "vpk");
      // A VPK cannot hold a download/ directory, so the third candidate must be
      // the shipped path and must be the packed one.
      checkStr("vpk candidate is packed", plan.candidates[2].packed ? "yes" : "no", "yes");
      checkStr("vpk candidate path", plan.candidates[2].relativePath,
        "maps/cp_gullywash_f9.bsp");
      checkStr("download candidate is loose",
        plan.candidates[1].packed ? "packed" : "loose", "loose");
    }
  }

  // ---- determinism --------------------------------------------------------
  // Two calls must produce the identical list. This is the "search order is
  // deterministic" half of the acceptance that a set-based implementation
  // would pass by accident and a directory-order implementation would not.
  {
    const auto first = tf2::native::planMapLookup("cp_process_f12");
    const auto second = tf2::native::planMapLookup("cp_process_f12");
    checkStr("deterministic across calls", planText(first), planText(second));
  }

  // ---- names that are accepted -------------------------------------------
  // The character set TF2 map names actually use, including the leading digit
  // on the stock 2koth_* maps.
  for (const char* name : {"cp_sunshine", "2koth_abbey", "koth_bagel_rc12",
                           "cp_orange_z7_n", "pl_upward", "achievement_apg_r16c"}) {
    const auto plan = tf2::native::planMapLookup(name);
    checkStr(std::string("accepted: ") + name, plan.nameUsable ? "yes" : "no", "yes");
  }

  // ---- names that are refused --------------------------------------------
  // Each is a different reason, so the probe reports the reason's presence
  // rather than one catch-all sentence.
  checkRefused("refused: empty", "");
  checkRefused("refused: forward slash", "../secrets");
  checkRefused("refused: backslash", "..\\..\\windows");
  checkRefused("refused: absolute drive", "C:/windows/system32/x");
  checkRefused("refused: parent segment", "cp..sunshine");
  checkRefused("refused: leading dot", ".hidden");
  checkRefused("refused: trailing dot", "cp_sunshine.");
  checkRefused("refused: space", "cp sunshine");
  checkRefused("refused: non-ascii byte", "cp_sunsh\xc3\xa9");

  // Traversal is refused as a NAME problem, not as a missing file: the detail
  // says a separator was seen, which is the only way a caller can tell this
  // apart from a map that simply is not installed.
  {
    const auto plan = tf2::native::planMapLookup("../secrets");
    checkStr("traversal is refused by name",
      plan.reason.find("separator") == std::string::npos ? plan.reason : "separator",
      "separator");
  }

  std::cout << "{\"checks\":" << checks << ",\"bad\":" << bad << "}\n";
  return bad == 0 ? 0 : 1;
}
