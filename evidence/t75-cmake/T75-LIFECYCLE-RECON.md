# T75 第 4 节对账 —— 安装清单里的两份许可证记录

Status: **LIFECYCLE-RECON-OK**。门禁修好后 `--forward` 15 用例 214 检查全绿、
`--mutation` 4 个变异体全部按名字捕获、三个源码逐个复原；产品源码一行未改。

基准提交 3a67c56（"T75 WIP: phase 2 build and gate verification"），分支 task/t75-cmake，
worktree D:/TF2_Native_Worktrees/t75-cmake。Phase 2 的构建/门禁报告见 T75-PHASE2.md
（不改写，它记录的是修门禁之前的读数），本文只处理那一次的唯一红项：第 4 节的三条断言。

## 0. 一句话结论

T75 phase 2 里 `install-lifecycle-check.sh --forward` 的三条红项不是产品漂移，是门禁第 4 节
的 pin 停在"许可证进包之前"的值。判定为门禁陈旧：产品把两份 WebView2 许可证记为 preserved
是设计（四种状态之一），而 pin 期望的是 preserved=0 / records=5 / "new unchanged"。
修的是门禁（77 插入 / 11 删除），产品源码未动；同时补了一个 M4 变异体，让这三条 pin
各有自己的红法。

## 1. 现象

T75 phase 2 第 6 步（2026-10-28，锁 t75-cmake-r1，267.6s，rc=1）：

```
cases=15 checks=213 fail=3
FAIL no file was already there to preserve = 2, expected 0
FAIL the manifest lists exactly the installed files, each once = 7, expected 5
FAIL every manifest record is a state word the product knows = new preserved unchanged, expected new unchanged
```

三条都在第 4 节（`--install` 到已经放着两份许可证的前缀），其余 14 个用例全绿。
读数本身是对的：`preserved=2`、清单 7 条记录、状态词 `new preserved unchanged`。

## 2. 根因：e5851c1 改了包，但这一节没重跑

| 时间 | 事件 | 对本节的影响 |
|---|---|---|
| T42（2026-10-21） | 生命周期门禁记录 `cases=15 checks=213, PASS`（T42-INSTALLER-VPK-2026-10-21.md:27） | 那时包里没有许可证，pin 的 0/5/"new unchanged" 是对的 |
| e5851c1（2026-10-22 22:49:35） | logstf：包 7 -> 11 个文件，payload 3 -> 4；顶层新增 WebView2Loader.dll 与 `third_party/webview2/{LICENSE,NOTICE}.txt` | 两份许可证从此在安装目录里，清单开始记 preserved=2 -- records=7 -- "new preserved unchanged" |
| T54 的绿名单 | `T54-RUN-DONE ... readiness=0`、`INSTALL-READINESS=PASS (cases=13 checks=145 mutations=0)`、`SOURCES-RESTORED=PASS` | **没有 INSTALL-LIFECYCLE 这一项**：这一轮没跑它，三个 pin 没人发现已经过期 |
| T75 phase 2（2026-10-28） | 门禁第一次在许可证进包之后运行 | 三条 pin 一起红 |

同一个提交里，第 4 节"installed file count = 8"的注释和计数已经改成了含两份许可证的版本
（`4 payload + VERSION.txt + manifest + 2 licence files`），只有 preserved / records /
state-word 三个 pin 留在旧值——这也解释了为什么红的是这三条，不是全部。

## 3. 判定：产品侧正确

产品的四状态写在 `native/include/install_manifest.h`（`new` :40、`changed` :45、
`unchanged` :48、`preserved` :52），语义写在 T37-INSTALLER-2026-10-20.md:98：

> 四种状态语义：`new`（本包新建，卸载删）、`changed`（本包覆盖过，卸载还原备份）、
> `unchanged`（本来就在且字节相同，不是本包的，不动）、`preserved`（本包没装过，
> 列出只为让"卸载删掉了它装的"成为对**完整清单**的陈述）。

许可证随包进安装目录、但安装器不拥有它 -> 记 preserved 正是这个状态存在的理由；
卸载不删它（`install_lifecycle.cpp:566-572` 跳过 preserved/unchanged）。所以产品不需要改，
要改的是门禁的三条 pin，以及那句已经不成立的注释（"they are not install records and the
manifest below does not claim them"）。

## 4. 改动（只改门禁）

`git diff --stat`：`install-lifecycle-check.sh | 88 +++++---`（77 insertions, 11 deletions）。
十个 hunk，按执行顺序：

| # | 位置（现文件行号） | 改动 |
|---|---|---|
| 1 | :61 | 头注释 "Three mutations rebuild the product" -> "Four mutations" |
| 2 | :290-306 | 第 4 节新增事实说明：许可证是包里的文档、在安装前就在目录里、preserved 是四种状态之一（引用 install_manifest.h 与 T37 第 4 节），并写明 e5851c1 改了包却没重跑本门禁 |
| 3 | :314 | `the two licence documents were already there and were preserved` = **2**（原 0） |
| 4 | :335 | `the manifest lists the five installed files and the two licence documents, each once` = **7**（原 5） |
| 5 | :341 | **新增**：只靠计数会接受任意两条野记录，所以把 preserved 记录的**路径**也钉住 —— `sed -n "s/^file=preserved\t[^\t]*\t[^\t]*\t//p"` 排序后必须正好是 `third_party/webview2/LICENSE.txt third_party/webview2/NOTICE.txt` |
| 6 | :349 | 状态词 = `new preserved unchanged`（原 `new unchanged`） |
| 7 | :1037-1075 | **新增 M4**：`install_lifecycle.cpp:352` 的 `if (ours.count(relative) != 0) continue;` -> `... \|\| relative.rfind("third_party/", 0) == 0) continue;`，让 preserved 扫描跳过 third_party/ 下所有文件 |
| 8-10 | :1050-1084 | M4 的四个断言（应用、构建且真的重链、运行的是变异二进制、三条 pin 各按名字变红）+ 逐字节复原 + 复原后重建；计数与判定行 `cases=4`、`MUTATIONS -eq 4`、`INSTALL-LIFECYCLE-MUTATION=PASS (4 mutations caught, sources restored)` |

修完的 forward 读数（本次实跑）：

```
  OK   the two licence documents were already there and were preserved = 2
  OK   the manifest lists the five installed files and the two licence documents, each once = 7
  OK   the only records the manifest adds for files it did not install are the two licence documents = third_party/webview2/LICENSE.txt third_party/webview2/NOTICE.txt
  OK   every manifest record is a state word the product knows = new preserved unchanged
  cases=15 checks=214 mutations=0
  INSTALL-LIFECYCLE=PASS
```

检查数 213 -> 214 的原因就是第 5 项新增的那条路径断言。

## 5. 本次实测

锁 `t75-lifecycle-r1`（`hostlock.sh run`，退出自动释放），命令
`bash .scratch/run-lifecycle-recon.sh`（实际跑的那份；入库副本见 §9，只差树定位方式与退出码），
输出目录 `.scratch/t75-lifecycle-recon/out`。

| 步骤 | 时间 | rc | 读数 |
|---|---|---|---|
| 取锁 ACQUIRED | 22:55:19 | 0 | owner=t75-lifecycle-r1 |
| `--forward` | 22:55:19 -> 22:58:49（约 210s） | **0** | cases=15 checks=214 fail=0，INSTALL-LIFECYCLE=PASS |
| `--mutation` | 22:58:49 -> 23:00:07（约 78s） | **0** | cases=4 checks=34 mutations=4，INSTALL-LIFECYCLE-MUTATION=PASS |
| 放锁 | 退出时自动 | 0 | status=FREE |

M4 的红法（日志原文，四条各自按名字）：

```
under M4: exit=0 preserved=0 records=5 states=new unchanged
  OK   M4 caught: the preserved pin reads 0, not 2 = 0
  OK   M4 caught: the record pin reads 5, not 7 = 5
  OK   M4 caught: the state-word pin reads new unchanged, not new preserved unchanged = new unchanged
```

M1/M2/M3 与 phase 2 时一致，仍全部捕获（`M1 caught: exit 0 with an empty redirect file`、
`M2 caught: the user's own file was deleted`、`M3 caught: a foreign file was accepted as an
install manifest`），四个变异体的复原重建都是 rc=0。

## 6. 复原与二进制的边界

- 三个被改的源码（main.cpp、install_lifecycle.cpp、install_manifest.cpp）每个变异体之后
  都 `cmp -s` 逐字节复原，日志里逐个打印 `source restored byte-identical = yes`；
  `=== mutation verdict ===` 再统一打印三条。
- 复原后重建出的 exe **不等于**变异前的参考 exe：sha256 链
  `f630e71400da`（forward 参考） -> `1207747de2a6` -> `72d71edfd01d` ->
  `818506156cde` -> `9cb9cce4038b` -> `84f3a0677ef8` -> `5564befb9550` ->
  `764d89fe9164` -> `54656bef3bee`（最后一次复原重建）。
  源码逐字节相同时重建仍得到不同 sha，说明构建里带着时间戳之类的非内容差异
  （PE 头）。所以这个门禁证明的是"**从复原后的源码重新链接出的二进制**"，
  不是"逐位可复现的构建"——两件事不要混。
- 本门禁不比对参考 exe 的 sha 与复原重建的 sha 是否相同，只比对"变异构建确实重链了、
  跑的是哪一份"（`mutated_binary_identity`）。

## 7. 未验证 / 边界

- **生命周期门禁不在 verify-all.sh 的 30 步里**（`grep -n 'install-lifecycle-check' verify-all.sh`
  无输出），所以最终那条链不会跑它；这份对账就是它这一轮的记录。要让它进入发布链是另一个决定，
  本文不改 verify-all.sh。
- 本次没有从零全量重建：构建树是 phase 2 留下的（T75-PHASE2.md 记录的 53/53 目标、errors=0），
  这里只做门禁本身需要的重建。从零构建的结论仍以 T75-PHASE2.md 为准。
- `checks=214` 是这棵树的读数；T42-INSTALLER-2026-10-21.md:27 与 T75-PHASE2.md:43/:127
  里的 213 是修门禁之前的历史读数，保留不改。
- 未跑 install-readiness-check.sh（phase 2 已绿，本次未触及它的输入）。
- 未做卸载/重装之外的产品行为验证；本节只覆盖安装清单的形状。

## 8. 复现命令

```
cd D:/TF2_Native_Worktrees/t75-cmake
bash /d/TF2_Native_Worktrees/_shared/hostlock.sh run t75-lifecycle-r1 \
  bash evidence/t75-cmake/lifecycle-recon/run-lifecycle-recon.sh
```

等价的手工两步（必须同一目录、先 forward 再 mutation）：

```
bash install-lifecycle-check.sh --forward  .scratch/t75-lifecycle-recon/out
bash install-lifecycle-check.sh --mutation .scratch/t75-lifecycle-recon/out
```

## 9. 证据文件

| 文件 | 内容 |
|---|---|
| evidence/t75-cmake/lifecycle-recon/forward-gate.log | 本次 `--forward` 全量输出（20,701 B，末尾 `cases=15 checks=214` / `INSTALL-LIFECYCLE=PASS`） |
| evidence/t75-cmake/lifecycle-recon/mutation-gate.log | 本次 `--mutation` 全量输出（3,814 B，含 M1-M4 的捕获与复原） |
| evidence/t75-cmake/lifecycle-recon/install.txt | 第 4 节 `--install` 的摘要行：`files=5 ... preserved=2` |
| evidence/t75-cmake/lifecycle-recon/installed-files.txt | 安装目录清单 8 行（5 个安装文件 + manifest + 2 份许可证） |
| evidence/t75-cmake/lifecycle-recon/install-manifest-files7.txt | 就是被 pin 的那个清单：`files=7`，5 条安装记录 + `file=preserved` 的两份许可证 |
| evidence/t75-cmake/lifecycle-recon/run-lifecycle-recon.sh | 本次 runner 的入库副本：从脚本位置定位树、`LIFECYCLE_OUT` 可覆盖输出目录、返回第一个非零 rc |
