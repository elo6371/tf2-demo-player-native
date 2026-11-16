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

2026-11-16 16:16 lead 裁定按 WINDOW-DEADLINE 收工、不再等锁：host 锁（owner=t62-mapaxes，`map-geometry` 在跑）与 verify 锁（owner=card-arch，E1 第 5 轮）都是合法持有，等不到窗口。因此第 5 节的自测是本文件里唯一的实测；完整重跑与 `readings.tsv` 刷新仍未做，下一窗口第一件事是取得独占 host 锁后重跑三张可用图。

## 7. 2026-11-16 重跑时发现的门禁自身缺陷（已修，实测）

第一次完整重跑（17:20 起）在每张图上都记了同一组失败：

```
cp_snakewater_final1 probe rc=2 (see probe-plan.txt)
cp_snakewater_final1 probe emitted no map.distinctMaterials
cp_sunshine probe rc=2
cp_sunshine: the demo header names map=NA, not cp_sunshine (…/cp_sunshine/POV/autorecord_2026-05-17_16-32-36.dem)
```

`evidence/t62-map-matrix/cp_snakewater_final1/probe-plan.txt` 里锁内命令的输出是 probe 自己的用法行：

```
ACQUIRED owner=t62-map at 2026-11-16 17:20:24
usage: material_coverage_probe --self-test | --fixture | --controls | --map <map.bsp> --tf-root <dir> …
LOCKED-COMMAND-EXIT rc=2 owner=t62-map
```

**不是语料的问题，是调用方式的问题。** 同一个二进制、同一组参数、同一时刻，直接从 shell 调用
`rc=0`（`map.distinctMaterials=148`），而经过门禁的 `bash -c "<%q 串>"` 调用 `rc=2`。把探针换成
`python -c 'import sys;print(sys.argv)'` 走同一条路径，拿到的是被 MSYS 重新转换坏的 argv：

```
['-c', '--map', 'D:\\SteamLibrary\\steamapps\\common\\Team\\',
 'Fortress 2tfmapscp_snakewater_final1.bsp', '--tf-root', …]
```

即 `--tf-root D:/…/Team Fortress 2/tf` 里的空格与 `/` 被拼成两个畸形 token；probe 读到未知参数，
打印用法、退出 2。触发条件是**被调用程序的路径本身是 POSIX 形式**（`$ROOT/native/build-nmake/…`
= `/d/TF2_Native_Worktrees/…`）且前面没有 `timeout` 之类的转换层。`demo_open_probe` 走同一条
路径，所以 `E:/demo测试/…` 也没能打开（`demo file cannot be opened` → `map=NA`）。

修法（`map-material-matrix.sh`，两行）：探针改用 Windows 形式路径，绕开这次转换。

```bash
PROBE=$(cygpath -m "$ROOT/native/build-nmake/material_coverage_probe.exe")
PRODUCT=$ROOT/native/build-nmake/tf2_demo_native.exe   # 不变：product 总在 timeout 之后启动
DEMO_PROBE=$(cygpath -m "$ROOT/native/build-nmake/demo_open_probe.exe")
```

product 不需要改：它的 trace 实测 `tf_root=D:\SteamLibrary\steamapps\common\Team Fortress 2\tf valid=1`，
因为 `timeout`（MSYS 程序）到原生进程的那一跳不做这次重写。

验证：修后同一张图的 `probe-plan.txt` 首行是 `{"reading":"map.distinctMaterials","value":"148"}`；
替换录像的 header 探针读出 `header=1 map=cp_sunshine server_map=cp_sunshine packets=219069`。
因此 `cp_sunshine` 的替换件是合格的，先前那条 "header names map=NA" 是同一个 argv 缺陷的回声，
不是语料问题。

