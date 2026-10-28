# T75 Phase 1 -- native/CMakeLists.txt split into build modules

Status: **PHASE1-OK** (static equivalence only: no compilation, no link, no executable,
no gate, no hostlock). Base commit db3316e5b14b9a9dd573fe04686790db64184732, branch
task/t75-cmake. Phase 2 (real clean build + gate run) is NOT started: the machine build
lock is still owned by lead-t611-r15.

## 0. What changed, in one paragraph

native/CMakeLists.txt went from one 891-line build script to an **entry point** (53 lines:
two statements plus 23 include() lines) and **23 modules** under native/cmake/ (1210 lines).
The modules are **contiguous slices of the original file**, carried over as statement text,
and they are included in the original statement order. No statement was re-ordered, rewritten
or re-grouped; the only new statements in the build graph are the 23 include() calls. Target
names, per-target source sets, link libraries and their order, compile definitions, compile
options, target properties, install rules and cache variables are therefore identical by
construction, and the Phase 1 verifier proves that against the generated build tree rather
than against the source text (section 4).

## 1. File inventory

| | before (db3316e) | after |
|---|---|---|
| files | 1 | 24 (1 entry point + 23 modules) |
| lines | 891 | 1263 (entry 53 + modules 1210) |
| longest file | 891 | 149 (cmake/tf2-product.cmake) |

Line accounting, so the total is not a mystery:

* 891 original lines = **865** statement lines moved into the 23 modules + **2**
  statements kept in the entry point (cmake_minimum_required, project) + **24** blank
  separator lines that only existed to space out sections and were dropped.
* + **23** include() lines + **345** module header/comment lines + **28** entry-point
  comment lines = 1263.
* Every one of the 865 moved lines is byte-identical to its original line (modulo the
  CRLF line ending, which all 24 files share); the verifier checks this per module as
  exact line ranges of the base file, and the non-blank line multiset of the whole tree
  is preserved: 685 distinct lines, 0 lost, 0 duplicated.

## 2. Modules and their responsibilities

Include order below is the entry point's real order, which is the original statement order.
"moved lines" is the number of original statement lines in the slice; the difference to the
file's line count is that module's header comment.

| # | module | lines | moved lines | original lines | owns | targets |
|---|---|---|---|---|---|---|
| 1 | cmake/tf2-settings.cmake | 42 | 27 | 4-30 | global settings and the shared demo-header contract | (none) |
| 2 | cmake/tf2-version.cmake | 39 | 24 | 156-179 | package identity: version, build stamp, source tree, TF2_VERSION_DEFINES | (none) |
| 3 | cmake/tf2-product.cmake | 149 | 133 | 32-154, 181-190 | the product target tf2_demo_native and its include dirs / defines / link line | tf2_demo_native |
| 4 | cmake/tf2-probes-logstf.cmake | 27 | 12 | 192-203 | the logs.tf panel contract probe | logstf_viewer_probe |
| 5 | cmake/tf2-probes-core.cmake | 46 | 31 | 205-235 | item schema, overlay contract, temp effects/fixtures, demo open | item_schema_probe entity_overlay_probe temp_effect_probe temp_entities_fixture_probe demo_open_probe |
| 6 | cmake/tf2-probes-archive.cmake | 38 | 23 | 237-259 | PCF capability, material/resource chains through the archive layer, VPK query and integrity | pcf_capability_probe material_chain_probe resource_reachability_probe vpk_query vpk_integrity_probe |
| 7 | cmake/tf2-probes-world.cmake | 64 | 49 | 261-309 | world material / geometry / coverage / lighting / feature / terrain blend | world_material_probe world_geometry_probe material_coverage_probe lighting_semantics_probe world_feature_probe terrain_blend_probe |
| 8 | cmake/tf2-probes-presentation.cmake | 32 | 17 | 311-327 | presentation, map lookup order, texture quad | presentation_probe map_lookup_probe texture_quad_probe |
| 9 | cmake/tf2-probes-model.cmake | 77 | 62 | 329-390 | model pose, viewmodel range, model robustness, entity model | model_pose_probe viewmodel_range_probe model_robustness_probe entity_model_probe |
| 10 | cmake/tf2-probes-overlay.cmake | 56 | 41 | 392-432 | the overlays that read per-entity state: SourceTV spectate and projectile track | spectate_probe projectile_track_probe |
| 11 | cmake/tf2-probes-viewmodel.cmake | 140 | 125 | 434-558 | the ViewModel contract, FOV, animation state and the static weapon body | viewmodel_fov_probe viewmodel_contract_probe viewmodel_animation_probe weapon_body_probe animation_viewmodel_probe |
| 12 | cmake/tf2-probes-animation.cmake | 49 | 34 | 560-593 | skinning contract and weapon animation | skeleton_skin_probe weapon_animation_probe |
| 13 | cmake/tf2-probes-audio.cmake | 85 | 70 | 595-664 | audio effects / schedule / timeline census / content map / corpus scan / winmm sink | audio_effects_probe audio_scheduler_probe sound_timeline_census_probe sound_content_map_probe sound_corpus_scan_probe winmm_sink_probe |
| 14 | cmake/tf2-probes-runtime.cmake | 37 | 22 | 666-687 | install smoke and the native UI resource panel | native_install_smoke_probe native_ui_probe |
| 15 | cmake/tf2-probes-playback.cmake | 33 | 18 | 689-706 | playback control and the camera director / bookmarks / key table | playback_control_probe camera_probe |
| 16 | cmake/tf2-probes-protocol.cmake | 39 | 24 | 708-731 | entity protocol census and the synthetic wire fixtures | entity_protocol_probe entity_message_fixture_probe |
| 17 | cmake/tf2-probes-quiet-capture.cmake | 22 | 7 | 733-739 | the quiet-capture trace/metric/bitmap reader | quiet_capture_probe |
| 18 | cmake/tf2-probes-viewmodel-material.cmake | 35 | 20 | 741-760 | the ViewModel material resource chain (phase 1) | viewmodel_material_probe |
| 19 | cmake/tf2-probes-hud.cmake | 24 | 9 | 762-770 | the custom HUD import contract | hud_probe |
| 20 | cmake/tf2-probes-install-cli.cmake | 36 | 21 | 772-792 | the installer lifecycle CLI and the VPK readiness probe | install_cli_probe vpk_readiness_probe |
| 21 | cmake/tf2-probes-minimap.cmake | 26 | 11 | 794-804 | the minimap projection / hit test / panel state | minimap_probe |
| 22 | cmake/tf2-probes-review.cmake | 26 | 11 | 806-816 | the review workspace store and bookmarks | review_workspace_probe |
| 23 | cmake/tf2-package.cmake | 88 | 74 | 818-891 | install rules, package payload, WebView2 loader staging, compiler options | (none) |

Total: 23 modules, 1210 lines, 865 moved statement lines, **53 targets** covered
(1 product + 52 probe / query executables).

Why some domains are two modules (world, model, viewmodel-material, ...): the original file
interleaves targets from different domains, and CMake creates targets in read order while
NMAKE builds in that order. Moving a target past another domain's target to make the file
tidier would be a **build-order change**, which the probes' own comments depend on (see
tf2-probes-viewmodel.cmake). The split therefore follows the original order and, where a
domain is interrupted by another domain, that domain is simply two modules.

## 3. Equivalence method

~~~text
bash .scratch/t75-cmake/verify-split.sh        # configure only; never builds
~~~

The verifier (committed as evidence/t75-cmake/verify-split.sh + verify_split.py, 25 + 680
lines) does **not** trust the source text; it configures three trees with the same generator
and toolchain the product build uses (**NMake Makefiles**, Release, cl, MSVC PATH/INCLUDE/LIB
from env-msvc.sh) and compares what CMake produced:

| tree | source | purpose |
|---|---|---|
| before | full copy of native/ with CMakeLists.txt replaced by the db3316e bytes, cmake/ removed | the reference build graph |
| after | native/ as it is in this commit | the split |
| before-ctl | the **same** before source configured a second time | measures CMake's own nondeterminism |

Layers, each a hard gate on exit 0:

0. staging -- the module list is parsed from the entry point's include() lines, so the
   same list drives the comparison and the file list (23 modules, all on disk).
1. configure -- 3x rc=0, and an assertion that configure produced **no object file**
   (Phase 1 must not compile).
2. control -- the before-ctl run defines what "stable" means. Measured: 503 files,
   501 byte-identical, 9 unstable between two identical runs (Makefile.cmake's unordered
   dependency list, CMakeConfigureLog.yaml, ShowIncludes/main.obj, CompilerId/ABI binaries
   with PE timestamps, CMakeScratch random dirs). The layer asserts that **no
   contract-bearing file is in the unstable set**.
3. generated tree -- normalise paths (case-insensitive, both slash styles, the lowercase
   drive letter CMakeCache.txt writes vs the uppercase backslashes build.make writes), fold
   the TF2 build stamp, then byte-compare all 503 files.
4. contract parse -- layout-independent extraction of the build graph from the source text:
   add_executable/add_library (name + ordered sources), target_link_libraries (per-target,
   ordered), target_compile_definitions/options/include_directories, set_target_properties,
   install(), CACHE variables (name, default, type, doc) and the target creation order.
5. verbatim guard -- every module must be exact line ranges of the base file; the entry
   point may carry only the two statements plus include() lines; non-blank line multiset.
6. inventory -- from the generated tree: target directories, per-target ordered source list,
   link line (exe name, subsystem, libraries and order) and install script rules.

Note on the link line: **NMake Makefiles generates no link.txt** (CMakeFiles/<t>.dir/link.txt
does not exist under this generator). The link line lives in each target's
CMakeFiles/<t>.dir/build.make as the " /out:..." command, with the project's libraries in
source order followed by CMake's default system libraries; that line is what layer 6
compares, for all 53 targets.

## 4. Result

The verifier exits 0. Quoted from evidence/t75-cmake/verify-split-output.txt:

~~~text
== RESULT
  targets_before=53          targets_after=53
  object_rules_before=430     object_rules_after=430
  generated_files=503 per side
  byte_identical=501          control_proven_unstable=2          unexplained=0
  contract_files_differing=0  install_rules_differing=0  link_lines_differing=0
  EQUIVALENT=YES
~~~

The two non-identical files, and why neither is a defect:

~~~text
  OK   every remaining difference is control-proven unstable    2 of 503 files differ
       CMakeFiles/Makefile.cmake                    unordered dependency list (+ the 23 module paths)
       CMakeFiles/ShowIncludes/main.obj             control-proven unstable
  OK   CMakeFiles/Makefile.cmake delta                          0 lines lost, 23 gained, exactly the 23 module paths
~~~

Makefile.cmake is compared as a multiset because its ordering is unstable even between two
runs of the same source; the delta is the point: CMake now records the 23 modules as
makefile dependencies, which is exactly what makes an **edit to a .cmake file trigger a
reconfigure** -- and nothing else was gained or lost.

Other contract lines worth quoting:

~~~text
  OK   add_executable/add_library: names and ordered sources    53 targets
  OK   no target defined twice                                  53 add_* calls == 53 unique names, both sides
  OK   target creation order (== NMAKE build order)             identical, 53 targets
  OK   target_link_libraries: libs and their order              32 targets, 35 calls
  OK   target_compile_definitions                               35 targets
  OK   target_compile_options                                   6 targets
  OK   target_include_directories                               50 targets
  OK   set_target_properties                                    52 targets
  OK   install() rules                                          8 rules
  OK   CACHE variables                                          TF2_PACKAGE_VERSION, TF2_PACKAGE_VERSION_CODE, TF2_SOURCE_DATE_EPOCH
  OK   per-target ordered source list                           53 targets, 430 compile rules, 106 distinct sources
  OK   link lines: exe name, subsystem, libs and order          53 targets identical (53 unique exes)
  OK   cmake_install.cmake file(INSTALL) rules                  9 before == 9 after
~~~

Configures: before 1.5s + 0.4s, after 1.5s + 0.4s, control 1.5s + 0.4s (cmake 4.4.3).

## 5. Static guard: no target or source lost, none duplicated

* 53 add_executable/add_library statements before, 53 after, 53 **unique** names -- a
  name defined twice would collapse into one dict key and pass a naive set compare, so the
  raw call count is asserted separately.
* 430 source->object compile rules on both sides, from **106 distinct source files**; the
  per-target ordered source list is compared target by target, and the union path set is
  identical ("source path set: identical; no source lost, none invented").
* 53 link lines compared including library order; 53 unique executable names.
* 8 install() rules and 9 file(INSTALL) calls in the generated install script, both sides.

## 6. Scope guarantees that follow from the mechanism

* include() does not open a new scope: a variable set by a module is visible to the modules
  included after it, exactly as when the statements were adjacent in one file. Nothing was
  moved across a variable it defines before first use (the verifier's layer 3 would have
  caught a use-before-define, since the generated build graph would change).
* No module uses a path variable whose value depends on which file is being processed:
  CMAKE_CURRENT_LIST_DIR / CMAKE_CURRENT_LIST_FILE appear 0 times before and 0 times after.
  CMAKE_CURRENT_SOURCE_DIR appears 9 times before and 9 times after and still resolves to
  native/, because include() does not change the current source directory (this is not
  add_subdirectory -- there is no extra directory scope anywhere).
* CACHE variables (3) are set in the same module that owns them, with the same default,
  type and doc string.

## 7. Risks and unverified items

1. **Nothing was compiled.** That is the Phase 1 contract, not an oversight. The generated
   build graph is byte-identical for every contract-bearing file, which is stronger than a
   compile for the questions this change can plausibly affect (sources, defines, libs,
   flags, order), but it says nothing about runtime behaviour. Phase 2 must build.
2. **install-readiness-check.sh breaks on the new layout** -- and it is the one gate that
   hard-codes line-oriented greps against native/CMakeLists.txt. Its file is out of my
   scope (no .sh edits in Phase 1), so it is listed here as required Phase 2 pre-work:
   * :559-561 asserts grep -c install_lifecycle.cpp native/CMakeLists.txt == 3 -> now 0: **FAILS**.
   * :565-566 asserts grep -cE "^set\(TF2_PACKAGE_VERSION" native/CMakeLists.txt == 2 -> now 0: **FAILS**.
   * :750-776 (M3 mutation) anchors on the install(TARGETS tf2_demo_native
     native_install_smoke_probe ...) line and on the TARGET_FILE generator expression for
     native_install_smoke_probe, both of which now live in native/cmake/tf2-package.cmake
     -> python exits "M3 root anchor absent" -> "M3 applied" and the M3 assertions **FAIL**.
     It fails safely (nothing is mutated, the backup restore is byte-identical).
   * :609-612 backs up and restores native/CMakeLists.txt as SRC_CMAKE; the mutation target
     must become native/cmake/tf2-package.cmake, including the restore at :799-800 and the
     restore-verification loop at :815.
   * :228 "find native/src native/include native/CMakeLists.txt -type f -newer EXE" no
     longer sees native/cmake/*.cmake: this one **weakens silently** rather than failing,
     which is worse than the loud failures -- it should find native/cmake as well.
   * :551 and :557 (CPack and blind directory-removal greps) still pass, but they now grep
     only the entry point, so a rule hidden in a module would go unnoticed. Same silent
     weakening.
   * Fix for :559 and :565: grep over native/CMakeLists.txt **and** native/cmake/*.cmake
     (or change the assertion to count across the build script directory). I did not touch
     them; this is the lead's call.
3. **evidence/t49/fullscreen-locked-run.sh:50** uses "find native -maxdepth 1 -name
   CMakeLists.txt" for a freshness check; it is a historical evidence script, not a gate,
   but it would now miss module edits.
4. build-target.sh is layout-agnostic (it reads the generated Makefile), so the per-target
   build path the mutation gates use is unaffected.
5. The verifier depends on the CMake 4.4.3 / NMake behaviour documented above (no link.txt).
   On another generator or CMake version layer 6 would need adjusting; the contract layers
   (4, 5) are generator-independent.

## 8. Phase 2 plan (not started -- blocked on the host lock)

~~~text
bash /d/TF2_Native_Worktrees/_shared/hostlock.sh status            # read-only, allowed
bash /d/TF2_Native_Worktrees/_shared/hostlock.sh acquire t75-cmake # only after the lead says released
bash build-cmake.sh clean
bash build-cmake.sh                    # expect exe_count=53, 0 errors, 0 warnings
bash build-target.sh tf2_demo_native vpk_query   # incremental target build stays valid
bash install-lifecycle-check.sh        # real --install / --uninstall behaviour
bash /d/TF2_Native_Worktrees/_shared/hostlock.sh release t75-cmake
~~~

Estimates: clean build of 53 executables / 430 compile rules at /O2 with --parallel 2,
roughly 4-8 minutes (measure and record the real number); incremental target build < 1
minute; install-lifecycle-check.sh 2-3 minutes. install-readiness-check.sh (5-10 minutes,
it rebuilds the product for mutations) can only be run **after** the anchor fixes in
section 7.2. The full verify-all.sh chain is 30-60+ minutes and is not required by a change
that moves no source line -- recommend a targeted set unless the lead wants full parity.

## 9. Artefacts

In evidence/t75-cmake/:

| file | what it is |
|---|---|
| T75-SPLIT-PHASE1.md | this report |
| module-manifest.tsv | module, lines, moved lines, original line ranges, owns, targets |
| contract-inventory.tsv | per-target source counts plus the link libraries, from the generated tree |
| verify-split-output.txt | the full verifier transcript quoted above |
| generated-tree-differences.txt | the 2 residual files and why each is control-proven |
| line-map.json | the machine-readable slice map used to build the modules |
| verify-split.sh | the verifier wrapper (configure only) |
| verify_split.py | the verifier engine (6 layers) |

Both verifier files are byte-identical to the pair that produced verify-split-output.txt and
they locate the repository from their own directory, so they can be re-run from
`evidence/t75-cmake/` (they write their outputs there). `core.autocrlf=true` on this host means a
fresh clone checks them out as CRLF, like every other `.sh` in the repository; MSYS bash runs a
CRLF script here (checked), so that is a non-issue, but converting back with `sed -i 's/\r$//'`
restores the exact original bytes.
