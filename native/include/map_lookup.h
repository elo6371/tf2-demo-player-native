#pragma once

#include <string>
#include <vector>

namespace tf2::native {

// ---------------------------------------------------------------------------
// Where a demo's BSP may live, inside ONE tf root.
// ---------------------------------------------------------------------------
// T9 (2026-10-16). Before this the product built exactly one path,
// `<tf>/maps/<map>.bsp`, and read it as a loose file; if that failed it asked
// the VPKs for `maps/<map>.bsp` and nothing else. A map that a server pushed to
// `<tf>/download/maps/` was therefore invisible: the demo played, but the BSP
// parse failed and the run reported `BSP: invalid/BSP header is invalid` with a
// fallback material path. T4 recorded two demos in that state.
//
// The order below is the contract, and it is a list rather than a search so it
// can be asserted without a disk:
//
//   1. maps/<name>.bsp            loose, the install's own copy
//   2. download/maps/<name>.bsp   loose, the copy a server pushed
//   3. maps/<name>.bsp in a VPK   packed, the shipped snapshot
//
// Why this order: this codebase resolves every other asset loose-before-packed
// (materials, models, sounds), so the map follows the same rule rather than
// inventing a second one. Between the two loose locations the install's own
// copy wins, because download/ is whatever the last server pushed and may be a
// different revision of the same map name.
//
// A VPK cannot contain a `download/` directory -- the pak is built from the
// shipped tree -- so there is no fourth candidate. Saying so here keeps a later
// reader from adding one and turning "not found" into a slow not-found.
struct MapLookupCandidate {
  std::string relativePath;  // posix separators, always relative to the tf root
  std::string store;         // "maps" | "download-maps" | "vpk"
  bool packed = false;       // true = read from a VPK, not from the file system
};

struct MapLookupPlan {
  bool nameUsable = false;
  std::string mapName;
  // Why the name was refused, when it was. Not a generic "invalid": a traversal
  // attempt and an empty name are different findings and the caller reports
  // them separately.
  std::string reason;
  std::vector<MapLookupCandidate> candidates;
};

// The plan for one map name. Pure: no file system, no VPK, no root. The caller
// walks `candidates` in order and stops at the first that yields bytes.
MapLookupPlan planMapLookup(const std::string& mapName);

// True when `mapName` is a bare map name this player will look for. Exposed so
// the product and the probe refuse the same names for the same reason.
bool mapNameIsUsable(const std::string& mapName, std::string* reason);

} // namespace tf2::native
