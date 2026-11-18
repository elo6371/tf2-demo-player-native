# D53 CR 假设普查 — 2026-11-15

**卡片**：D53 同族普查（lead 派工 m00840）　**执行**：t105-e5 / e17-e20-char　**性质**：只读普查，未改任何门禁脚本，未跑产品，未跑完整门禁。

**为什么做这张表**：N22 要把门禁接进 CI。一个「判决取决于 PATH 上哪个 awk 在前面」的门禁接进去就是随机红绿，比没有门禁更坏。这张表是「接 CI 之前必须先修哪些行」的清单。

**本文件与 docs/N17-N20-CHAR-COVERAGE-2026-11-15.md 的关系**：互不覆盖。那份是要素覆盖矩阵（N17/N18/N19/N20 四个需求），这份只回答一个问题——门禁解析文本时对换行做了什么未经证实的假设。

**本文件登记的缺陷**（lead 编号）：**D53**（§1，门禁在 CRLF 输入上静默判假红）／**D55**（§2.1，行尾字段导致静默假绿）／**D56**（§2.4，字段名根本不存在导致恒红）／**D57**（§8，门禁没钉播放速度，耗时不可预算且贴死持锁上限）。四个归同一个**门禁修复窗口**（lead m01063 排期），不分开碰同一批文件。

---

## 0. 物理前提：产品 trace 以 CRLF 落盘

这不是推测，是读源码得出的结论。产品 trace 的唯一写入点是 native/src/main_trace.cpp（104 行，T76 2026-10-29 从 main.cpp:236-318 原样搬出）：

- :25　`std::FILE* g_traceFile = nullptr;`
- :40　`std::fprintf(g_traceFile, "%.1f %s\n", traceNowMs(), what);`（在 traceCheckpoint 内）
- :85　`if (_wfopen_s(&g_traceFile, path, L"w") != 0) return;`
- 全文件对 `_setmode` / `O_BINARY` / `freopen` / `imbue` 的命中数：**0**。

std::FILE* 由 MSVC CRT 以**文本模式**打开，`\n` 落盘即 `\r\n`。所以：**每一个解析产品 trace 的门禁，读到的都是 CRLF 行尾。** main.cpp 里另有 :9379 materialDump、:9431 metricsFile 两处文件级 ofstream（另一条通道，是否文本模式未逐一核，本文按 trace 主通道立论）；traceFmt 调用点在 main.cpp 里有 270 处。

对照实测：evidence/t62-matrix/pose-families/scout-crouch-seq10.txt 工作树副本 24 行、24 行含 CR；同类文件 evidence/t26-world-holes/analysis/hole-account.txt 30 行、30 行含 CR，`git show HEAD:` 版本 0 行含 CR（即 CR 是检出/生成时加的，不是仓库内容）。
### 0.1 解释器是谁：本机实测与 CI 现状

§1 的判决依赖解释器，所以「谁在跑 awk」是这张表的前提之一，不能留白。实测（2026-11-15，工作树 D:/TF2_Native_Worktrees/t105-e5）：

| 查法 | 结果 |
|---|---|
| `command -v awk` | `/c/Users/Administrator/.dsh/dsh-ops/shells/niubash-prepared/e3765d43…/winuxcmd/usr/bin/awk.exe` |
| `type -a awk` | 只有上面这一条（PATH 上没有第二个 awk） |
| `bash -lc 'command -v awk'` | 同上（登录 shell 不换解释器） |

关键事实：**`/c/Program Files/Git/usr/bin/awk.exe`（会剥 CR 的那个）不在 PATH 上**——PATH 里 Git 只有 `C:/Program Files/Git/cmd`，而 Git 的 awk 在 `usr/bin`。所以在本机，所有解析 CRLF 的门禁用的都是**不剥 CR** 的那个 awk：§2 里每一条「会红」在本机都按真红算，D53 的 RED 可复现不是巧合。三种查法给同一答案，这一点可以当作稳定事实用。

CI 一侧——这是 lead 点名要查的地基，结论是**查不到，因为 CI 还不存在**：

- 仓库内**没有任何 CI 配置**。查过 `.github/**/*.yml`（0 文件）、`.gitlab-ci.yml`、`azure-pipelines.yml`、`Jenkinsfile`、`appveyor.yml`、`.circleci`（均 0 文件）。N22 要接的 runner 尚未存在，也就没有它的 PATH 可读。
- `verify-all.sh`（1544 行）**不设 PATH**：`export PATH` 与 `PATH=` 命中 0，它继承调用者的 PATH。顺带：该文件里 awk 只出现 1 次（:1189 `ps -W | awk '{print $NF}'`），不参与任何判决。
- 全仓非 evidence 的 `.sh` 里**没有任何一处硬编码 awk 路径**（`awk\.exe|/usr/bin/awk|gawk|GAWK` 命中 0）。会改 PATH 的只有三个构建脚本，加的是 MSVC 目录：`build-cmake.sh:37 export PATH="$MSVC_ROOT/bin/Hostx64/x64:$SDK_BIN:$PATH"`、`env-msvc.sh:8`、`resource-discovery-check.sh:228`——都不影响 awk。

所以只能写到这里：**本机可确定（不剥 CR，§2 的「会红」按真红算）；CI 不可确定，且不是「没查」而是「没有可查的对象」。** 按 lead 的口径，这里不猜。要确定它，唯一办法是在 runner 上执行一次 `type -a awk` 并把结果写进 CI 日志——这正是下面 §3 提议里 Tier 1 自检要顺手做的事（把 `awk_path=` 打进日志，让判决可归因）。

---

## 1. D53 本体：同一门禁、同一份输入，判决取决于哪个 awk 在跑

lead（m00840）已定因，本文复核一致：

- 本机有两个 GNU Awk 5.4.1：`/usr/bin/awk`（Git 版，读文件时做 CRLF 翻译）与 dsh niubash 的 `winuxcmd/usr/bin/awk.exe`（不翻译）。两者文件大小相同（4,647,424 B），版本字符串相同。
- 单行探针 `printf 'a=1\r\n'` 上 `{print length($0)}`：Git awk 得 3，niubash awk 得 4。
- 用 class-pose-family-check.sh:130 那段 awk 判决：
  - niubash awk + 原样含 CR → animpose_lines=4 bad=1（判非有限）
  - Git awk + 原样含 CR → animpose_lines=4 bad=0
  - niubash awk + `tr -d '\r'` → bad=0
  - gawk --posix + 原样含 CR → bad=0

本文复核：对 evidence/t26-world-holes/analysis/hole-account.txt 用同一段「按 = 切分后比对」的选取器，Git awk 得 MATCH（与 PIN_SEAM_MEMBERS 一致），PATH 上解析到的 awk 指向 niubash 目录。**判决差异可复现，输入字节一个没变。**

D53 的正确定义（沿用 lead 措辞）：

> 同一门禁、同一份输入，判决取决于 PATH 上哪个 awk 在跑。

补充边界（lead 已收回一半早前判断）：evidence/t62-matrix/pose-families/ 的历史 PASS 在会剥 CR 的解释器下今天仍可复现，所以它**不是**「门禁被偷偷改过」的证据；但它也**不能当门禁有效的证据**——它是个依赖环境的结论，换个 shell 就变红。git 事实：该路径 50 个文件被跟踪，提交史 1 次（0efd844，2026-11-04 18:34:06 +0800）；门禁脚本本身提交史也是 1 次，且是同一次提交。

---

## 2. 普查总表

口径说明：**「会红」= 该行的判据字段在行尾，且比较是字符串相等 ⇒ CR 使判决翻转。「假绿」= 翻转方向相反，门禁本该拦却永远放行。「安全」= 判据字段不在行尾，或比较前已数值化/已剥 CR。「未普查」= 本文没有读到产生该字段的那一行源码，或没有读到消费该值的那一行脚本，一律不猜。

### 2.1 A 类：awk `split($i, a, "=")` 之后做字符串相等比较

| 脚本:行号 | 判的是什么 | 会被 CR 打红 | 依据 |
|---|---|---|---|
| fog-check.sh:197-202 | fog_reason()：`sed 's/.* reason=//'` 取行尾自由文本，随后与 4 个期望串比（:267 `-`、:282 `default off; --world-fog turns it on`、:331 `-`、:358 `world geometry is filtered, so the fogged set is not the map`） | **会红** | 同文件 :194-196 注释原文写「The reason is free text and the LAST field on the line」；sed 模式无 `$` 锚定，删到行尾 ⇒ 带 CR ⇒ 4 条断言全不等 |
| class-pose-family-check.sh:134 | animpose 行 `finite=` 判 `!= "1"` | **会红（D53 本体）** | `finite=` 是 animpose 行最后一个字段（样本见 evidence/t62-matrix/pose-families/scout-crouch-seq10.txt:10-13）；gawk 默认 FS 不含 `\r` ⇒ a[2]="1\r" |
| player-animation-check.sh:362 | moving 行 `advanced` 判 `==0`，汇总成 bad_move | **假绿（方向相反）** | producer native/tools/model_pose_probe.cpp:748-750 的格式串以 `advanced=%d\n` 结尾 ⇒ adv="0\r"；非 strnum，与数字 0 比退化为字符串比较 ⇒ 恒不等 ⇒ bad_move 恒 0 ⇒ :364 的 assert_eq 永远通过 |
| player-animation-check.sh:363 | idle 行 `advanced` 判 `==1`，汇总成 bad_idle | **假绿（方向相反）** | 同上 ⇒ bad_idle 恒 0 ⇒ :365 永远通过 |

### 2.2 A 类续：取值带 CR，但消费侧已核到具体断言

| 脚本:行号 | 判的是什么 | 会被 CR 打红 | 依据 |
|---|---|---|---|
| playback-error-paths-check.sh:145 | `camera: mode applied=free-look` 的 `paused=`，:146 与 `1` 比 | **会红** | producer native/src/main.cpp:10027 `camera: mode applied=%s tick=%d paused=%d` ⇒ paused 行尾 |
| playback-error-paths-check.sh:151 | `camera: mark tick=` 的 `tick=`，:152 与 AB 和 -1 比 | **会红** | producer native/src/camera_script.cpp:404 `traceFmt("camera: mark tick=%d", ...)` ⇒ tick 行尾 |
| playback-error-paths-check.sh:174 | `camera: expect what=same-tick` 的 `match=`，:175 与 `1` 比 | **会红** | producer native/src/camera_script.cpp:412 `camera: expect what=same-tick marked=%d actual=%d match=%d` ⇒ match 行尾 |
| playback-error-paths-check.sh:217 | `audio: advance` 的 `dup=`，:218 与 `0` 比 | **会红** | producer native/src/main.cpp:9969-9973 两段拼接，末段 `" missing=%zu dup=%zu"` ⇒ dup 行尾 |
| playback-error-paths-check.sh:225 | `audio: timeline` 的 `scheduled=`，:226 与 `5323` 比 | **会红** | producer native/src/main.cpp:5946 `audio: timeline ... read=%zu scheduled=%zu` ⇒ scheduled 行尾 |
| playback-error-paths-check.sh:229 | `mode: summary` 的 `audio_device_opened=`，:230 与 `0` 比 | **会红** | producer native/src/main.cpp:12857 `mode: summary ... audio_mode=%s audio_device_opened=%zu` ⇒ 行尾 |
| playback-error-paths-check.sh:171 | `camera: snapshot label=before/after` 的 `tick=`，:172 三值互比 | **部分会红** | 有效快照行 native/src/camera_script.cpp:123 tick 不在行尾（安全）；**无效快照行 :115 `camera: snapshot label=%s valid=0 tick=%d` tick 是行尾**（会红） |
| world-geometry-hole-check.sh:1090 | `HOLE seam rank=` 行的 `left/right/up/down=`，:1092 `sub(/:[0-9]+$/, "", face)` 后与 PIN_SEAM_MEMBERS（:264）比 | **会红** | `$` 不匹配 CR 之前的位置 ⇒ 去掉 `:N` 失败 ⇒ face 仍带 `\r` ⇒ 成员串污染。产生方是 world-geometry-hole-account.py:494 `print("HOLE seam rank=%d point=%d,%d %s" % ...)`，`down=` 为行尾；actual 样本 evidence/t26-world-holes/analysis/hole-account.txt:21 末尾为 `down=4173:25\r\n`（已 od 确认） |
| map-material-matrix.sh:449 | `camera_yaw=` 宽提取后作为 `--debug-camera` 实参 | **取值带 CR，未见相等比较** | producer native/tools/demo_open_probe.cpp:932 `<< L" camera_pitch=" << angles[0] << L" camera_yaw=" << angles[1]` ⇒ yaw 行尾。用途 :480 写调试文本、:499 传给 strtod；strtod 遇 CR 停止 ⇒ 未见判红路径，**未实测** |
| third-person-weapon-check.sh:404 | `firstRefusal=` 宽提取 | **取值带 CR，无害** | producer native/src/main.cpp:11681 以 `firstRefusal=%s` 结尾 ⇒ 行尾。消费 :405 只回显、:477 只做 `[ -n ... ]` ⇒ 非空判断不受 CR 影响 |
| logstf-check.sh:325 | `runtimeKind=[^ ]* runtimeAvailable=[0-9]` | **提取安全** | 末字符类 `[0-9]` 不含 CR，且 runtimeKind 值后必须有空格；:328 用 `case ... in *"runtimeAvailable=1"*)` 子串匹配 ⇒ 安全。**消费方其余路径未普查** |

### 2.3 确定安全（判据字段不在行尾，或比较前已数值化/已剥 CR）

| 脚本:行号 | 判的是什么 | 会被 CR 打红 | 依据 |
|---|---|---|---|
| class-pose-family-check.sh:65/66 | `index=` / `label=` 只当 key 用 | 否 | 值原样带走但不做相等比较；:69-70 只做正则前缀匹配 |
| class-pose-family-check.sh:67 | `frames = a[2] + 0` | 否 | 强制数值化 |
| class-pose-family-check.sh:135 | `maxDelta` 比较 | 否 | `a[2] + 0` 双向数值化 |
| class-pose-family-check.sh:136 | `checksum=` 当关联数组 key | 否 | 只判重复，不判值 |
| class-pose-family-check.sh:88 | `grep -o "sequences=[0-9]*"` | 否 | 字符类不含 CR |
| class-pose-family-check.sh:125-126 | `grep -m1 "^animpose "` 后取 `label=[^ ]*`，与 `label=${want}` 比 | 否 | label 之后还有 frame=/localFrame=/sectionFrames=/bones=/checksum=/maxDelta=/finite= 共 7 个字段 ⇒ 不在行尾，`[^ ]*` 吃不到 CR |
| observer-focus-check.sh:304/306 | `resolved=` / `targetOrigin=` / `selfOrigin=` | 否 | producer native/tools/entity_model_probe.cpp:1447 `... status=%s resolved=%d states=%zu` ⇒ resolved 不在行尾；:1494/:1497/:1502 的两个 origin 也不在行尾；:308-309 立即 `split(...,",")` 数值化 |
| playback-control-check.sh:108-118 | step/advance/check 三类行的 status/ticks/ok | 否 | producer native/tools/playback_control_probe.cpp:55 status 不在行尾（其后还有 playing= speed=）；:169 ticks 不在行尾；:49 ok 后还有 detail 串。消费 :242 的相等比较因此安全 |
| world-material-semantics-check.sh:333-353 | `lfield()` 提取 + `af()` 用 `split($0, pair, "=")` | 否 | :332/:336/:338/:339 四个调用入口全部先 `| tr -d '\r'`；:347-352 只用 pair[1] 做键匹配，值走 `substr($0, index($0,"=")+1)` |
| stability-release-check.sh:156 / :220 | `while IFS='=' read -r k v` | 否 | 两个入口的上游 awk 都先 `{ sub(/\r$/, "") }`（:147 / :216）；read 本身不剥 CR，但拿到的是已剥的 |
| world-geometry-hole-capture.sh:313 | `eye=/pitch=/yaw=/src=` 宽提取 | 否 | 输入 :311 已 `| tr -d "$CR"`（:300/:302 同） |
| map-material-matrix.sh:438/439 | `map=` / `server_map=` 宽提取后与期望地图名相等比（:440/:442） | 否 | producer native/tools/demo_open_probe.cpp:705-718 中 map= 之后还有 6 个字段；:768-789 中 server_map= 之后还有 20 余个字段 ⇒ 都不是行尾 |
| world-geometry-hole-check.sh:1127/1128 | `first` / `second` 交换重组 | 否 | :1130/:1131 只重排不断言；`first[2] != second[2]` 是交换触发条件，两值都带 CR 时仍相等 ⇒ 只影响是否交换，不影响判决 |

### 2.4 字段名根本不存在（另一类缺陷，与 CR 无关，一并登记）

| 脚本:行号 | 取什么 | 后果 | 依据 |
|---|---|---|---|
| playback-error-paths-check.sh:154 与 :232 | `camera: summary` 的 `modeApplies=` | 恒取 `-`；:155 的 B-summary 永远红，:233 的 E-modeapplies 永远红 | producer native/src/main.cpp:12908 只打印 `mode=%s modeChanges=%u spacePresses=%u spaceSingles=%u`，没有 modeApplies 字段 |
| playback-error-paths-check.sh:227 | `mode: summary` 的 `events_seen=` | 恒取 `-`；:228 的 E-events 永远红 | producer native/src/main.cpp:12857 只打印 `window_created= window_visible= audio_mode= audio_device_opened=` |

---

## 3. 接 CI 之前必须先修的清单（按危害排序）

> **缺陷编号（lead 已分配，见 docs/DEFECT-REGISTER-ADDENDUM-D51-D56-2026-11-15.md）**：本节第 1 条 = **D55**；第 2 条 = **D53**；第 4 条里那两条「字段名根本不存在」= **D56**。三者归同一个「门禁修复窗口」一次做完（lead m01063 排期），不分开碰同一批文件。

1. **player-animation-check.sh:362/363**（= **D55**） — 唯二的「假绿」。它看起来在测「moving 必须 advance / idle 不得 advance」，实际上这两条断言永远成立。这不只是换行问题，是**一条从未生效的断言**，应优先定级。
2. **class-pose-family-check.sh:134** — D53 本体。当前 RED 是假的（数据是好的），而历史 GREEN 也是环境产物。修法二选一：awk 前统一 strip CR，或探针统一写 LF。注意这是**共享 gate helper**，改完所有跑过它的证据都要重跑。
3. **fog-check.sh:197-202** — 4 条断言会因 CR 全红；producer 自己在注释里写明 reason 是行尾自由文本，最容易被当成「产品坏了」。
4. **playback-error-paths-check.sh 的 6 个 CR 点 + 2 个不存在字段** — 8 条断言里 8 条都不可信，其中 2 条恒红与 CR 无关。
5. **world-geometry-hole-check.sh:1090** — 会产生门禁红，但产生方是 Python 脚本（world-geometry-hole-account.py:494）而非产品 trace，修法与上面几条不同。
6. **animation-drive-states-check.sh 没钉播放速度**（= **D57**，§8）— 它不是判错，是**代价错**：单趟 36.3 分钟、贴死单次持锁 <40 min 的铁律，且耗时由 demo 长度决定、门禁从不打印。接 CI 前必须钉速度，否则一个换 demo 就会让 CI 任务超时且不报错。
7. 其余（map-material-matrix.sh:449、third-person-weapon-check.sh:404、logstf-check.sh:325）**不需要修**，登记在案是为了「接 CI 时不要误判它们是同类」。

一条通用建议：与其逐个补 `tr -d '\r'`，不如在 gate helper 的公共入口统一做一次规范化的同时，**给门禁加一条自检**——喂一行已知带 CR 的合成输入，断言解析结果与不带 CR 时相同。这样 D53 这类缺陷会被门禁自己抓到，而不是靠人肉普查。
### 提议：解析器自检（待批准）

lead 采纳「门禁自己测自己能不能被 CR 打瞎」这条，需要落成可执行规格。以下是提议，**尚未实现，等批准**。

#### 问题的一般形式

不是「有些门禁忘了 `tr -d '\r'`」，是**门禁的解析器没有声明它的输入契约**：它假设每行以 `\n` 结束，而生产中每行以 `\r\n` 结束，这个假设既没写下来也没被测过。所以修法不能是逐行补 `tr -d '\r'`——那只是把同一个未声明的假设换个地方藏。

#### 放哪里

新建**一个共享 helper** `gate-selftest.sh`，由各 check `source` 进来，不单独跑。两个理由：D53/D55 都是 gate 侧共享面的缺陷，自检若散在每个 check 里，下次仍会漏掉某个 check；而自检本身应该被 N22 的 CI 统一调用。

边界：**新增文件不属于「修改共享 gate helper」**，所以第一步不落进独占写域那条串行规则；一旦现有 check 开始 `source` 它，就进入了。因此分两步（见「接法」）。

#### 两个函数

```bash
# gate-selftest.sh -- 契约：被 source，不单独执行。
#
# gate_selftest_parser <name> <parser-cmd...>
#   在临时目录造两份只差行尾的语料（LF / CRLF），对两份各跑一次
#   <parser-cmd>，要求 stdout 逐字节相同。
#   返回 0 = 相同；1 = 不同（打印 diff）；3 = 证据不足（见下）。
#
# gate_selftest_selfcheck
#   自检的自检：用同一入口跑一个**已知会被 CR 打瞎**的解析器，
#   要求它返回 1。跑不出 1 就说明自检本身瞎了，整条链作废。
```

命名理由：叫 `gate_selftest_parser` 而不是 `assert_no_crlf`——它断言的是「解析器对行尾不敏感」，不是「输入里没有 CR」。后者会把正当的 CRLF 输入判成错误（产品 trace 本来就是 CRLF），前者才是要的性质。

#### 语料怎么造（幂等性）

```
tmp=$(mktemp -d)
printf 'animpose cycle=0.0000 sequence=10 label=X frame=0/21 finite=1\n' > "$tmp/lf.txt"
sed 's/$/\r/' "$tmp/lf.txt" > "$tmp/crlf.txt"      # 派生，不手写
cmp -s "$tmp/lf.txt" "$tmp/crlf.txt" && fail        # 派生没生效就是自检坏了
```

三条硬要求：

1. **LF 是唯一真源**，CRLF 由 `sed 's/$/\r/'` 派生。手写两份会漂移；派生让「只差行尾」这件事本身可验证（`cmp` 不相等才算派生成功）。
2. **语料里必须有一个值在行尾**（上面 `finite=1` 就在行尾，即 D53 的形状）。拿字段在中间的样例去测，测的是不存在的问题，会一路绿到底——那本身就是一条假绿。
3. **不碰产品**：只在 `mktemp -d` 里读写，退出 `rm -rf`。不读写 `evidence/`、不读写 trace、不启产品、不占 host lock。因此可以进 CI、可以并行跑、可以在任何锁状态下跑。

#### 它自己怎么防止退化成假绿（lead 点名的套娃问题）

四条，缺一条就没有意义：

1. **自检必须先证明自己会失败**（`gate_selftest_selfcheck`）。用一个已知会被 CR 打瞎的解析器（就是 `awk -F= '{ if ($2 != "1") bad=1 } END { print bad+0 }'` 这种 D53 本体形状）跑一遍，**必须返回 1**。返回 0 就等于说「这个自检抓不到 D53」——那它自己就是一条假绿，整条链不许用。这与仓库既有的负控惯例（docs/READ-THE-PLAYBOOK-FIRST.md：没见它红过的判据不是证据）是同一条规矩。
2. **空输出不算通过**。两次都产出 0 字节时 `cmp` 也相等，那是「什么都没测」。所以 `gate_selftest_parser` 必须先断言两次输出都非空，否则返回 3（证据不足），**调用方必须把 3 当失败**，不许当通过。这一条是 D55 的教训直接转化来的：假绿的本质就是把「没测到」当成了「测过了」。
3. **逐字节比，不比语义**。用 `cmp -s`，不要用「diff 之后看空不空」这类会吞掉差异的写法。
4. **判决必须可归因**：自检日志里打印 `awk_path=$(command -v awk)` 与 `awk_version=$(awk --version 2>/dev/null | head -1)`。D53 的根因是解释器身份从头到尾没被记录；一个不记录解释器身份的「已通过」，与 §1 里那个依赖环境的历史 PASS 是同一类东西。

#### 接法（两步，都不在本轮）

- **第一步**：落 `gate-selftest.sh` + 一份只跑自检的 `gate-selftest-check.sh`（不需要产品、不需要锁、不需要 demo）。它在 CI 里就是第一个 gate。
- **第二步**：现有 check 逐个 `source gate-selftest.sh`，在解析产品输出之前调 `gate_selftest_parser`。这一步动共享 helper，按串行规则等放锁后单派；改完所有跑过它的证据都要重跑。

#### Tier 2（可选，更便宜的大面积体检）

对**已有捕获日志**的 gate，不必重跑产品：把日志 `tr -d '\r'` 后跑同一套断言，要求判决不变。这能在不占锁、不启产品的前提下，一次性回答「历史上哪些 PASS 是换行撑起来的」。代价是只覆盖已有日志里出现过的字段，覆盖不到新代码路径。

#### 这条提议不解决什么

自检保证「同一个解析器对 LF 与 CRLF 给同一答案」，**不保证答案是正确答案**。一个字段名根本不存在的解析器（D56 那两条）在两个行尾下都一样取不到值，会顺利通过自检然后继续恒红。所以「行尾自检」与「字段存在性检查」是两件事，都要有；不要以为自检上线就能把 D56 一起关掉。

---

## 4. 这张表不证明什么

> **本表只回答「读的一侧对换行做了什么假设」，没有回答「修了没有」。** 所有「会红」都是**静态推断 + producer 字段位置核对**得出的，除 D53 本体与 seam 选取器外，本文**没有实跑任何门禁**（lead 明令：不跑完整门禁，hostlock 在 ui-d 手里跑 cp_sultry_b8a 抓帧，重负载会污染其 trace/metrics）。

具体不证明：

1. 未实测「会红」的行在真实 CRLF 输入上确实红。依据是字段位置 + 比较语义，不是跑出来的。
2. 未证明除本表所列之外没有别的同类假设。普查口径是「awk split(=) 后字符串比较」与「grep -o 宽提取」两族，**用 sed/read/cut/IFS 的其它写法未被穷举**。
3. 未证明改动任何一条门禁后其余断言仍成立。
4. 「安全」只表示 CR 不会让这一行判错，不表示这一行判得对。

---

## 5. 未普查项（登记，不猜）

1. **模型是否也在 CRLF 上出问题**：本表只管门禁脚本，产品代码里读自己写的 trace 的地方未普查。
2. **native/src/main.cpp:9379 materialDump 与 :9431 metricsFile 的换行模式**：只确认它们是文件级 ofstream，未确认是否二进制模式。
3. **playback-error-paths-check.sh 其余断言**：本文只核了 fnum/firstf 的 14 个调用点，其它 `has` / `nhas` / `grep -qF` 类整串匹配按 lead 口径归为安全，未逐条读。
4. **logstf-check.sh:325 之后的消费链**：只核到 :328 的 case 匹配，其后是否还有相等比较未读。
5. **`HOLE seam rank=` 之外 world-geometry-hole-check.sh 的读取入口**：只核了 :1040 这一条 grep，同文件其它 grep 未普查。
6. ~~**门禁在 CI 上的 PATH**~~ → **已查，见 §0.1**。本机确定（不剥 CR，§2 每条「会红」按真红算）；CI 侧「不可确定」不是没查，而是**没有可查的对象**——仓库里没有任何 CI 配置，runner 还不存在。唯一补法：runner 上跑 `type -a awk` 并写进 CI 日志。**不猜。**

---

## 6. 复现命令

两个 awk 的判据差异（不需要产品、不需要锁）：

```
printf 'a=1\r\n' > /tmp/crlf-probe.txt
"/c/Program Files/Git/usr/bin/awk.exe" '{print length($0)}' /tmp/crlf-probe.txt   # 3
awk '{print length($0)}' /tmp/crlf-probe.txt                                      # 4（niubash 版在 PATH 前面时）
```

D53 本体（只解一个模型的一个序列，不跑完整门禁）：

```
PROBE=build/native/Release/model_pose_probe.exe
$PROBE --player-animation-test --tf-root <tf> --model models/player/pyro.mdl \
        --sequence 15 --ticks 4 > /tmp/pose.txt 2>&1
awk '/^animpose /{for(i=1;i<=NF;i++) if ($i ~ /^finite=/) { split($i,a,"="); if (a[2] != "1") bad=1 } } END{print bad+0}' /tmp/pose.txt
# 原样 -> 1（假红）；tr -d '\r' 后 -> 0
```

seam 选取器（Git awk 与 PATH awk 判决相反，见 §1）：

```
F=evidence/t26-world-holes/analysis/hole-account.txt
git show HEAD:$F | grep -c $'\r'   # 0
grep -c $'\r' $F                  # 30
```

---

## 7. 归档事实（仓库状态与行号锚点）

- 工作树 D:/TF2_Native_Worktrees/t105-e5，分支 candidate/e5。**当前 HEAD = `6dd853e`**（§§0-8 落盘时的 `a901ece` 已过时；§9/§10 的每个行号都在 `6dd853e` 上重新核过）。
- `git status --porcelain -- native/src/main.cpp native/src/playback_control.cpp` 在 `6dd853e` 上无输出，两个文件干净，所以本文的 `native/src/main.cpp` 行号可移植到该 HEAD（§§0-8 写作时的「该文件正被 n12-camera 修改」已不成立）。
- 本次普查**没有修改任何一个字符的代码**，没有新增/删除任何门禁脚本。§9 的普查脚本全部落在仓外 `D:/_shared/EXEC-2026-11-15/census-d59*.sh`，只读、不跑产品。
---

## 8. D57：门禁没有把播放速度钉住（同族，风险低于 D53/D55/D56）

> **D57（lead 编号）：门禁没有把播放速度钉住，于是它的耗时由 demo 的 tick 率和 tick 目标共同决定，且从不记录。**

### 8.1 定位

`animation-drive-states-check.sh` 里 **`playback-speed` / `PLAYBACK_SPEED` / `playbackSpeed` 命中 0 次**（全文 grep）。`run_capture`（:125-150）启动产品的完整命令行是：

```
TF2_NATIVE_TRACE="$trace" timeout "$TIMEOUT" "$EXE" --tf-root "$TF" \
  --settings-file "$ISO_SETTINGS" \
  --ui-settings-file "$ISO_UI" \
  --audio-device "$AUDIO_DEVICE" --demo "$demo" --capture-tick "$tick" \
  --capture-frame "$bmp" --metrics-file "$OUT/$name.csv" \
  --player-animation "$@" > "$log" 2>&1
```

**没有 `--playback-speed`。** 于是产品走默认值：`native/include/playback_state.h:25` `double speed = 1.0;`；`native/src/main.cpp:1959` `double playbackSpeed = 0.0;`，`--playback-speed` 不传时保持 0.0，`:5977-5981` 的 `if (playbackSpeed > 0.0) { g_playback.speed = std::clamp(playbackSpeed, 0.125, 64.0); }` 整段被跳过。

播放推进是**按墙钟、单 tick 步进**的：`playback_control.cpp:307` `accumulator_ += elapsedSeconds * tickRate_ * speed_;`，`:310-316` `while (accumulator_ >= 1.0) { … cursorTick_ += direction; … accumulator_ -= 1.0; }`。`tickRate_` 来自 `native/src/demo_header_core.cpp:294-303` `demoTickRate()`（`ticks / playbackTime` 夹在 30–128，不可用时退回 66.6666667）。

### 8.2 精确算术（本次实测）

9 次运行的 `--capture-tick`：

| 运行 | tick |
|---|---|
| default | 16780 |
| legacy-crouch | 14765 |
| legacy | 16780 |
| states-flinch | 16780 |
| states-flinch-repeat | 16780 |
| states-crouch | 14765 |
| states-crouch-b | 14800 |
| states-cover | 20000 |
| early | 12000 |
| **合计** | **143450** |

**143450 ticks ÷ 66.65 ticks/s = 2152 s = 35.9 分钟纯播放**，加 9 次进程启动约 **36.3 分钟**。单次持锁铁律 **<40 min**，只剩 3.7 分钟余量。

单次运行不会撞超时：`TIMEOUT=420`（:100），最大的一次是 `cover@20000` = 300 s。**问题不在超时，在总账。**

66.65 ticks/s 这个数**是倒算出来的，门禁自己从不打印它**：仓库里 t84 的历史 metrics `evidence/t84-animdrive/animation-drive-states/default.csv` 末行 `elapsed_seconds=251.772, tick=16780` → 16780 / 251.772 = **66.65 ticks/s**，正好 1× 实时。

### 8.3 对照：同一批 gate 里，一个钉了速度，一个没钉

这是 D57 最好的证据——不是「大家都不小心」，是**同一批门禁里有的钉了有的没钉，而没有任何一条规则要求钉**。

| 门禁 | 是否传 `--playback-speed` | tick | 预计纯播放时长 |
|---|---|---|---|
| `attach-weapon-budget-check.sh` | **传了**，`SPEED=32`（:42），调用行 :101 `--capture-tick "$TICK" --playback-speed "$SPEED"` | 30001 | 30001 / 66.65 / 32 ≈ **14 秒** |
| `animation-drive-states-check.sh` | **没有**（命中 0 次） | 9 次合计 143450 | **2152 秒 / 35.9 分钟** |

### 8.4 后果（lead 定级四条）

1. **耗时不可预算** —— 门禁不打印 tick 率、不打印播放速度、不打印预计时长；要算出来得去翻历史 metrics 倒推。
2. **贴死单次持锁上限** —— 36.3 / 40 min。任何人在这里多加一个负控就翻车（4 个负控现在复用已落盘的产物，所以不额外跑产品；若改成真跑，立刻超）。
3. **和 n08 的坑同族但方向相反** —— n08-n10-maps 在 N10 抓帧上白跑是因为窗口装不下（没传 `--playback-speed`，upward 播到 tick 40000 要 10 分钟，只给了 6 分半）；我这次是**装下了，但把锁吃满**。**同一个根因：谁都没钉播放速度。**
4. **更隐蔽的一层** —— 实时 1× 意味着门禁耗时**随 demo 长度线性增长**，而 demo 会换。今天 36 分钟，换一份更长的素材就是 60 分钟；那时它**连跑都跑不完，而且不报错，只是超时**（`timeout 420` 每次单跑仍会返回，但整趟跑不完、成绩作废）。

### 8.5 本次怎么处理的

lead 裁决（2026-11-14 19:26）：**立刻 kill、release 锁，把 P1 的 n12-qa 放行**。理由是 3 分钟沉没成本 vs 36 分钟 P1 顺延。执行记录：19:26 起的一次运行在 19:26:2x 被取消（`default@16780`，跑约 4 分钟），job 状态 `killed`；`hostlock.sh release e17-e20-char` → `RELEASED owner=e17-e20-char by=e17-e20-char`，紧接着 `LOCKED owner=n12-qa`，队列为空。孤儿进程检查：`ps -W | grep -ci tf2_demo_native` = **0**。

**这一趟没有留下可用读数**（`default` 一次运行都没跑完），N17 的像素层仍为 NOT VERIFIED。重跑排在 n12-qa 之后，跑**完整**那趟（一张票，36 分钟）。

### 8.6 修法（不在本轮，属门禁修复窗口）

- 最小修法：`run_capture` 加 `--playback-speed`（如 `SPEED=32`，与 attach gate 一致）→ 2152 s / 32 ≈ **67 秒**。
- 但**只加快速度会改变被观测的东西**：`--capture-tick` 是「在 tick N 画出来的那一帧」（`native/src/main.cpp:10029-10033` 的 pin 逻辑），加速后每帧跨更多 tick，`animdsum` 的**峰值**统计口径（脚本 :319-335 取峰值而非末行）需要重新确认仍成立。所以这条不能顺手改，必须连同证据重跑一起做——这正是它归「门禁修复窗口」而不是「现在顺手加一行」的原因。
- 同时应让门禁**打印** `tickRate=`、`speed=`、`expected_seconds=`，让耗时从「事后倒算」变成「跑前可见」。这条属于 §3.1 自检提议的同一族：把未经声明的假设写进日志。

### 8.7 这一节不证明什么

- 没有实测加速后 `animdsum` 峰值口径仍成立（这是 8.6 说的「不能顺手改」的理由，不是已证结论）。
- 没有普查其它门禁是否也没钉速度。**本文只核了 lead 点名的这两个。**
- 没有证明 36.3 分钟是这台机器上的稳定值——它依赖 demo、机器负载与渲染帧率；66.65 ticks/s 来自历史 metrics 反推，不是本次跑出来的。
### 8.8 本 gate 的 D53 / D55 / D56 自查（结论：不属于同族）

lead 要求跑之前先自查这三族。逐族核到行，结论写在表里。**注意这里的「安全」是逐个判据核出来的，不是从「有 tr -d」推出来的**（§2.4 已经证明过那种推法是假阳性来源）。

| 缺陷 | 本 gate 是否同族 | 依据（脚本行 → producer 行） |
|---|---|---|
| **D53** 解析被 CR 改判 | **否** | 全文唯一的 `cut -d=` 是 `:318 peak_of() { grep -o "$1=[0-9]*" "$dsum_file" \| cut -d= -f2 \| sort -n \| tail -1; }` —— 字符类 `[0-9]*` 不含 CR，且提取的是数字串，不是自由文本。其余读取只有两类：`grep -ac`（纯计数）与 `md5sum`（字节）。**没有一处 `awk -F'='`/`split("=")` 之后做字符串相等比较。** |
| **D55** 行尾字段导致恒真假绿 | **否** | C2 取的五个字段里 `crouch`/`crouchwalk`/`flinch`/`flinchStarts` **都不在行尾**（只有 `fallback` 在行尾，`native/src/main.cpp:11762`），且用 `grep -o "flinchStarts=[0-9]*"` 提取本身就锚定了数字串；断言是 `peak >= 1` 的数值比较，`"7\r"` 这种值到不了比较（`sort -n` 会把它当 0 排在最前，峰值仍取到纯数字那条）。A/B 的相等断言全是 md5 字节比较，与换行符无关。 |
| **D56** 断言读的字段在 producer 不存在 | **否** | 逐条对上 producer 格式串：`anim: state` ↔ `native/src/main.cpp:11311`（state= / reason= / label= / needle= 全在）；`animdsum:` ↔ `:11762`（crouch= / crouchwalk= / flinch= / flinchStarts= / fallback= 全在）；`anim: pick ` ↔ `:11164`；`animsum:` ↔ `:11755`（advanced= / skipped= / maxDeltaCycle= 全在）；`state=flinch ` / `label=a_flinch0` 由 `:11311` 的 `state=%s` / `label=%s` 产生。**没有一条断言在等一个不存在的字段。** |

顺带把 §2 的「五个无 CR 剥离的门禁」名单更新一下：`animation-drive-states-check.sh` 也不剥 CR，但**不剥也没事**，因为它的判据不碰自由文本字段。这条正好是 §3.1 自检提议要防的那类误判——「没剥 CR」不等于「有 D53」，得看判据落在哪个字段上。

### 8.9 若给 `run_capture` 补 `--playback-speed 32`，C 的读数会怎样（未实测，属风险）

这是 8.6 说「不能顺手改」的**具体量化**。产品侧的承诺是清楚的：

- `native/src/main.cpp:1952-1959` 原文：*the multiplier changes only how fast the cursor travels -- the captured frame is unchanged, because the cursor is held AT the requested tick and the scene is resolved from the recording rather than accumulated frame by frame.*
- `:10029-10033` 的 pin：越过目标 tick 后 `g_playback.tick = captureTick`；`:12742` 到 tick 才渲染落盘。

**但门禁的 C2 不是「取那一帧」，是「取整段播放的峰值」**：`animdsum` 是**每帧一行**（`:10752-10770` 的局部计数 + `:11759-11765` 打印），历史 `states-cover@20000` 在 1× 下产出 **14178 行**。32× 下同一段播放被采样成**少得多的绘制帧**：

| 计数器 | 1× 历史峰值（tick） | 32× 下被跳过的风险 |
|---|---|---|
| crouch | 2 | 中——`=2` 说明是多帧同时进入，被跳过概率低 |
| crouchwalk | 2（tick=11355） | 中 |
| flinch | 7 | 低 |
| **flinchStarts** | **7（tick=12016）** | **高——这是「该帧有几具身体刚进入 flinch」，是瞬时事件计数；14178 帧里只有极少数帧非零，末行（tick=29526）已经是 0** |
| fallback | 10 | 低 |

若 `tick=12016` 那一帧在 32× 下不被绘制，`flinchStarts` 峰值会掉到 0，`:330-331` 的 `>= 1` 断言直接变红。**这不是「加速改变了产品行为」，而是「门禁的采样密度变了」——但对判决而言后果一样：可能从 PASS 翻成 FAIL。**

所以 8.6 的修法必须连同证据重跑一起做，而且要**先跑一个 target 验证口径**（`states-cover@20000` 单跑，32× 约 10 秒），确认五个峰值仍 >= 1，再谈改脚本。反过来，如果实测发现峰值掉了，那结论就不是「加个速度参数」，而是**C2 的「峰值」口径本身依赖 1× 采样密度，属于未声明的假设**——那是比 D57 更值得记的一条。

### 8.10 九条运行命令（本轮要跑的就是这九条，一字不改）

`run_capture` 展开后的模板（`${T84_STATES_OUT}` 换成我的输出目录 `D:/_shared/EXEC-2026-11-16/e17-e20-char/t84-states`）：

```
TF2_NATIVE_TRACE=$OUT/<name>.trace timeout 420 native/build-nmake/tf2_demo_native.exe \
  --tf-root "D:/SteamLibrary/steamapps/common/Team Fortress 2/tf" \
  --settings-file $ISO_SETTINGS --ui-settings-file $ISO_UI \
  --audio-device 6 --demo $demo --capture-tick <tick> \
  --capture-frame $OUT/<name>.bmp --metrics-file $OUT/<name>.csv \
  --player-animation <drive>
```

| # | name | tick | `--demo` | 尾部参数 | 1× 预计 |
|---|---|---|---|---|---|
| 1 | `default` | 16780 | `bb841c6d…snakewater` | **（无 `--animation-drive`）** | 252 s |
| 2 | `legacy-crouch` | 14765 | 同上 | `--animation-drive legacy` | 222 s |
| 3 | `legacy` | 16780 | 同上 | `--animation-drive legacy` | 252 s |
| 4 | `states-flinch` | 16780 | 同上 | `--animation-drive states` | 252 s |
| 5 | `states-flinch-repeat` | 16780 | 同上 | `--animation-drive states` | 252 s |
| 6 | `states-crouch` | 14765 | 同上 | `--animation-drive states` | 222 s |
| 7 | `states-crouch-b` | 14800 | 同上 | `--animation-drive states` | 222 s |
| 8 | `states-cover` | 20000 | `$COVER_DEMO`（空 → 同 `DEMO`） | `--animation-drive states` | 300 s |
| 9 | `early` | 12000 | 同上 | `--animation-drive states` | 180 s |

合计 **143450 ticks / 66.65 ticks/s = 2152 s**，加 9 次启动 ≈ **36.3 分钟**（§8.2）。

**四个负控不额外跑产品**：`control_failed_as_required` 只读 `check_artifacts` 对已落盘变异副本的判决（`:428-458` 全是 `cp` + `sed` + 再判一次），所以 36.3 分钟就是全程。

---

## 9. D59：门禁不区分「状态量 / 沿量 / 帧计数」（同族，风险低于 D53/D55/D56，但会把门禁判据和采样密度绑在一起）

> **D59（lead 编号）：提速/降采样改变「沿量」的读数，而门禁的判据里没有一处能区分状态量、沿量与帧计数 —— 任何人加 `--playback-speed`（D57 的修法）时，没有任何信号告诉他会不会改变被观测对象。**

### 9.1 机制：同一条 trace 里，三种读数的行为不一样

producer 侧的三个事实（行号取自 `6dd853e` 的 `native/src/main.cpp`）：

1. **每个绘制帧才观测一次场景。** `main.cpp:10170` 每帧写 `scene: begin tick=%d sceneTick=%d`；`main.cpp:10180` 取 `resolvedSceneTick`、`main.cpp:10183` 调 `resolveSceneTick(...)`；`main.cpp:11762` 每帧写一行 `animdsum: tick=%d crouch=%zu crouchwalk=%zu flinch=%zu flinchStarts=%zu fallback=%zu`。一帧跨多少 tick 由 `native/src/playback_control.cpp:307`（`accumulator_ += elapsedSeconds * tickRate_ * speed_`）与 `:310-316`（`while (accumulator_ >= 1.0) { cursorTick_ += direction; }`）决定：speed=1 时一帧约 1 tick，speed=8 时一帧约 8 tick。
2. **`flinchStarts` 是边沿计数器，不是状态。** `main.cpp:11202` `const bool newTick = !runtime.hasLastHealth || runtime.lastHealthTick != resolvedSceneTick;`；`main.cpp:11208-11219` 只在 `signals.hasHealth && newTick` 时把本 tick 的 `signals.health` 与「上一次被观测 tick」的 `runtime.lastHealth` 比，变低才 `++playerAnimFlinchStarts`（`:11215`），并把 `lastHealth/lastHealthTick` 写回（`:11217-11219`）。它测的是**相邻两个被观测 tick 之间**的血量下降；观测密度决定能看见几个下降沿。`:11190-11200` 的注释把定义写成「Flinch comes from m_iHealth DECREASING between two observed scene ticks on a living body」。
3. **同一行的另一半是状态量。** `main.cpp:11221-11225` 的 `duckFlags`/`duckFields`/`crouched` 读 `duckflags`、`m_bDucked`、`m_bDucking`、`m_flDucktime`，是持续状态；8× 下漏不掉。**所以「这行 trace 在提速下还能不能读」是逐字段判断，不是整行是/否。**

两条与「提速会不会丢读数」直接相关的既有机制：

- **捕获帧被钉住**：`main.cpp:10029-10033` 在 `g_playback.tick > captureTick` 时把 tick 回夹到 `captureTick` 并 `resetAccumulator`；`main.cpp:12742` 在 `g_playback.tick >= captureTick` 时请求抓帧；`main.cpp:12881` 在抓帧写完后 `PostQuitMessage` 退出。**所以「末帧 / 最后一行」类读数与 speed 无关 —— 但这是读代码得出的顺序，未实测。**
- **区间事件不会被快速前进吞掉**：`main.cpp:10038-10045` 的 T93-C 注释写明：t1 → t2 > t1 的前进会把 `forEachRange(t1+1, t2)` 交给**一次** `advance()`，跳过的 tick 里每个 event 都在这一帧 resolve 并播放（历史实测 0.24-0.53 events/skipped tick）；`:10045` 的原文是「the seek counter is the trigger, so a fast playback advance is not mistaken for a seek」。**事件计数类读数在提速下不会丢（seek 路径 reset 除外）。**
- **metrics csv 是 1 行/秒的时间采样**：`main.cpp:9871-9874` `if (!force && interval < 1.0) return;`；`main.cpp:12942` 退出前强制写一行。行数随运行秒数变，不随帧数变。

### 9.2 三个类别与判据

| 类型 | 定义 | 提速下会发生什么 |
|---|---|---|
| **状态量** | 读某一时刻的字段值，或一个持续期远长于帧间隔的状态 | 不改变。捕获帧读数被 `:10029-10033` 钉住；持续状态漏不掉 |
| **沿量** | 读「相邻两个被观测 tick 之间的差分/边沿计数」 | **改变**。观测密度下降，下降沿可能被整段跳过 |
| **帧计数** | 读 trace 行数、绘制帧数、首/末帧序号 | 数值随绘制帧数变；判决是否翻红取决于判据：`>=1`/`==0`/`>0` 不翻，`==N` 或峰值阈值可能翻 |

### 9.3 普查口径（可复现）

- 仓库根 `*-check.sh` 共 **85** 个。
- 其中文本命中 `--demo` 且 `--playback-speed` 命中 **0** 的 **54** 个：这是「播 demo 但没钉播放速度」的实测集合。45 个带产品引用（`$PRODUCT` / `tf2_demo_native` / `run_product` / `t50_launch`）；9 个只跑探针、不跑产品：`animation-availability`、`entity-property-lookup`、`history-coverage`、`observer-focus`、`player-animation`、`player-model`、`slot-freshness`、`trajectory-overlay`、`weapon-world-model`（各 check 文件名后缀 `-check.sh`）。
- 又有 14 个门禁**已经**传了 `--playback-speed`（`attach-weapon-budget` 的 `SPEED=32` 在 `:42`、调用在 `:101`；另有 `bone-mismatch-semantics`、`build-identity`、`class-pixels`、`demo-clock`、`fog-check`、`hud-live`、`map-geometry-lighting`、`outline`、`player-body-appearance`、`spectate`、`third-person-weapon`、`timeline`、`world-geometry-budget`）。这 14 个已经在自己选的密度下读数，不在本节普查范围内。
- 扫描脚本（仓外、只读、不跑产品）：`D:/_shared/EXEC-2026-11-15/census-d59.sh`（85/54 计数）、`census-d59-list.sh`（54 条清单 + product_refs）、`census-d59-reads.sh`、`census-d59-frames.sh`、`census-d59-series.sh`（取峰值/末行/首行/计数四类模式）、`census-d59-peaks.sh`（`sort -n` 与相邻帧比较）。
- 扫描结果：54 个里 22 个出现「取峰值 / 末行 / 首行 / 计数」四类模式中的至少一类；其余 32 个读的是单条具名行（一次运行只发一行的 summary 或 decision 行），按 9.2 的定义直接落进状态量。
- **与 lead 转述的「40 个」差额未核对**：arch-ci 的原清单不在本工作树可读范围内（对 `/d/_shared/EXEC-2026-11-15/` 的 `grep -r` 超时 2 分钟），本报告的 54 是上述 grep 口径的实测数。

### 9.4 逐条普查表

| # | gate | 脚本:行号 | 读数 | 类型 | 提速是否会改变它 | 依据（producer 文件:行号） |
|---|---|---|---|---|---|---|
| 1 | `animation-drive-states` | `:313` `dsum_lines=$(wc -l < "$dsum_file")` | `animdsum` 行数 | 帧计数 | 会：行数≈绘制帧数，8× 下约 ÷8 | `main.cpp:11762` 每帧一行；`playback_control.cpp:307-316` |
| 2 | `animation-drive-states` | `:318-320` `peak_of flinchStarts`；判据 `:329-333` `-ge 1` | flinch 下降沿计数 | **沿量** | **会，且可归零 → 判据翻红** | `main.cpp:11202`、`:11208-11219`、`:11215` |
| 3 | `animation-drive-states` | `:318-320` `peak_of flinch` | 处于 flinch 一次性窗口的身体数 | 状态量（窗口受限） | 会缩水：`oneShotTicks` 默认 13（`:11212-11218`），8× 下一帧跨 8 tick，窗口只被 1-2 帧看到 | `main.cpp:11212-11218` |
| 4 | `animation-drive-states` | `:318-320` `peak_of crouch`/`crouchwalk`/`fallback` | 持续状态的同时计数 | 状态量 | 不会归零；多体同时的峰值可缩水 | `main.cpp:11221-11225` |
| 5 | `animation-drive-states` | `:216-225` `states_flinch_lines`/`a_flinch0` `-ge 1` | `state=flinch` 行数 | 帧计数 | 不会（判据 `>=1`） | `main.cpp:11311` 每帧一行 |
| 6 | `animation-drive-states` | `:226-231` legacy 侧计数 `==0` | `anim: state` 行数 | 帧计数 | 不会（稀疏采样造不出行） | `main.cpp:11311` |
| 7 | `animation-drive-states` | `:191-193` `picks`/`legacy_sums` `>=1` | trace 行数（存活性） | 帧计数 | 不会 | `main.cpp:11164`、`:11755` |
| 8 | `animation-drive-states` | `:213-215`、`:232-234` 三个 `md5_of` bmp 相等 | 捕获帧像素 | 状态量 | 不会（捕获帧 tick 被钉住） | `main.cpp:10029-10033`、`:12742`、`:12881` |
| 9 | `viewmodel-animation` | `:368-370` `max_frames()`（`sort -n \| tail -1`）；判据 `:829`/`:833`/`:920`/`:923` `==0`/`==31`/`==61` | 每帧 `frame=i/N` 的最大 N | **沿量（采样峰值）** | **会；判据是精确相等 → 可翻红** | `main.cpp:12461` 每帧一行；门禁自己的注释 `:361-367` 已记「末行读数随绘制帧数变」，正因为如此才改成取 max |
| 10 | `viewmodel-animation` | `:374-380` `rfield`/`rline`/`rsum` 取探针末行 | 探针 stderr 末行 | 状态量（探针侧） | 未普查（探针是否按命令步进而非按墙钟，未核） | 未普查 |
| 11 | `viewmodel-contract` | `:354-365` `capture_tick()` = 首个 `draw: begin` 的 tick | 首帧的播放位置 | 帧计数 | 会改变数值；但只用于回显 `PLAYBACK_TICK`（`:397`、`DEMO_TICK/PLAYBACK_TICK`），**不参与断言** | `main.cpp:12721`（首帧即首个 `draw: begin`）；`playback_control.cpp:307-316` |
| 12 | `viewmodel-contract` | `:372-375` `capture_scene_tick()` | 同一捕获帧的 `sceneTick` | 状态量 | 不会（定义是「首个 draw 之前的最后一个 `scene: begin`」，`:366-371` 注释自证） | `main.cpp:10170`、`:12721` |
| 13 | `camera-ui` | `:658` `follow_lines` `-ge 1` | `src=follow` 行数 | 帧计数 | 不会（判据 `>=1`） | `main.cpp:12620` |
| 14 | `camera-ui` | `:661` `src=follow` 行中 `eye=0.0,0.0,0.0` 计数 `==0` | 逐帧全称覆盖 | 帧计数 | 判决不会翻红；覆盖变稀，漏检风险上升 | `main.cpp:12620` |
| 15 | `camera-ui` | `:217` `tail -1` 取 `target=` | 末帧的 target | 状态量 | 不会 | `main.cpp:12620` |
| 16 | `camera-ui` | `:951-963` `t50_steps\>=85`、`t50_failed==0`、`pause_frozen==3` | 脚本步命令的判决计数 | 事件计数 | 不会（步数由 seed 固定，`:955-960`；判据比较命令前后 tick，不是帧数） | `main.cpp:12713` 每帧一步 |
| 17 | `world-geometry-hole` | `:1430-1433` `m1_draws == 0` | `draw: begin` 行数 | 帧计数 | 不会（判 0；该路径以 rc=14 在首个 draw 前退出） | `main.cpp:12721` |
| 18 | `fullscreen-quiet` | `:320-323` `rows=$(wc -l < "$2.csv")` `>0` | metrics csv 行数 | 帧计数（时间采样） | 会改变行数，不改变 `>0` 判决 | `main.cpp:9871-9875`（≤1 行/秒）、`:12942`（退出前强制一行） |
| 19 | `hud` | `:393-404` `tail -1` `hud: draw` 的 `drawn`/`union`/`scissor` | 捕获帧的 HUD 几何 | 状态量 | 不会（该行只在抓帧时发一次，`main.cpp:12847`） | `main.cpp:12847` |
| 20 | `hud-text` | `:152` `tail -1` `hud: draw` 某字段 | 同上 | 状态量 | 不会 | `main.cpp:12847` |
| 21 | `quality-audit` | `:137-139` `tail -1` draw tick `== "2000"` | 捕获帧 tick | 状态量 | 不会（pin） | `main.cpp:10029-10033`、`:12742` |
| 22 | `visual-baseline` | `:174-176` 同上 `== "2000"` | 捕获帧 tick | 状态量 | 不会（pin） | `main.cpp:10029-10033`、`:12742` |
| 23a | `weapon-body` | `:350` `tail -1` `draw: begin tick=` | 末帧 tick（与期望的捕获 tick 比） | 状态量 | 不会（捕获帧 tick 被钉住） | `main.cpp:12721` + `:10029-10033` |
| 23b | `weapon-material` | `:257` `tail -1` `draw: begin tick=` | 末帧 tick（与期望的捕获 tick 比） | 状态量 | 不会（捕获帧 tick 被钉住） | `main.cpp:12721` + `:10029-10033` |
| 23c | `world-material-semantics` | `:297` `tail -1` `draw: begin tick=` | 末帧 tick（与期望的捕获 tick 比） | 状态量 | 不会（捕获帧 tick 被钉住） | `main.cpp:12721` + `:10029-10033` |
| 23d | `world-near-clip` | `:161` `tail -1` `draw: begin tick=` | 末帧 tick（与期望的捕获 tick 比） | 状态量 | 不会（捕获帧 tick 被钉住） | `main.cpp:12721` + `:10029-10033` |
| 23e | `lighting-dark-account` | `:138` `tail -1` `draw: begin tick=` | 末帧 tick（与期望的捕获 tick 比） | 状态量 | 不会（捕获帧 tick 被钉住） | `main.cpp:12721` + `:10029-10033` |
| 23f | `lighting-semantics` | `:142` `tail -1` `draw: begin tick=` | 末帧 tick（与期望的捕获 tick 比） | 状态量 | 不会（捕获帧 tick 被钉住） | `main.cpp:12721` + `:10029-10033` |
| 24 | `world-material-face` | `:121`、`:171-172` `trim_trace` 保末帧 draw 行与每帧 `gpu:` 行 | 末帧 tick + 每帧行的存在性 | 状态量 | 不会（存在性断言，非计数） | `main.cpp:12721`、`:12881` |
| 25 | `world-geometry-hole` | `:301` `tail -1` `capture: armed` 的 tick | 抓帧时的 tick | 状态量 | 不会（该行每次运行发一次） | `main.cpp:12752` |
| 26 | `material-coverage` | `:750` `head -1` `capture: armed` 的行号 | 抓帧行在 trace 中的位置 | 帧计数（行号） | 会改变数值；用于切分 trace 而非断言 | `main.cpp:12752` |
| 27 | `logstf` | `:532` `tail -1` `capture: armed` | 抓帧行 | 状态量 | 不会 | `main.cpp:12752` |
| 28 | `viewmodel-companion` | `:644` `tail -1` `scene: vmAnim summary` 的 `observations` | 末次 summary | 未普查 | 未普查 | 未普查 |
| 29 | `viewmodel-robustness` | `:393-401` `MULTI_MAXBONE` `sort -n \| tail -1` | 探针一次输出的 range 行 | 状态量（探针侧） | 不会（探针不按墙钟推进） | 未普查（探针侧未核） |
| 30 | `audio-keys` | `:129`、`:140-150` 按键行计数 | 按键 → 一行 | 事件计数 | 不会 | `main.cpp:1522` 一次按键一行 |
| 31 | `playback-error-paths` | `:227` 取 `events_seen` | 事件计数 | 事件计数 | 不会改变（区间事件在快速前进下仍逐条 resolve）；但该字段根本不存在（D56），该行恒红，与提速无关 | `main.cpp:10038-10045`、`:12857` |

### 9.5 表里读出什么

- **36 行读数里，判为「沿量」的只有 2 条**：`animation-drive-states-check.sh:318-320` 的 `flinchStarts` 峰值（判据 `:329-333` `>=1`）与 `viewmodel-animation-check.sh:368-370` 的 `max_frames` 峰值（判据 `==31`/`==61` 精确相等）。这两条在提速下**会翻红**，且翻红不来自产品行为变化，来自门禁采样密度变化。
- **帧计数类 10 条**（`#1`、`#5`、`#6`、`#7`、`#11`、`#13`、`#14`、`#17`、`#18`、`#26`）里，**没有一条会翻红**：判据是 `>=1` / `==0` / `>0` / 仅回显（`#26` 只用于切分 trace），判决不变，**但证据强度随提速下降** —— 提速 8× 时 `camera-ui` 的「每一帧 eye 都不是 0」这类全称断言只检查到 1/8 的帧，仍然绿。
- **状态量类 20 条全部不翻红**（`#3`、`#4`、`#8`、`#10`、`#12`、`#15`、`#19`、`#20`、`#21`、`#22`、`#23a`、`#23b`、`#23c`、`#23d`、`#23e`、`#23f`、`#24`、`#25`、`#27`、`#29`），前提是 `main.cpp:10029-10033` 的 pin 在 speed>1 时仍成立 —— **这一条未实测（NOT VERIFIED）**。
- **事件计数类 3 条**（`#16`、`#30`、`#31`）不翻红，依据是 `main.cpp:10038-10045` 的 T93-C 语义。
- 合计：**36 行 = 沿量 2 + 帧计数 10 + 状态量 20 + 事件计数 3 + 未普查 1**（`#28`）。其中判据会翻红的只有沿量那 2 条。
- 结论的形态值得记住：**绝大多数门禁读的是「最后一个 draw 帧」这个被 pin 钉住的状态快照，所以在它们的判据里提速几乎无害；风险集中在那两条把「整段播放的峰值」当判据的读数上。** 这与 D57 的方向相反（D57 是耗时问题，D59 是读数问题），两者不互相解释。

### 9.6 已量化的一个例子

§8.9 已经用同一份历史 1× 运行算过 `animation-drive-states` 的五个峰值：`crouch=2`、`crouchwalk=2`（`tick=11355`）、`flinch=7`、`flinchStarts=7`（`tick=12016`）、`fallback=10`，而 `states-cover@20000` 在 1× 下产出 14178 行 `animdsum`。若把同一条运行改成 32×，绘制帧数掉到约 1/32，`flinchStarts` 峰值有掉到 0 的路（一次下降沿落在两个被观测 tick 之间就被跳过），`:329-333` 的 `>=1` 随即翻红。**这不是产品行为变了，是门禁采样密度变了。** 该推导是以历史读数为基础的算术，**未实测**。

### 9.7 修法方向（本节未实施任何一条）

1. **给每条读数打标签**：状态量 / 沿量 / 帧计数，写在门禁注释或 trace 行旁，让下一个加 `--playback-speed` 的人当场看到自己动的是哪一类。
2. **沿量必须留在原密度下读**，或改成 producer 侧累计量（`flinchStarts` 变成「本次运行累计下降沿次数」的恒定输出）。注意这与 pin 有交互：`:10029-10033` 的回夹会重设 accumulator，一次性窗口 `:11212-11218` 的 13 tick 也在同一密度假设下，改一处要一起核。
3. **峰值类判据不要用精确相等**：`viewmodel-animation` 的 `==31`/`==61` 改成「>= 某下界」，或让 producer 直接输出本次运行见过的最大 clip 帧数（`:12461` 的 `frame=%d/%d` 已在每帧给出手头数据）。
4. **门禁启动行统一打印 `tickRate=`、`speed=`、`frames=`**，让密度变化在日志里可见（D57 的修法要加 `--playback-speed`，这两条要一起落）。
5. **N25 报告要产出 §9.4 这张表并逐项签字**，而不是只写「已提速，测试通过」。

### 9.8 本节没有普查到的

- 9 个只跑探针的门禁（`animation-availability`、`entity-property-lookup`、`history-coverage`、`observer-focus`、`player-animation`、`player-model`、`slot-freshness`、`trajectory-overlay`、`weapon-world-model`）的探针是否按墙钟步进：**未普查**。
- 45 个跑产品的门禁里，只对落在「取峰值 / 末行 / 首行 / 计数」四类模式上的读数逐条判过（22 个门禁出现该类模式）；其余门禁的其余断言没有逐行读完：**未普查**。
- arch-ci 的 40 条清单与本机 54 条的口径差：**未普查**（原清单未取到）。
- `speed>1` 时 pin 与抓帧的实际交互：**未实测 → NOT VERIFIED**。
- 探针 stderr 末行读数（`#10`、`#29`）是否受提速影响：**未普查**。

---

## 10. 本文件没有证明什么（终节，覆盖全文）

1. **没有跑过产品。** 除 §8.9 引用既有 1× 历史读数外，全文没有任何一次 `tf2_demo_native` 运行，因此所有「会 / 不会」都是读代码得到的判定，不是实测结果。
2. **没有改任何代码或脚本。** §3.1 的 `gate-selftest.sh` 提议、§9.7 的五条修法都只是提议，一条都没有落地。共享 gate helper 是独占写域，落地要另派。
3. **没有证明 D53 修法落地后其余断言仍成立。** `tr -d CR` 只剥行尾，不改变任何数值判据；但「剥了之后门禁全绿」这件事没有跑过。
4. **没有全量普查 85/54 个门禁的每一行读数。** §2 只覆盖被读到的 awk 取值点与断言，§9.4 只覆盖落在四个模式上的 36 行读数；其余行记为「未普查」。
5. **没有证明 `speed>1` 时捕获帧仍被钉在 `captureTick`。** §9.5 把 20 条状态量读数判为「不翻红」，全部依赖 `main.cpp:10029-10033` 的 pin 在提速下照旧生效 —— 这条只读了代码顺序，**NOT VERIFIED**。
6. **没有证明 9 个只跑探针的门禁不受提速影响**（§9.8）。
7. **没有证明 D59 与 D53 的复合效应。** 两者是不同类的假设（一个关于行尾与解析器，一个关于采样密度）；本节没有构造「CR + 提速」同时出现的用例，也没有论证其中任一条能推出另一条。
8. **没有证明 §9.4 的「会 / 不会」对 `attach-weapon-budget` 那类已提速的门禁无影响。** 那 14 个门禁已经在自己选的密度下读数，本节没有把它们纳入普查。
9. **行号会漂移。** 全部行号取自 `6dd853e`（工作树 `native/src/main.cpp`、`native/src/playback_control.cpp` 干净）；后续提交会让这些行号失效，引用时以 commit 为准。
10. **§4 与 §8.7 的范围仍然只覆盖各自那一节**，本节不把它们扩大到全文。

