# 行尾治理：把源码、构建定义与门禁钉在 LF（2026-11-18）

## 1 一句话

2026-11-14 裁决、一直等「安全窗口」的 `.gitattributes` 行尾补丁已落地（提交 `07d8ca7d`）：
`native/{src,include,tools,cmake}`、仓库根门禁脚本与 `tools/` 被钉成 `text eol=lf`。
这不是新策略，是把已裁决的策略写进仓库——`core.autocrlf` 是每个 checkout 自己的设置，
只有仓库里的规则能活过一次 fresh clone。

## 2 为什么值得单独立一条

三条真实事故都记在门禁历史里，不是理论风险：

1. **bash 按名字失败**：CRLF 的 `.sh` 在 Bash 里报 `$'\r': command not found`，门禁的 EOL 守卫脚本因此按名字拒绝执行（`docs/INST1-MERGE-WINDOW-2026-11-16.md` §86 给了起手自检的写法）。
2. **逐字节门禁假红**：`core.autocrlf=true` 的 checkout 把 LF 交给门禁，`cmp`/diff 报 `1,25c1,25`，两侧字节其实一样（`.gitattributes` 里 T100 那段注释就是这个）。
3. **证据自证失败**：fresh clone 上行尾被重写，`artifacts.sha256` 的描述对象已经不是它自己的文件（证据目录因此早就有 `-text` 例外）。

## 3 改了什么

追加 6 条规则（`.gitattributes` 44 → 69 行）：

```gitattributes
/*.sh text eol=lf
/native/src/** text eol=lf
/native/include/** text eol=lf
/native/tools/** text eol=lf
/native/cmake/** text eol=lf
/tools/** text eol=lf
```

刻意**不是**全局 `* text=auto eol=lf`：markdown 保留它被提交时的字节，上面那些证据目录继续 `-text`，
本次不带任何全仓 renormalize（`git diff --stat` 只有 `.gitattributes` 一个文件，+26 行）。

## 4 落地前的读数：钉住的路径本来就是 LF

```
$ git ls-files --eol -- native/src native/include native/tools native/cmake tools '*.sh' | awk '{print $1" "$2}' | sort | uniq -c
    502 i/lf w/lf
```

502 个 tracked 文件全部索引 LF、工作树 LF，零 CRLF、零二进制。所以这条补丁**不重写任何文件**，
只阻止将来的 checkout 把它们交出去时带上 CR。这个读数是「不夹带 renormalize」的直接依据。

二进制例外按目录隔离：`native/third_party/webview2/x64/WebView2Loader.dll` 是 tracked 的 DLL，
它在 `native/third_party/**`，不被上面任何规则匹配，`git check-attr text eol` 仍是 `unspecified`——
二进制保持自己的字节。将来若在钉住的目录里再放二进制，必须自己加 `-text`，否则 `text eol=lf` 会毁掉它。

## 5 fresh checkout 的 A/B（正控 + 负控，同一台机、同一个 `core.autocrlf=true`）

用 `git -c core.autocrlf=true worktree add` 模拟「Windows 默认 checkout」，分别检出补丁前后两个提交，
数每个文件里的 CR 字节：

| 文件 | 补丁前 `d2c4667f` | 补丁后 `07d8ca7d` |
|---|---|---|
| `verify-all.sh`（根门禁） | CR=1610 | **CR=0** |
| `native/src/main.cpp` | CR=16313 | **CR=0** |
| `tools/audit-tree.sh` | CR=106 | **CR=0** |
| `README.md`（故意不钉的 markdown） | — | CR=82 |
| `docs/PROJECT-STATUS-2026-11-18.md` | — | CR=102 |

前两列是负控：没有规则时 autocrlf 确实会把三个文件整份改成 CRLF（1610/16313/106 行）。
第三行是正控：钉住的三个文件 CR 归零；两行 markdown 仍然拿回 CRLF，证明这一轮 checkout 里
`core.autocrlf=true` 是真的生效的——CR=0 不是「autocrlf 没开」造成的假绿。

复现：

```bash
cd /d/TF2_Native_Worktrees/t105-integrate
git -c core.autocrlf=true worktree add -q --detach /d/TF2_Native_Worktrees/eol-check 07d8ca7d
cd /d/TF2_Native_Worktrees/eol-check
for f in verify-all.sh native/src/main.cpp README.md; do printf '%-28s CR=%s\n' "$f" "$(tr -cd '\r' < "$f" | wc -c)"; done
cd /d/TF2_Native_Worktrees/t105-integrate && git worktree remove --force /d/TF2_Native_Worktrees/eol-check
```

## 6 跨 worktree 影响（裁决要求的「先核」）

`.gitattributes` 是从**工作树的文件**读的，不是从 HEAD 读的，所以这条提交对别的 worktree
**零影响**，直到它们真正取用这个提交：

- 22 个 worktree 全查过：`t105-n17anim`、`t105-overview`、`t105-qa` 等工作树的 `HEAD:.gitattributes`
  里 `line-ending governance` 命中数 = 0，属性未变。
- 22 个 worktree 里「脏的 .sh/.cpp/.h/.c/.cmake」：20 个为 0；`t105-n17anim` 7、`t105-overview` 4、
  `t105-integrate` 5——都是各自分支上的在途 WIP（integrate 那 5 个是正在做的 3D 天空盒），
  不是本次属性造成的行尾翻新。
- `stash@{0}`「WIP docs + LF .gitattributes」里存着**补丁前**的那份 `.gitattributes`。
  将来 pop 它会在这条提交之上制造冲突；正确做法是 pop 后只保留 docs 部分、丢弃 `.gitattributes` 那个 hunk。

## 7 没做的事（边界）

- 不阻止程序自己写出 CRLF 文件：`.gitattributes` 只管 Git 的检出/入库转换，管不了进程写盘。
- 不做历史 renormalize、不改已有 blob；只有在这些路径上的文件下次被 `git add` 时才按规则归一。
- `.ps1` 探针（根目录 12 个）**没有**钉：PowerShell 两种行尾都吃，本轮按裁决保持原样，等有需要再单独裁决。
- markdown 与 `evidence/**` 不动（逐字节证据目录本来就有 `-text` 例外）。
