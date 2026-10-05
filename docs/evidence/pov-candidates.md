# 真实 POV Demo 候选表

> 由 `scripts/pov-scan.ts --evidence` 生成，**不要手改** —— 手改会与 `release-pov-scan/pov-scan.json` 脱节。
> 生成时间：2026-10-01T10:35:34.908Z
> 扫描目录：`D:\SteamLibrary\steamapps\common\Team Fortress 2\tf\demos`（扫描 1582 个 `.dem`，用时 1 s）
> 机器可读读数：`release-pov-scan/pov-scan.json`

## 判据：为什么是 `is_hltv`，不是名字

两条独立事实分开报，不合并：

| 事实 | 来源 | 强度 |
|---|---|---|
| `is_hltv` | 服务器在 `svc_serverinfo` 里自报（`isSourceTV` 字段） | **权威** —— 是服务器说的，不是我们猜的 |
| `clientName` / `serverName` | demo 固定 1072 字节头部 | **线索** —— SourceTV 机器人会把自己名字写进去，但玩家也能录 SourceTV |

判决由**产品自己的** `inferRecordingType()`（`src/parser/recording-type.ts`）给出，本工具不另写一套。

**本次读数**：服务器标志 `is_hltv` = TV **12** / 玩家 **1570**；
产品判决 = pov **1570** / sourcetv **8** / unknown **4** / unreadable **0**；
**权威判据覆盖 1582/1582**。

两条数字不一致的 4 条，是**产品的矛盾守卫**在起作用：`is_hltv=1` 与一个玩家味的 `clientName`
同时成立时，产品返回 `unknown` + `requiresUserChoice`，而不是硬选一个。这是产品设计，不是本工具的缺陷：

- `autorecord_2026-06-04_22-03-42.dem` → 判决 `unknown`（low），`clientName="皮革warrior"`
- `autorecord_2026-06-04_22-06-40.dem` → 判决 `unknown`（low），`clientName="皮革warrior"`
- `autorecord_2026-06-19_20-54-44.dem` → 判决 `unknown`（low），`clientName="皮革warrior"`
- `autorecord_2026-08-21_20-55-22.dem` → 判决 `unknown`（low），`clientName="SpawN"`

**变异证据（证明这条守卫不是摆设）**：把 `serverInfo` 整个置空后重跑，这 4 条会从 `unknown` 变成 **`pov`** ——
即把 SourceTV 录像判成了验收要找的那一类。所以 `usable` 只认服务器标志，不认判决。

## 判据字段：两类各一条样本

**POV（最长）**

| 字段 | 值 |
|---|---|
| 文件 | `autorecord_2026-08-08_21-43-59.dem` |
| 地图 / 时长 / 大小 | pl_upward_f12 / 4592.0 s / 146.9 MB |
| `isSourceTV` | `false` |
| `playerSlot` | `8` |
| `maxPlayers` | `25` |
| `server` | `Matcha Bookable` |
| `sky` | `sky_upward` |
| `clientName` | `Sinister Mark` |

**SourceTV（最长）**

| 字段 | 值 |
|---|---|
| 文件 | `SUNSHINE.dem` |
| 地图 / 时长 / 大小 | cp_sunshine / 1926.5 s / 82.8 MB |
| `isSourceTV` | `true` |
| `playerSlot` | `0` |
| `maxPlayers` | `25` |
| `server` | `MatchaTV` |
| `sky` | `sky_tf2_04` |
| `clientName` | `SourceTV Demo` |

## 可立即用于验收的样本

`usable` = 「`is_hltv=0` + 时长 ≥ 60 s + 权威判据」。
**光有 POV demo 不够，还得有对应地图**，所以下表只列本地确实有 BSP 的地图
（BSP 目录：`D:\SteamLibrary\steamapps\common\Team Fortress 2\tf\maps`，共 249 张）。

可用 POV 共 **1341** 个，覆盖 **96** 张地图；其中 **79** 张地图本地有 BSP：

| 地图 | 最长的可用 POV | 时长 | 大小 | `playerSlot` |
|---|---|---|---|---|
| `pl_patagonia` | `autorecord_2026-09-20_15-38-38.dem` | 60.8 min | 149.9 MB | 14 |
| `cp_snakewater_final1` | `autorecord_2026-09-27_21-44-41.dem` | 58.5 min | 84.3 MB | 12 |
| `pl_thundermountain` | `autorecord_2026-06-25_15-43-38.dem` | 56.6 min | 136.1 MB | 6 |
| `cp_gullywash_final1` | `autorecord_2026-08-26_22-07-39.dem` | 56.1 min | 71.4 MB | 1 |
| `cp_sunshine` | `autorecord_2026-05-17_16-32-36.dem` | 55.4 min | 75.9 MB | 8 |
| `cp_process_final` | `autorecord_2026-09-30_22-14-37.dem` | 52.1 min | 66 MB | 10 |
| `cp_reckoner` | `autorecord_2026-06-26_21-49-14.dem` | 47.9 min | 55.3 MB | 6 |
| `mvm_mannhattan` | `autorecord_2026-06-06_14-57-20.dem` | 44.6 min | 79.2 MB | 25 |
| `pl_enclosure_final` | `autorecord_2026-09-27_22-46-48.dem` | 44.1 min | 91.7 MB | 11 |
| `cp_premuda` | `autorecord_2026-07-27_13-42-33.dem` | 38.5 min | 80.1 MB | 6 |
| `mvm_rottenburg` | `autorecord_2026-06-06_14-18-39.dem` | 37.1 min | 65.3 MB | 2 |
| `pl_odyssey` | `autorecord_2026-08-09_14-48-38.dem` | 36.9 min | 82.1 MB | 8 |
| `pl_frontier_final` | `autorecord_2026-09-10_13-27-06.dem` | 36.8 min | 77.9 MB | 20 |
| `pl_citadel` | `autorecord_2026-08-30_18-01-17.dem` | 36.7 min | 78.2 MB | 14 |
| `pl_barnblitz` | `autorecord_2026-08-24_23-15-50.dem` | 36.4 min | 75.7 MB | 11 |
| `pl_borneo` | `autorecord_2026-09-01_16-44-41.dem` | 36.1 min | 72.9 MB | 22 |
| `pl_badwater` | `autorecord_2026-06-28_23-53-45.dem` | 35.9 min | 70.1 MB | 4 |
| `pl_goldrush` | `autorecord_2026-08-27_13-42-37.dem` | 34.3 min | 65.2 MB | 2 |
| `pl_upward` | `autorecord_2026-07-23_12-57-17.dem` | 34.1 min | 73.7 MB | 5 |
| `pl_pier` | `autorecord_2026-05-30_14-00-52.dem` | 33.8 min | 63.2 MB | 4 |
| `pl_swiftwater_final1` | `autorecord_2026-08-12_23-11-18.dem` | 33.6 min | 74.9 MB | 4 |
| `pl_aquarius` | `autorecord_2026-08-03_23-29-31.dem` | 33.2 min | 66.2 MB | 10 |
| `pl_phoenix` | `autorecord_2026-09-09_22-13-29.dem` | 32.9 min | 74.7 MB | 14 |
| `pl_embargo` | `autorecord_2026-06-09_20-06-32.dem` | 30.4 min | 69.2 MB | 9 |
| `pl_redwood` | `autorecord_2026-07-22_21-21-35.dem` | 29.1 min | 65.3 MB | 12 |
| `cp_mojave` | `autorecord_2026-07-15_14-25-14.dem` | 28.5 min | 59.1 MB | 9 |
| `pl_venice` | `autorecord_2026-08-04_12-55-36.dem` | 27.7 min | 54 MB | 14 |
| `koth_harvest_final` | `autorecord_2026-09-17_18-09-24.dem` | 27.7 min | 49.8 MB | 12 |
| `pl_snowycoast` | `autorecord_2026-06-24_17-40-06.dem` | 25.9 min | 53.3 MB | 4 |
| `pl_camber` | `autorecord_2026-07-23_23-18-57.dem` | 25.8 min | 57.7 MB | 9 |
| `pl_cashworks` | `autorecord_2026-08-15_13-34-05.dem` | 25.7 min | 54.9 MB | 2 |
| `pl_emerge` | `autorecord_2026-08-19_11-55-40.dem` | 22.3 min | 45.1 MB | 12 |
| `koth_lakeside_final` | `autorecord_2026-09-12_20-09-44.dem` | 17.5 min | 41.2 MB | 21 |
| `pl_breadspace` | `autorecord_2026-08-20_11-40-23.dem` | 16.7 min | 35.2 MB | 3 |
| `koth_megaton` | `autorecord_2026-07-20_20-24-19.dem` | 15.8 min | 34.9 MB | 13 |
| `koth_king` | `autorecord_2026-07-03_19-42-05.dem` | 15.6 min | 30.8 MB | 16 |
| `koth_brazil` | `autorecord_2026-07-23_12-26-36.dem` | 15.1 min | 26.5 MB | 10 |
| `cppl_gavle` | `autorecord_2026-05-28_10-38-11.dem` | 14.9 min | 27.3 MB | 18 |
| `pl_rumford_event` | `autorecord_2026-08-03_23-04-09.dem` | 14.8 min | 30.6 MB | 3 |
| `koth_dryfield` | `autorecord_2026-07-10_11-40-10.dem` | 14.3 min | 24.7 MB | 7 |
| `koth_sawmill` | `autorecord_2026-05-24_10-24-28.dem` | 14.3 min | 27.7 MB | 6 |
| `koth_viaduct` | `autorecord_2026-09-13_18-54-42.dem` | 12.6 min | 24.8 MB | 11 |
| `ctf_turbine` | `autorecord_2026-08-20_10-54-18.dem` | 12.4 min | 19.9 MB | 16 |
| `koth_lazarus` | `autorecord_2026-08-31_13-56-16.dem` | 11.4 min | 23.1 MB | 11 |
| `koth_camp_saxton` | `autorecord_2026-07-17_20-08-23.dem` | 11.0 min | 21.3 MB | 6 |
| `koth_shorelight` | `autorecord_2026-07-18_21-38-26.dem` | 10.7 min | 19.9 MB | 19 |
| `cp_steel` | `autorecord_2026-07-18_20-46-07.dem` | 9.9 min | 13.3 MB | 4 |
| `koth_rotunda` | `autorecord_2026-05-24_10-14-35.dem` | 9.6 min | 22.4 MB | 6 |
| `koth_nucleus` | `autorecord_2026-07-13_14-25-51.dem` | 9.6 min | 19.1 MB | 5 |
| `koth_badlands` | `autorecord_2026-08-23_18-30-34.dem` | 9.1 min | 16 MB | 9 |
| `koth_winter_ridge` | `autorecord_2026-07-15_23-37-03.dem` | 9.0 min | 16.9 MB | 4 |
| `ctf_haarp` | `autorecord_2026-07-18_22-06-42.dem` | 8.9 min | 18.3 MB | 22 |
| `cp_degrootkeep` | `autorecord_2026-09-13_22-42-59.dem` | 8.3 min | 15.8 MB | 7 |
| `cp_metalworks` | `autorecord_2026-09-13_22-34-30.dem` | 8.2 min | 14.3 MB | 14 |
| `koth_boardwalk` | `autorecord_2026-07-18_13-32-49.dem` | 7.9 min | 19 MB | 9 |
| `koth_cascade` | `autorecord_2026-05-24_10-40-03.dem` | 7.5 min | 16.2 MB | 18 |
| `cp_canaveral_5cp` | `autorecord_2026-08-21_22-45-07.dem` | 7.5 min | 11.3 MB | 11 |
| `koth_sharkbay` | `autorecord_2026-07-10_11-54-49.dem` | 7.3 min | 20.8 MB | 4 |
| `cp_foundry` | `autorecord_2026-08-29_19-20-29.dem` | 6.9 min | 10.6 MB | 10 |
| `koth_overcast_final` | `autorecord_2026-07-03_19-58-03.dem` | 6.9 min | 19 MB | 16 |
| `cp_yukon_final` | `autorecord_2026-08-31_23-01-55.dem` | 5.9 min | 8.8 MB | 7 |
| `cp_granary` | `autorecord_2026-07-13_16-01-35.dem` | 5.9 min | 9.5 MB | 8 |
| `cp_powerhouse` | `autorecord_2026-09-09_21-50-13.dem` | 5.5 min | 7.7 MB | 8 |
| `2koth_abbey` | `autorecord_2026-08-03_23-23-37.dem` | 5.5 min | 8.1 MB | 9 |
| `koth_cachoeira` | `autorecord_2026-07-22_14-06-28.dem` | 5.2 min | 9 MB | 7 |
| `cp_vanguard` | `autorecord_2026-09-08_22-48-21.dem` | 4.9 min | 9.2 MB | 17 |
| `koth_suijin` | `autorecord_2026-07-24_12-12-41.dem` | 4.7 min | 9.5 MB | 13 |
| `cp_well` | `autorecord_2026-09-17_17-54-06.dem` | 4.3 min | 6.3 MB | 13 |
| `ctf_2fort` | `autorecord_2026-06-19_15-59-42.dem` | 3.8 min | 6.3 MB | 15 |
| `cp_badlands` | `autorecord_2026-08-14_11-14-39.dem` | 3.8 min | 5.8 MB | 18 |
| `pl_hoodoo_final` | `autorecord_2026-07-06_12-58-28.dem` | 3.5 min | 4.8 MB | 7 |
| `cp_coldfront` | `autorecord_2026-08-21_22-17-55.dem` | 3.4 min | 5.4 MB | 10 |
| `koth_probed` | `autorecord_2026-08-22_20-42-43.dem` | 3.3 min | 8 MB | 9 |
| `cp_dustbowl` | `autorecord_2026-07-30_13-15-32.dem` | 3.2 min | 6.1 MB | 8 |
| `cp_freight_final1` | `autorecord_2026-07-29_13-47-20.dem` | 3.1 min | 3.9 MB | 8 |
| `cp_standin_final` | `autorecord_2026-07-26_16-23-32.dem` | 2.0 min | 3.7 MB | 1 |
| `ctf_sidewinder` | `autorecord_2026-07-13_14-54-31.dem` | 1.4 min | 2.5 MB | 0 |
| `koth_snowtower` | `autorecord_2026-09-09_23-30-47.dem` | 1.3 min | 3 MB | 6 |
| `cp_junction_final` | `autorecord_2026-08-02_21-37-43.dem` | 1.2 min | 2.9 MB | 20 |

**没有把任何 demo 复制进仓库**：上表全是路径引用。`testdata/` 已被 gitignore，没有绕过。

## 本遍测不到的（如实标出）

- 快照广度 / svc_setview：本遍只读 header 与 signon 块，不读 dem_packet 快照
- demPacketFooterValues：TF2 协议 3 没有包尾字段（DemoScanner 亦如此声明）
- 字符串表 clientside 标记：本遍不解析字符串表
- **`usable` 不等于「内容已人眼确认」**：它只说「够长 + 服务器标志说不是 TV」。画面里到底有没有正常的第一人称视角，要跑一遍才知道。
- **`playerSlot` 不是判据**：本次 1570 个 POV 里 53 个的 `playerSlot` 是 0，而 12 个 SourceTV 也全是 0。
  0 是**合法的玩家槽位**（取值 0..25 都有分布），所以它只是与 TV 巧合，不能拿来代替 `is_hltv`。
- **`DEM_DATATABLES` 陷阱在本实现里不可达**：`svc_serverinfo` 全部落在签名块第 1 个命令
  （分布 {"1":1582}），扫描在读到它时就取消，永远走不到 datatables。
  该陷阱是**设计时要绕开的坑**，不是当前代码的缺陷 —— 变异 M3 因此是 no-op，不作有效证据。

## 复核方式

```bash
# 全量重跑（约 1–5 s，只读每个文件的前约 2 MB）
node node_modules/vite-node/vite-node.mjs scripts/pov-scan.ts \
  "D:/SteamLibrary/steamapps/common/Team Fortress 2/tf/demos" \
  --maps="D:/SteamLibrary/steamapps/common/Team Fortress 2/tf/maps" \
  --out=release-pov-scan/pov-scan.json --evidence=docs/evidence/pov-candidates.md

# 只看某一类
node node_modules/vite-node/vite-node.mjs scripts/pov-scan.ts <dir> --list=sourcetv --list-limit=20
```
