# N22 — tier-1 workflow 落地，43 条断言的算术，以及远端为什么还不能宣称

作者：lead，2026-11-18。写盘范围：`.github/workflows/tier1.yml`（新增）、`verify-fast.sh`（只加注释）、本文档、
`docs/PROJECT-EXECUTION-UPDATE-2026-11-18.md` 的 N22 行。
**没有推 workflow，也没有远端 run。** 理由见 §4，不是省事。

## 0. 一句话结论

tier-1 的 CI 文件已经落进仓库（`.github/workflows/tier1.yml`，5 个 step，触发面是 `main` / `candidate/*` / 所有 PR / 手动 dispatch），
N22 草案里那个「数到 44 条、脚本钉 43、差 1 无解释」的算术已经核清（**43 是对的，草案把 E_MODEL_POSE 的注释行当成了一条**），
但 **CI 是否绿仍然未知**：这台机器上 `gh` 未登录、匿名 API 已限流，推上去也读不到 run 状态。所以本文档声明的是「文件就位 + 算术落地」，
不是「CI 通过」。

## 1. 落地的文件

`.github/workflows/tier1.yml`（4,399 B）五个 step：

| step | 做什么 | 失败时说什么 |
|---|---|---|
| `checkout` | `actions/checkout@v4`，`fetch-depth: 0` | — |
| `line endings stay LF` | `git ls-files --eol` 扫 `*.sh` 与四个钉住的目录，数 `w/crlf` | `TIER1-LINE-ENDINGS=FAIL` + 前 5 个文件 |
| `toolchain` | `bash tools/tf2-toolchain.sh` | `TF2-TOOLCHAIN: <具名原因>`，非零退出 |
| `tier 1` | `bash verify-fast.sh --mutation` | `VERIFY-FAST=FAIL (...)` |
| `upload evidence` | `actions/upload-artifact@v4`，`if: always()`，`evidence/fast` | `if-no-files-found: warn` |

三个设计取舍，写进文件头注释里：

1. **只跑 tier-1，不跑 `verify-all.sh` / `verify-release.sh`**。后两者解码整段 demo、开窗口、抓帧、做 mutation 构建，需要私有 TF2 资产、GPU 和分钟到小时级时间；
   放进这里会让 job 因为与改动无关的原因变红。
2. **触发面不含 `work/*`**。这个文件先落在 `work/integrate` 上，若 push 就自触发，第一次 run 的日志会混进「文件自己落地」这件事。
   要在工作分支上试，用 `gh workflow run tier1 --ref work/integrate`，或推一个 `candidate/*` 分支——这两条是设计上被支持的入口。
3. **加了一个行尾 guard**。`.gitattributes` 的六条 `text eol=lf`（`07d8ca7d`）是今天才落地的，guard 在 runner 实际检出的树上去断言它，
   而不是相信它：CRLF 曾经让 21 个脚本 `bash -n` 报语法错，而静默 renormalise 一个字节代码都不改就会把它带回来。

## 2. 43 条断言的算术（N22 草案 §7 的遗留问题）

草案 `:168` 的原文是「我数到 44 条字面量（10+8+7+8+4+7），脚本钉的是 43。**这个差我没有解释**」。逐数组实测：

| 数组 | 位置 | 条目数 |
|---|---|---|
| `E_PRESENTATION` | `verify-fast.sh:128-139` | 10 |
| `E_ENTITY_MODEL` | `:141-150` | 8 |
| `E_WORLD_MATERIAL` | `:152-160` | 7 |
| `E_MODEL_POSE` | `:162-173` | **7** |
| `E_ITEM_SCHEMA` | `:175-180` | 4 |
| `E_VIEWMODEL_FOV` | `:186-194` | 7 |
| 合计 | | **43** |

差的那 1 条在 `E_MODEL_POSE`：它的七条字符串之间夹着一段三行注释（`:169-171`，解释为什么
`"corruptPosePreflight":false` 读起来是反的），草案按行数扫数组时把注释算成了条目。
**pin 是正确的**，不是脚本数错，也不是数组少了一条。`verify-fast.sh:267` 的 `[ "$ok" -eq "$FAST_ASSERTIONS" ]` 就是这条 pin 的守卫：
断言数一旦和数组不一致，`VERIFY-FAST=FAIL` 会打出来。

已把这个算式写进 `verify-fast.sh:76-87` 的注释，免得下一个人再数一遍。

> 口径提醒：这是**静态核对**。运行时 `ok=` 是否真等于 43，要在一次跑绿的 tier-1 里才看得到；本机没跑（§4 的锁与 sky-3d 构建）。草案 `:168` 要的「跑通后核一次」仍然开着。

## 3. 本地能证明什么

| 项 | 读数 |
|---|---|
| workflow 是合法 YAML | `yaml.safe_load` 通过：顶层 `name/on/permissions/concurrency/jobs`，`jobs.fast` 五步齐全，`on.push.branches=[main, candidate/*]` |
| 行尾 guard 在干净树上不误报 | 本地干跑同一命令：`worktree CRLF files: 0` |
| 工具链入口存在且失败具名 | `tools/tf2-toolchain.sh` 9,576 B，头部自述「explicit override → vswhere/discovery → hard failure with a named reason」，成功行带 `status=OK` |
| 全新构建树会被 configure | `build-target.sh:70` `BUILD=${TF2_BUILD_DIR:-native/build-nmake}`；`:101-123` 在树不存在时跑 `cmake -S native -B "$BUILD" -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release` |
| tier-1 不依赖私有资产 | `verify-fast.sh:14-16` 自述「Nothing here reads a demo, opens a window, or touches an audio device」 |

## 4. 远端状态：没跑，以及为什么不在这一轮盲推

| 通道 | 读数 |
|---|---|
| `gh` | `/e/gh.exe` 存在，`gh auth status` → `You are not logged into any GitHub hosts. To log in, run: gh auth login` |
| 匿名 GitHub API | `https://api.github.com/repos/elo6371/tf2-demo-player-native` → **403**，`API rate limit exceeded for 67.159.48.149`（同一个 run 列表接口同样 403） |
| 远端可达性 | SSH 推送是通的（本批之前已推 `cb4912ca..8b319e25`） |

**为什么不推：** 推了就能触发 run，但读不到 run 的结论——`gh` 未登录、匿名 API 限流，等于制造一个「绿/红都不可观测」的远端事件。
N22 的验收原话是「保存远端 run URL/commit」，做不到 URL 就不要把「已推」写成「已跑」。
所以本批把文件落地到工作分支（触发面不含 `work/*`，**推这个文件不会自触发**），把「真跑」留给有 `gh` 凭据或能读 Actions 页面的窗口。

两条被设计支持的触发路径：

```bash
# A. 有 gh 凭据时，直接在开发分支上跑一次
gh workflow run tier1 --ref work/integrate

# B. 推一个 candidate/* 分支（触发面匹配），run 会出现在 Actions 页
git push origin HEAD:refs/heads/candidate/n22-tier1
```

## 5. 这份材料不能证明什么

- **不能证明 CI 会绿**：workflow 没跑过，一次都没有。
- 不能证明 `windows-2022` runner 上 `nmake` + 7 个目标的构建成立：本机唯一的实测是 `native/build-nmake` 这棵树，
  runner 上是全新树 + 另一个 MSVC/SDK 版本组合。
- 不能证明 45 分钟超时够用：本机没有 tier-1 端到端墙钟（构建要持锁，本轮锁与 sky-3d 的构建在争）。
- 不能证明 `--mutation` 在 runner 上的 setUp 断言（`verify-fast.sh:240-243`）不会因为 probe 输出格式不同而 setup-fail。
- 不能证明 artifact 大小与保留期合适。

## 6. 复现

```bash
python -c "import yaml;d=yaml.safe_load(open('.github/workflows/tier1.yml',encoding='utf-8'));print(list(d['jobs']['fast']['steps']))"
git ls-files --eol -- '*.sh' 'native/src/**' 'native/include/**' 'native/tools/**' 'native/cmake/**' 'tools/**' | grep -c 'w/crlf'   # 期望 0
python - <<'PY'   # 43 的算术
import re
src=open('verify-fast.sh',encoding='utf-8').read().splitlines()
cur=None; tot=0; per={}
for ln in src:
    m=re.match(r'^(E_[A-Z_]+)=\($', ln)
    if m: cur=m.group(1); per[cur]=0; continue
    if cur and ln.strip()==')': cur=None; continue
    if cur and ln.strip()[:1] in ("'", '"'): per[cur]+=1
print(per, sum(per.values()))
PY
```
