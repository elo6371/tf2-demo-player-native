# T63 RUNTIME PROPOSAL -- commands, durations, lock, assertions, mutations

Static phase deliverable. NOTHING here has been run: no build, no launch, no capture.
All binaries are the ones already present under native/build-nmake (sha256 pinned in
sample_list.json identity block; product tf2_demo_native.exe f454718b...).

Shared lock: every phase below runs under
  bash /d/TF2_Native_Worktrees/_shared/hostlock.sh run t63-sourcetv '<command>'
(hostlock.sh v2.2 usage: status | run <owner> <cmd...> | acquire | release | reserve | unreserve | break).
One serial block, owner t63-sourcetv. Proposed reservation: 35 minutes. No builds in P0-P3;
the build mutations M7-M10 are a separate 40-minute block the Lead schedules after P0-P3.

Demo paths (exact, copied from sample_list.json demos[].path):
  S1 D:/TF2_Demo_Player/testdata/demos/bb841c6d379ff7c40d0c8baf99f59d8d_matcha-20260927-1347-cp_snakewater_final1.dem
  S2 D:/SteamLibrary/steamapps/common/Team Fortress 2/tf/demos/review12.dem
  S3 D:/SteamLibrary/steamapps/common/Team Fortress 2/tf/demos/73.dem
  S4 D:/SteamLibrary/steamapps/common/Team Fortress 2/tf/demos/prodcutscout.dem
  S5 D:/TF2_Demo_Player/testdata/demos/4a9bfb9276509d0ec5f5fdc722a95b17_match-20260927-0239-koth_bagel_rc13.dem
  X1 D:/SteamLibrary/steamapps/common/Team Fortress 2/tf/demos/SUNSHINE.dem
  (S1 and S5 live in the D:/TF2_Demo_Player testdata corpus, not in tf/demos; both are SourceTV,
   clientName "SourceTV Demo" in the header.)

## P0 -- sample-list integrity and SourceTV camera identity (~1 min)

  cd /d/TF2_Native_Test
  python - <<'PY'
  import hashlib,json
  d=json.load(open("evidence/t63-sourcetv/sample_list.json"))
  for x in d["demos"]:
      assert hashlib.sha256(open(x["path"],"rb").read()).hexdigest()==x["sha256_full"], x["name"]
  print("hash-ok",len(d["demos"]))
  PY
  native/build-nmake/demo_open_probe.exe --scan "<S5>" > evidence/t63-sourcetv/rt-scan-bagel.txt

Assert A0: six hashes match; mutation: substitute S3 path in one row -> hash mismatch -> red.
Assert A1: scan output has setview_count=1 and last_view_entity=1 (SourceTV camera slot);
  mutation: --scan a POV demo (e.g. D:/SteamLibrary/.../autorecord_2026-07-02_13-26-46.dem,
  last_view_entity=18) -> red. This is the only assertion that proves the HUD entity is the camera.

## P1 -- property ground truth via entity_model_probe (~14 min total)

Tool semantics verified in native/tools/entity_model_probe.cpp (1429-1448, 700-712, 929-966):
- --entity N filters to ONE entity (a repeated flag just overwrites; it is not additive).
- With no --entity the tool prints every entity but only ORIGIN properties (isOriginProperty
  filter), so health/ammo/charge/score/objective need one run PER entity.
- --props-at takes a comma-separated tick list; each run parses the whole demo.
- Property lines go to STDERR -> always redirect with 2>&1.

  native/build-nmake/entity_model_probe.exe --demo "<S5>" --history-stats \
      --props-at 129277 --entity 1 > evidence/t63-sourcetv/rt-props-bagel-e1.txt 2>&1
  native/build-nmake/entity_model_probe.exe --demo "<S1>" --history-stats \
      --props-at 10919,30319,74568 --entity 1 > evidence/t63-sourcetv/rt-props-snakewater-e1.txt 2>&1
  native/build-nmake/entity_model_probe.exe --demo "<S2>" --history-stats \
      --props-at 15944,35344,136178 --entity 1 > evidence/t63-sourcetv/rt-props-review12-e1.txt 2>&1
  native/build-nmake/entity_model_probe.exe --demo "<S4>" --history-stats \
      --props-at 26195,75595,113804 --entity 1 > evidence/t63-sourcetv/rt-props-product-e1.txt 2>&1

  S3 (ashville73) needs seven runs, one entity each, ticks 16949,36349,128642:
    --entity 3  (player)      -> rt-props-ashville-e3.txt
    --entity 7  (player)      -> rt-props-ashville-e7.txt
    --entity 12 (player)      -> rt-props-ashville-e12.txt
    --entity 26 (CTFTeam)     -> rt-props-ashville-e26.txt
    --entity 27 (CTFTeam)     -> rt-props-ashville-e27.txt
    --entity 31 (PlayerResource) -> rt-props-ashville-e31.txt
    --entity 32 (ObjectiveResource) -> rt-props-ashville-e32.txt
  (If the enter roster shows team/resource ids differ on another demo, discover them with
   --prop-candidates-at <tick> --candidate-suffix m_iScore / m_iOwner first.)

Discovery for demos whose rosters are unknown statically (bagel/snakewater/review12/product):
  entity_model_probe.exe --demo "<S1>" --prop-candidates-at 20000 --candidate-suffix m_hActiveWeapon
  entity_model_probe.exe --demo "<S3>" --prop-candidates-at 36349 --candidate-suffix m_iAmmo
  (prop-candidates prints candidate properties per entity; run it without --entity.)

Expected duration: S3 7 runs x ~85 s = ~10 min; S5 47 MB ~40 s; S1 52 MB ~45 s; S4 62 MB ~50 s;
S2 80 MB ~65 s. Baseline: entity_protocol_probe 664 MB / 9 demos = 3.5 min -> 0.32 s/MB;
entity_model_probe carries history + model work, allow 2-3x. Total ~13-14 min.
Cost note: the per-entity split is forced by the tool; a cheaper path would be a probe flag that
prints selected properties for all entities at once (not present at this HEAD).

## P2 -- product HUD behaviour (~7 min)

Camera script per demo (steps are literal; see native/src/main.cpp:689-760 loader):
  script S5 (bagel):  tick 600 / mark-tick / tick 60000 / capture <out.png> hud-p1 / quit 0
  script S1 (snakewater): tick 600 / tick 20000 / capture <out.png> hud-p1 / quit 0
  and similarly S2, S3, S4; X1 only for pixel.

Launch (one at a time; title carries the HUD line):
  TF2_NATIVE_TRACE=evidence/t63-sourcetv/rt-trace-bagel.txt \
  native/build-nmake/tf2_demo_native.exe --demo "<S5>" --playback-speed 32 \
      --hidden-window --audio-disabled --camera-script evidence/t63-sourcetv/rt-script-bagel.txt
Poll the window title (PowerShell):
  (Get-Process tf2_demo_native).MainWindowTitle  -> "HUD[tick=N ... health=.. team=.. class=.. clip=.. uber=..]"

Expected duration: at 32x, bagel 73118 frames ~38 s, snakewater 34 s, product 46 s,
ashville 58 s, review12 63 s, plus ~8 s startup each -> ~6 min. Assertions read the last
HUD title before quit and the trace file.

## P3 -- pixel captures (T64/T65 hand-off; BSP demos only) (~4 min)

Only S1/S2/X1 have a BSP. Use the existing camera-ui harness for capture+title assertions
(evidence/camera-ui-check.sh run_e2e, assertions at lines 357-362), or the P2 launches with
capture steps. bagel/ashville73/product must NOT be used for pixel evidence (gap G3).

## Total

P0 1 min + P1 14 min + P2 6 min + P3 4 min + overhead ~4 min = ~29 min; request 35 min lock.
Build mutations (below) need a second block: build ~3-5 min + 4 product runs ~4 min + margins = 40 min.

## Assertions (expected value + mutation that reds it)

| id | reading | known expected value | mutation that would red it |
|---|---|---|---|
| A0 | sha256 of 6 demos | sample_list.json sha256_full | point one row at another demo -> mismatch |
| A1 | demo_open_probe --scan S5 | setview_count=1, last_view_entity=1 | run --scan on a POV demo -> last_view_entity=18 |
| A2 | product HUD at S5-P0 (playback 600) | entity=before-window stale=0 health=? team=? class=? clip=? uber=? (R4) | build M7 (correct firstPacketTick) -> becomes in-window numeric |
| A3 | entity_model_probe --props-at S3 36349 entity 3/7/12 | m_iHealth 1..150; m_iTeamNum in {2,3}; m_iClass 1..9; properties present | --entity 9999 -> must print absent (if it prints 0/1 the reader defaults -> red) |
| A3b | product HUD S1-P1 (playback 20000) | health/team/class equal the props-at values of entity 1 at scene tick 20002 (stale 10317..) | build M7 -> stale becomes 0 and values change to server tick 30319 -> red |
| A4 | --props-at S3-P1/P2 entity 3..19 grep m_iAmmo | at least one m_iAmmo.0xx present, int 0..999 | --candidate-suffix m_iAmmo.099 -> absent |
| A5 | weapon entity m_iClip1 (resolve m_hActiveWeapon & 0x7FF) | int 0..99 (or absent for non-clip weapons) | build M8 (read weapon entity in playback_hud) -> HUD clip=? becomes a number -> red on current-behaviour assertion |
| A6 | --props-at S3 medic m_flChargeMeter | Float 0..100, present | query m_flChargeLevel -> absent |
| A7 | --props-at S3 m_nPlayerCond | int present (bit mapping unknown, gap G5) | query m_nPlayerCondEx instead -> different value |
| A8 | --props-at S3-P1/P2 entities 26,27 m_iScore (two runs) | ints >=0, P2 >= P1 | read m_iRoundsWon -> different number |
| A8b | --props-at S3-P2 entity 31 m_iTotalScore.000-.101 | per-slot ints, present for occupied slots | read m_iDeaths.xxx -> different |
| A9 | --props-at S3-P1/P2 entity 32 | m_iOwner.000-.007 in {0,1,2,3}; 0<=m_flLazyCapPerc<=1; m_iNumControlPoints int | read m_iTeamIcons -> absent/wrong type |
| A10 | --props-at S3 entity 3/7/12 across ticks | observe m_lifeState=2 (dead) at some tick and 0 (alive) at another | all-alive tick -> assertion "dead seen" red; or build M10 |
| A11 | product HUD title | contains no player-name field (none exists) | build adds name -> red (contract change must be explicit) |
| A12 | --props-at game-rules entity (discover id) | m_iRoundState int, m_flCountdownTime float present | wrong class entity -> absent |

Honesty note: no independent oracle exists for property values (ent-oracle.exe not found; the
oracle CLI '<demo> <0|1>' prints survey/enter counts only). Therefore A3-A12 are anchored by
(a) exact known readings R1-R4 where they exist, (b) two-run determinism (same command twice,
byte-identical values), (c) plausibility ranges, (d) tick-sensitivity (a +1 tick query must change
at least one moving property), (e) absence checks (wrong entity/property must not print a default).
A reading that is merely nonzero is NOT evidence.

## Build mutations (separate block, needs build + lock; Lead schedules)

M7: main.cpp:8226 change sceneTick origin to the demo's first SERVER tick (net_Tick of first packet)
    -> A2 and A3b must flip from before-window/stale to in-window/exact.
M8: playback_hud.cpp:45 read m_hActiveWeapon handle -> weapon entity DT_LocalWeaponData.m_iClip1
    -> A5 current-behaviour assertion (clip=?) goes red (proves the assertion is live).
M9: playback_hud.cpp:46 change m_flChargeLevel -> m_flChargeMeter -> A6 HUD assertion uber=? red.
M10: add m_lifeState to the HUD title -> A10/A11 contract assertion red.
Each mutation: before/after same input, byte-restore the source after (git diff must be empty).
