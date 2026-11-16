# T62 map-material-matrix: 语料登记与失联输入（2026-11-16）

状态：注册表已落地并自测通过；**完整重跑被 host 锁占用阻塞**，读数刷新见第 6 节。

## 1. 问题

`map-material-matrix.sh` 用三条 POV 录像钉住三张图的材质读数（改动前 :126-130）：

| 地图 | 钉住的录像 | 字节数 | ticks | 身份凭据 |
| --- | --- | --- | --- | --- |
| koth_bagel_rc12 | `autorecord_2026-05-14_21-28-01.dem` | 51,759,919 | 153,502 | `evidence/header-scan/header-scan.csv:10` |
| cp_sunshine | `autorecord_2026-05-14_23-09-16.dem` | 51,337,166 | 144,583 | `evidence/header-scan/header-scan.csv:14` |
| pl_odyssey | `autorecord_2026-05-26_23-14-06.dem` | 27,718,172 | 55,294 | `evidence/header-scan/header-scan.csv:131` |

三份文件都已不在 `$TF/demos`（该目录现有 2063 个文件，最早 2026-08-02）；三张地图的 BSP
都还在。三条 header 行仍保留 recording=POV、地图名、字节数与 tick 数，
`evidence/t56-map-audit/demo_headers_tfdemos.jsonl`（:9 / :13 / :130）给出同一组字节与
tick 数，与 header-scan 一致。也就是说**身份没丢，字节丢了**。

## 2. 普查结论

对全机的录像做了普查（`tf/demos` 与各 demo 根），koth_bagel_rc12 与 pl_odyssey
**没有任何一份录像**；cp_sunshine 有一份同图 POV 录像。这两张图不能替换，只能登记为
缺输入；BSP 在，所以 `map-geometry-lighting.sh` 的几何门不受影响。

## 3. cp_sunshine 的替换件

`E:/demo测试/cp_sunshine/POV/autorecord_2026-05-17_16-32-36.dem`

- 75,905,225 B，sha256 `acc134094ef2ab1d69b8fdf606346a331ae8dc630d4ba634c682e2b448562b3a`
- header 的 map=cp_sunshine，recording=POV；219,069 包；index 覆盖全文件；四类失败 0
- 与原件不是同一段比赛（原件 144,583 ticks / 51,337,166 B），因此**读数不能与历史值逐位比较**，
  只能作为同图同录制类型的替代输入；替换件的 sha256 钉在门禁里，换文件/改名/改字节会变红。

## 4. 门禁改动

`map-material-matrix.sh`（+76/−2 行）：

- `REPLACED` / `REPLACED_SHA` / `REPLACED_NOTE` 三表：同图替换件及其身份。
- `BLOCKED_INPUT` 表：缺输入的地图与理由（含原文件字节数、tick 数、凭据行、普查结论、BSP 在）。
- `map_demo()` 先查 `REPLACED`；`map_demo_source()` 对替换件返回 "registered replacement"。
- 启动时逐条打印 `input override …` / `input blocked …`，override 不允许静默生效。
- 缺 demo 且已登记 `BLOCKED_INPUT`：写 `status=BLOCKED-INPUT reason=demo-not-found`，保留一行
  （8 列 + 39 NA + note = 48 字段，与表头一致），计数为 blocked，不算失败，不 probe；
- 缺 demo 且**未**登记：仍然是 failure（新增的图/新增的缺口不会被静默放过）；
- 替换件按 `REPLACED_SHA` 用 sha256 钉住，不一致即 failure；
- 汇总行改为 `MATRIX maps=%d rows=%d unsupported=%d blocked=%d failed=%d`。

未改动的部分：`map_bsp()`、相机/探针/读数逻辑、任何材质断言、失败语义。

## 5. 自测（已通过）

```
$ bash map-material-matrix.sh --only=pl_odyssey,koth_bagel_rc12
input blocked koth_bagel_rc12: …
input blocked pl_odyssey: …
map=pl_odyssey status=BLOCKED-INPUT …
map=koth_bagel_rc12 status=BLOCKED-INPUT …
MAP-MATERIAL-MATRIX=PASS maps=2 rows=0 unsupported=0 blocked=2 failed=0
```

两行都是 48 字段（`awk -F'	' 'NR==1{print NF}' evidence/t62-map-matrix/readings.tsv` = 48）。
另外把注册表与 `map_demo()`/`map_demo_source()` 抽出来做了离线路由测试：
cp_sunshine → 替换件路径、koth_bagel_rc12 → 原路径（由 BLOCKED 分支拦下）、
source 串含 "registered replacement"。`bash -n` 语法检查通过，CR=0。

## 6. 完整重跑状态

2026-11-16 15:45 起跑的完整门禁（三张可用图 × tick 0/2000）在
`== cp_snakewater_final1 ==` 处等待 host 锁：`/d/TF2_Native_Worktrees/_shared/host.lock`
被 owner=t62-mapaxes 反复占用，本卡的排队票长期是 head 却拿不到锁（`.scratch/t62-map-matrix`
为空，`evidence/t62-map-matrix/cp_snakewater_final1/camera-probe.txt` 于 15:54 被本卡创建）。
因此 `evidence/t62-map-matrix/readings.tsv` 尚未刷新到本次改动后的语料；已写入的两行
BLOCKED-INPUT 就是本次自测留下的读数。

**这不是推断**：锁状态与队列是 `hostlock.sh status` 的读数，录像普查与替换件探针是实测。
重跑必须在取得 host 锁后补做，且补做时不得用历史读数冒充。
