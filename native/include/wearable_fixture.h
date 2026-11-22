#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace tf2::native {

// task-41: the five fixture cases the card requires, run against the REAL table
// builder and the REAL path resolver rather than against a reimplementation of
// them. Each case is a hand-built snapshot with exactly one thing wrong with it,
// so the verdict it produces can only come from the rule under test.
//
// The cases:
//   1. missing property        -- a wearable with no m_hOwnerEntity at all
//   2. invalid handle          -- a sentinel owner handle, and a handle that
//                                 indexes past the snapshot
//   3. deleted-and-reused slot -- the owner slot's generation advanced after the
//                                 tick the table is built for
//   4. missing model index     -- an attributed owner and no route to a model
//   5. schema fallback         -- a per-class family template loses to the item's
//                                 literal model_player; and a row the schema
//                                 cannot answer is answered by the precache
//
// A fixture that cannot be built (no writable temp directory for the synthetic
// items_game.txt, say) reports FAIL with the reason, never a silent pass: a suite
// that cannot run is not evidence that the rules hold.
//
// Returns the number of FAILED checks. Every check appends one line to `report`
// in the form "wearable_fixture\t<name>\tPASS|FAIL\t<detail>".
std::size_t runWearableFixtureSuite(std::vector<std::string>& report);

} // namespace tf2::native
