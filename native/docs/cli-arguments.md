# Command-line arguments: unrecognised flags are counted, and `--strict-args` refuses them

The player parses its own command line in `native/src/main.cpp` (the loop that
starts after the settings-file pre-scan and ends before `persistent.render.normalize()`).
Two rules describe what happens to an argument the loop does not recognise:

- **Default: ignore, but say so.** The argument is collected, and one line is
  written to the trace (only if `TF2_NATIVE_TRACE` names a file):
  `args: unrecognized=<count> first=<name> all=<name,name,...>`, capped at 16
  names with `,...+N more` marking the cap. The run continues exactly as it did
  before this line existed — no flag's default behaviour changed.
- **`--strict-args`: refuse.** If anything was unrecognised, the player prints
  `TF2 Demo Player: unrecognized argument(s) refused by --strict-args: <names>`
  to stderr and exits **2** before the window is shown and before any asset,
  demo or GPU device is touched. With a clean command line the switch changes
  nothing (the normal run continues and exits 0).

Exit code 2 is the strictness refusal. It is distinct from 13 (the player
*understood* a value and refused it, e.g. `--playback-speed 0`), from 15
(unreadable camera script), 16 (capture failed) and 10/11/12 (window/device
init failures). A gate that sees 2 knows the command line was wrong, not the
tree.

Why the default stays "ignore": `verify-all.sh` drives older scripts that carry
spellings this loop never learned, and turning a silently-ignored flag into a
hard failure by default would break them without proving anything about the
build. `build-identity-check.sh` (section 1c/5) covers both halves and fails on
an *empty* reading, so "the argument was counted" is proven, not assumed.
