# N27 可选组件契约（视频导出）— 2026-11-15

状态：契约设计稿。本文件只定义「可选组件 + GitHub 分发」的契约，不含实现、不含门禁脚本改动、不含提交。
产品要求（原话）：「视频导出这个做成可选安装项，到时候在 github 上发布可选安装插件」。
配套裁定：docs/N27-SCOPE-DECISIONS-2026-11-15.md 第七节（导出必须带音轨、不允许自备 ffmpeg）。
实测点：D:/TF2_Native_Worktrees/t105-e5，branch candidate/e5，HEAD ed20a7e。

## 0. 覆盖范围

覆盖：组件身份、CLI 动词、卸载边界、缺失时的运行期报错、GitHub 产物形状、可验证边界、门禁增量。
不覆盖：视频编码实现（N27 8.2 的能力探针未出结论前没有实现可写）、面板 UI、代码签名、CI 流水线。

---

## 1. 实测现状（先给证据，再给决定）

### 1.1 一个安装目录 = 一份清单

- 清单位置 install_lifecycle.cpp:185-187 = installDir/.uninstall/install-manifest.txt；状态目录名 .uninstall（install_lifecycle.h:44）。
- 顶层键固定顺序 install_manifest.cpp:294-309：format、formatVersion、package、packageVersion、packageVersionCode、sourceDateEpoch、sourceTree、sourceDirty、sourceDirtyFiles、installedUtc、installDirectory、files=N。
- 每文件记录 install_manifest.cpp:310-315，行内 4 或 5 列；解析要求恰好 4 或 5 列（:346），路径必须通过 safeRelativePath（:353-359）。
- 解析器拒绝任何不认识的键，报 "unknown manifest key"（:384）；formatVersion 必须等于 1（:370-371）。

机械结论：往核心清单里加任何一个顶层键，不认识它的构建会整份拒绝这份清单。这条直接决定了第 2 节的身份选择。

### 1.2 状态机与删除边界

- 分类循环 install_lifecycle.cpp:290-387：new / changed（带备份）/ unchanged / preserved。
- 卸载只对清单里的记录动作（:591-654）；preserved 与 unchanged 一律跳过（:592）。
- 目录清理只删空目录，并跳过 .uninstall（:656-675）。
- 收尾对 .uninstall 是整体删除：:697 std::filesystem::remove_all(options.installDir / kStateDirectory)。

机械结论：任何放在 .uninstall/ 下的组件状态（清单、payload），在核心卸载成功之后会被 :697 一起删掉。组件状态放在哪里，由这一行决定，不由命名习惯决定。

### 1.3 .uninstall 与 payload 被扫描器跳过

isLifecycleState（install_lifecycle.cpp:178-183）只匹配两项：相对路径等于或以 ".uninstall" 开头、等于或以 "payload" 开头。

机械结论：放在 .uninstall/components/ 下的组件清单与组件 payload 不会进入核心安装的「目录现状」清单，也不会被核心记成 preserved。

### 1.4 输出形状被门禁钉死（两处）

- --version 输出 install_lifecycle.cpp:789-814，共 4 行：第 1 行 :792-793，第 2 行 :798-799，第 3 行 :803-804，第 4 行 :811-812。
- 该函数 :794-797 的注释声称第 2 行形状固定、门禁在 install-lifecycle-check.sh:262-281 grep sourceTree= 并比对第 1 行。该行号已失效（实测：当前 :248-282 是 write_product_driver，一个生成的 PowerShell 转发壳）。真实断言在 :412-413（有 versionCode=、有 sourceTree=）与 :428-430（install_cli_probe 的 --version 第 1 行必须与产品一致）。注释的行号错，注释的结论对。
- install 成功行被逐字段 grep：:458 install=PASS、:459 mode=fresh、:460 files=OWNED_FILES、:461 new=1（该用例只改一个文件）、:462 backups=0、:463 preserved=2、:476-477 装机后文件数 INSTALLED_LISTING。
- 计数常量 install-lifecycle-check.sh:133-137：PAYLOAD_FILES=5、OWNED_FILES=6（+VERSION.txt）、FRESH_LISTING=7（+清单）、INSTALLED_LISTING=9（+清单+2 份 WebView2 许可证）、MANIFEST_RECORDS=8。

契约要求：--version 的前两行、install=PASS 行的既有字段，一个都不动；组件的一切新信息走新行、新命名空间。

### 1.5 打包现状

- native/cmake/tf2-package.cmake:16-29 说明 payload 就是 cmake --install 自己铺出来的文件；:30-74 是 install 规则；payload 现为 5 个文件。
- :62-68 的注释明确写着：往 payload 加文件会移动 install-lifecycle-check.sh 里的计数断言。这是「加文件会移动读数」的既有先例。
- 没有任何发布流水线：ops_glob ".github/**/*.yml|yaml" 返回空列表（0 个 workflow）。

### 1.6 没有组件概念，也没有导出动词

- ops_grep "Component|componentId|featureId" 于 native/src → 0 命中。
- ops_grep "export-video|export_video|exportVideo" 于 native/src → 0 命中。
- 未知参数被记录而不是被拒绝：main.cpp:3164-3168（D-23），并打印到 trace（:3172-3185）。

机械结论：今天写 --export-video，产品把它当成没认出来的参数，照常退出 0。此刻写一条读它的门禁，读到的是「参数被忽略」，不是「功能没实现」。

### 1.7 拒绝的形状已有先例

- main.cpp:3245-3254：--capture-depth 缺少 --capture-frame 时拒绝；注释写明「否则会什么都不写然后退出 0，而忘记参数的 gate 会把这种空白读成这个开关被忽略」。退出码 13（:3239、:3243）。
- AUD-10 trace：main.cpp:12960-13004，把「被开关关掉」和「声音丢了」分成两类计数；:12970-12973 的注释给出理由。

---

## 2. 决策一：组件身份 —— 每组件一份清单（独立状态根）

布局：

```
<installDir>/.uninstall/install-manifest.txt                        # 核心，不动
<installDir>/.uninstall/components/<name>/install-manifest.txt      # 组件自己的清单
<installDir>/.uninstall/components/<name>/payload/                  # 组件待装文件（模拟核心的 payload）
<installDir>/.uninstall/components/<name>/backup/                   # 组件改动前的原文件
```

复用方式（逐项，不是「看起来像」）：

- 同一个 format 串（tf2-demo-native-install-manifest，install_manifest.h:55）、同一个 formatVersion=1、同一套 serialize/parse（install_manifest.cpp:292-317、:319-388）。
- 组件清单里的 installDirectory 仍写安装目录本身，所以 resolveInside（install_lifecycle.cpp:204-218）、备份目录、modified 判定、运行镜像延迟（:637-641）都按原语义工作。
- 需要参数化的只有三处：manifestPath（:185-187）、statePath（:189-192）、收尾的 remove_all（:697）。组件模式把状态根换成 .uninstall/components/<name>，三个函数各加一个组件段；分类循环、备份、卸载循环、计数行逐字复用。

不选「核心清单加 component 字段」，理由按证据强度排序：

1. 解析器拒绝未知键（install_manifest.cpp:384）→ 旧构建整份拒绝，属硬破坏；
2. 组件安装必须改写核心清单，而现有代码里不存在「变更清单」的路径（只有安装会写清单）；
3. 核心清单的 packageVersion、packageVersionCode、files= 计数被门禁逐字段 grep（install-lifecycle-check.sh:458-463、:671-674），组件字段会让这些读数随组件而变，把「核心是什么」和「装了哪些组件」绑死在一个文件里；
4. 每组件独立安装目录也不选：运行镜像判定靠 GetModuleFileNameW 与自身路径比较（install_lifecycle.cpp:168-176），拆目录会把「一个目录 = 一次安装」的语义拆散。

两条边界机制（机械可检，不靠人记得）：

- M1 核心清单所有权：组件安装前先读核心清单；组件 payload 的路径若出现在核心清单的 new|changed 记录里 → 拒绝，退出 2，一个字节不动。
- M2 已存在但不属于核心的文件：完全复用分类循环（:335-380）——记为 changed 并备份，组件卸载时恢复。用户自己的同名文件不会丢。

---

## 3. 决策二：CLI 形状

六个动词，其中三条是新增：

| 场景 | 命令 |
| --- | --- |
| 只装核心 | --install（不变） |
| 核心+某组件 | --install 然后 --install-component NAME（两条命令，各自可验证） |
| 后装组件 | --install-component NAME |
| 只卸组件 | --uninstall-component NAME |
| 卸全部 | --uninstall --with-components |
| 查组件 | --component-status [NAME] |
| 核心卸载（语义收紧） | --uninstall —— 有组件时拒绝，见第 4 节 |

与既有 dry-run 契约不冲突：单独 --install-dry-run 仍只演核心安装（install-cli-contract-check.sh:34-36 的 A1）；与 --install-component 同用 → 演组件安装；与任何卸载动词同用 → 仍是 reason=dry-run-conflict（:41-43 的 A2）。

输出行（全部新命名空间，门禁可以直接 grep）：

```
component_install=START component=video-export version=1.0.0 versionCode=1000 payloadFiles=3 payloadBytes=... coreVersionCode=100
component_install=PASS component=video-export files=3 new=3 changed=0 preserved=0 backups=0
component_manifest=.uninstall/components/video-export/install-manifest.txt version=1.0.0 versionCode=1000
```

```
component_install=FAIL reason=no_core_install detail=no install-manifest.txt in <dir>
component_install=FAIL reason=unknown_component component=<name> known=video-export
component_install=FAIL reason=core_too_old component=video-export minCoreVersionCode=120 coreVersionCode=100
component_install=FAIL reason=no_payload payload=<path>
component_install=FAIL reason=path_conflict_with_core file=<p>
component_install=FAIL reason=cannot_copy file=<p>
```

```
component_status=VERIFIED|DEGRADED|ABSENT component=video-export version=1.0.0 files=3 present=3 missing=0 modified=0
component_uninstall=START component=video-export
component_uninstall=PASS component=video-export removed=3 restored=0 preserved=0 modified=0 alreadyMissing=0 deferredRunningImage=0
component_uninstall=INCOMPLETE component=video-export deferredRunningImage=1 hint=close the product and run again
```

退出码沿用既有语言：0 成功；2 命名拒绝（拒绝发生在任何字节移动之前，与 install_lifecycle.cpp:702-705 的注释一致：2 是「这个目录不是一次安装」这一族的命名失败）；3 卸载不完整（:709-710）。
不新增退出码：install-cli-contract-check.sh:66-67 已经把 0/1/2/90 的读法定死，混入 4 会让已有读法分层。

---

## 4. 决策三：核心卸载遇到组件文件 —— 拒绝，并给出两条出路

三个可选处置（一起删 / 保留并给一行 / 拒绝），选择拒绝。理由由机制给出，不由纪律给出：

1. 核心卸载唯一会碰到组件状态的地方是 :697 的 remove_all(.uninstall)。它会连同组件清单与组件 payload 一起删；组件已装进安装目录的文件留在原地、失去任何记录 → 变成无法再被卸载的孤儿。
2. 若选「一起删」：核心卸载将隐式执行 N 个组件的删除，而每个组件卸载都可能出现运行镜像延迟（:637-641）与不可删文件（:642-651），核心卸载的 PASS 含义会被稀释；并且用户没有说过要删组件。
3. 若选「保留并给一行」：必须把 :697 改成选择性删除（改的是现有收尾逻辑），而且用户会得到一个「卸载了但目录还在」的目录——这与 --with-components 想给的干净目录重叠，收益不足。

拒绝行：

```
uninstall=FAIL reason=components_installed count=1 components=video-export detail=--uninstall only removes the core; use --uninstall-component NAME or --uninstall --with-components
```

exit 2，任何字节不动。

--with-components 的顺序是被规定的：先逐个 component_uninstall（每个给独立 PASS/INCOMPLETE 行），全部成功才进入核心卸载；任一组件卸载不完整 → 核心卸载不开始，exit 3。这保证「卸全部」只有一条真实路径，且这条路径上组件仍然由组件自己的清单做权威。

不变量（写入门禁，见第 8 节）：

- I1 组件安装前后，核心清单字节不变（sha256 相同）。
- I2 只卸组件之后，核心 --install-status 仍是 VERIFIED，present 计数不变。
- I3 有组件在时 --uninstall 退出 2，目录字节不变。
- I4 --uninstall --with-components 之后，安装目录只剩用户自带的文件（用例里预置一个用户文件，确认它还在）。
- I5 组件卸载之后，核心 --version 的 4 行与未装组件时逐字节相同。

反向保护（为什么组件卸载碰不到核心）：组件卸载只对组件清单的记录动作；resolveInside（:204-218）挡目录外路径；M1 在安装时已经拒绝「组件 payload 覆盖核心记录」；因此组件卸载的删除集合与核心文件集合在安装阶段就不相交。

---

## 5. 决策四：缺组件时的运行期报错（AUD-10 模型）

三类状态分开计数（对齐 main.cpp:12970-12973 的教训：把「被关掉」和「丢了」折在一起，读数就分不清）：

```
export: video_mode=disabled|available component=video-export component_state=verified|absent|degraded core_too_old=0 export_requests=1 export_refused=0 export_frames_written=0 encoders=h264=1,aac=1 exit=0
```

请求了导出但产不出（每个 reason 都可行动、互斥）：

```
export=FAIL reason=component_not_installed component=video-export hint=--install-component video-export
export=FAIL reason=component_core_too_old component=video-export minCoreVersionCode=120 coreVersionCode=100
export=FAIL reason=component_degraded component=video-export missing=1 modified=0
export=FAIL reason=encoder_missing codec=h264 hint=install the Windows media feature pack
export=FAIL reason=disabled_by_flag detail=--export-video not requested
export=FAIL reason=export_path_unwritable path=<p>
```

退出码 13，与 --capture-tick 非法、--capture-depth 缺参同类（main.cpp:3239、:3243、:3253）。不选「退出 0 + 空文件」：理由已经在 main.cpp:3246-3249 的注释里。

关键实现约束：--export-video 必须被显式识别。今天它落在 unrecognizedArguments（:3164-3168）并照常退出 0（D-23），所以「没实现」和「参数打错」在读数上无法区分。契约要求在参数表里加入 --export-video / --export-out / --export-fps，并在启动时给一行能力行，让「没装」在用户点导出之前就可见：

```
capabilities: export_video=absent component=video-export hint=--install-component video-export
```

---

## 6. 决策五：GitHub 分发形状

产物（一个组件一个 zip；文件名必须能直接读出版本）：

```
tf2-demo-native-component-video-export-1.0.0-win64.zip
  component.txt
  payload/...
  LICENSES/...
tf2-demo-native-component-video-export-1.0.0-win64.zip.sha256
```

component.txt 用键值行（风格与 VERSION.txt 一致，parse 逻辑可以照抄）：

```
component=video-export
componentVersion=1.0.0
componentVersionCode=1000
minCoreVersionCode=100
artifactFormatVersion=1
package=tf2_demo_native
payloadFiles=3
payloadBytes=...
```

版本比较用核心的 TF2_PACKAGE_VERSION_CODE（native/cmake/tf2-version.cmake:21-22，写进 build stamp :149-150；当前值 100）。不满足时拒绝且不落地：

```
component_install=FAIL reason=core_too_old component=video-export minCoreVersionCode=120 coreVersionCode=100
```

旧组件 + 新核心：核心升级不触碰组件文件——核心安装把它们记成 preserved（:460-480），卸载跳过 preserved（:592），目录清理只删空目录（:663）。所以核心升级之后组件仍可用，--component-status 独立给出结论。核心降级被既有逻辑拒绝（:242-257），因此「装个更旧的核心把组件弄坏」这条路径不存在。组件自身的版本比较复用同一套判断（组件清单有 packageVersionCode）。

发布步骤（因为 0 个 workflow，这是手工链，不是流水线）：
构建 → cmake --install 到临时前缀 → 取该组件的 payload 子集 → 生成 component.txt → 打 zip → 算 sha256 → 上传 release。
每一步都能用现有工具完成；没有任何一步是自动的，这是未决项，不是本文件能承诺的东西。

---

## 7. 决策六：可验证性分表

| 单元格 | 证明手段 | 需要第二台机器 |
| --- | --- | --- |
| 组件安装/卸载/状态/计数 | install_cli_probe + 临时目录（install-lifecycle-check.sh 就是这么跑的，PROBE 定义在 :113） | 否 |
| 核心清单字节不变（I1） | 前后 sha256 比对 | 否 |
| 卸载拒绝（I3） | exit 2 + 目录前后 sha256 | 否 |
| 核心版本过低拒绝 | 手改 component.txt 的 minCoreVersionCode=999999 | 否 |
| 缺组件的 trace 与拒绝行 | 删组件状态根后跑 --export-video | 否 |
| --version 前两行未变（回归） | install-lifecycle-check.sh:412-413、:428-430 原样跑 | 否 |
| 组件把核心清单弄坏（负向） | 组件 payload 塞核心 exe，断言 exit 2 与核心 exe 字节不变 | 否 |
| 干净 Windows 上 H.264/AAC MFT 是否存在 | N27 8.2 的探针，在无 Codec Pack 的机器上跑 | 是 |
| GitHub 下载往返（zip 完整、代理、断点） | 在干净机器上按 release 页面步骤装一遍 | 是 |
| SmartScreen 与签名 | 需要签名证书与真机 | 是 |
| 真实用户目录权限（是否必须提权） | install_lifecycle.h:5-34 明确不要 elevation；真机确认一次 | 是（只确认） |

---

## 8. 门禁增量（新脚本，不改现有脚本）

install-component-contract-check.sh 的断言清单：

1. 组件安装后，核心清单 sha256 未变（I1）。
2. 组件清单的 format 与 formatVersion 与核心同值（直接 grep 两行）。
3. 组件安装后，--install-status 仍 VERIFIED，present 未变（I2）。
4. 有组件时 --uninstall 退出 2，reason=components_installed，目录字节不变（I3）。
5. --uninstall-component 之后，--version 的 4 行与未装组件时逐字节相同（I5）。
6. --uninstall --with-components 之后，安装目录只剩预置的用户文件（I4）。
7. minCoreVersionCode 过大时退出 2、reason=core_too_old、组件清单未落地。
8. 组件 payload 含核心 exe 时退出 2、reason=path_conflict_with_core、核心 exe 字节不变。
9. 同一组件重复安装：给出正常的 new/changed/unchanged 读数，不产生第二份清单。
10. 组件卸载遇运行镜像：deferredRunningImage=1、exit 3（与核心语义一致）。

这 10 条全部只需要临时目录与 install_cli_probe，属于「不需要第二台机器」的一列。

---

## 9. 与现有契约的冲突面（需要 lead 决定）

1. --uninstall 语义收紧：今天有组件也能卸核心；契约要求退出 2。现网没有组件（1.6 实测），所以真实环境里没有调用方会触发旧行为；install-cli-contract-check.sh 的 A5（:39-40）在无组件目录里跑，不受影响。
2. 组件文件会让核心的 preserved 计数从 2 变成 2+N。门禁 :463 的 preserved=2 只在干净目录成立；契约要求含组件的用例另开，不去改 :463 的读数。
3. :697 的 remove_all 会连组件状态一起删。--with-components 依赖「先卸组件」的顺序，而不是这一行本身的行为。

---

## 10. 未决问题

- 组件是否需要在核心升级后自动重验？本文件只要求 --component-status 能回答，不要求自动。
- 组件 payload 是否允许包含会被核心进程加载的 DLL？若是，卸载同样会走运行镜像延迟分支（:637-641），验收要加一条。
- 组件与核心是否同 tag 发布（决定 GitHub 上的发现路径）。
- 视频导出实现本身仍卡在 N27 8.2 的 MFT 探针上。组件契约可以先合入，组件产物不能先发。

---

## 11. 本文件没有证明什么

- 没有证明组件机制已经实现：native/src 里 0 处组件代码（1.6 实测）。
- 没有证明视频导出能被做出来：N27 1.1 与 8.2 尚未给出探针结果。
- 没有在真机上跑过任何一步：全程只读源码与既有报告。
- 没有证明 GitHub 上能下载：仓库里 0 个 workflow（1.5 实测），发布链是手工步骤。
- 没有证明 --uninstall 收紧不影响仓库外脚本：只证明仓库内门禁在无组件目录下不受影响。
- 没有给出任何工时或排期。
---

## 12. lead 裁决（2026-11-14 20:50）

### 12.1 §9 三处冲突

1. **`--uninstall` 收紧为 exit 2：批准。** 但要同时满足三件事：① 新增负控用例（目录里有组件 → `--uninstall` 必须 exit 2 **且目录字节不变**，即本文件 I3 的形态）；② `install-cli-contract-check.sh` 的 A5 不改（它在无组件目录里跑，不受影响）；③ 「仓库外脚本会怎么反应」**本文件没证明过**，必须原样写进发布说明，不许降级成「无影响」。
2. **`preserved` 计数不改：批准。** `install-lifecycle-check.sh:463` 的 `preserved=2` 一字不动，含组件的用例另开。理由不是省事：把「组件存在」塞进核心那一行读数，会让同一条断言同时依赖两个变量，红了你分不清是谁造成的。
3. **「先卸组件、再卸核心」的顺序：批准，本轮不改 `install_lifecycle.cpp:697`。** 但契约里要留一句给后人：「若将来支持 `--with-components --force`，`:697` 的 `remove_all` 必须改成只删核心清单与核心 payload」——否则读代码的人会以为这一行已经安全。

### 12.2 §10 四个未决

- **升级后自动重验：不做。** 只要求 `--component-status` 一次显式调用能答（`VERIFIED / DEGRADED / ABSENT`）。自动重验会把「核心启动」与「组件可用性」绑在一起，代价是每次启动都要读组件清单。
- **组件 payload 是否可含被核心进程加载的 DLL：首发禁止。** 导出组件是独立 exe（mfprobe 已验证：只链接 `mfplat/mfreadwrite/mfuuid/ole32/user32`），没有理由让核心去加载它。将来真需要，另立卡并加运行镜像延迟用例。
- **同 tag 发布：是** —— 组件与核心同一个 release tag，但组件是**独立资产**，不是核心包的一部分；发现路径 = release 页上的 `component.txt`（含 `minCoreVersionCode`）。
- **视频导出实现：等 mfprobe 报告落盘。** 契约可以先合入，组件产物不能先发——这一点本文件已自述，保持一致。

### 12.3 对 §5（Q4：未装组件时的报错）的修正 —— lead 亲自核过代码

本文件 §5 说「`--export-video` 今天落在 unrecognized 里、与打错参数无法区分」。**方向对，但话说过头了**，实测有三层，必须分层写：

1. `--export-video` 确实被推进 `unrecognizedArguments`（`native/src/main.cpp:3167`），且这是 **D-23 的有意设计**，注释原文在 `:3165-3166`：「D-23: recorded, not rejected. The default run still ignores it, which is the behaviour every existing gate was written against.」
2. 但名字**在 trace 里看得见**：`traceFmt("args: unrecognized=%zu first=%s all=%s", ...)`（`native/src/main.cpp:3205-3206`）**无条件打印**，`:3172-3173` 的注释写明「printed unconditionally, including the zero case」。所以「参数被忽略」与「trace 丢了这行」是可分的。
3. 默认退出码是 0；只有加 `--strict-args` 才按 `:3207-3217` 输出 `unrecognized argument(s) refused by --strict-args: …` 并 exit 2。

**因此契约的要求改成三条（比原文更硬）**：

- (a) 组件落地后，`--export-video` 必须**被识别**；未装组件时**不得**再进 `unrecognizedArguments`。否则门禁无法把「组件没装」与「参数拼错」分开 —— 两者都会在 `args: unrecognized=` 那条线上出现。
- (b) 未装组件必须给具名 reason 且**非 0 退出码**（本文件 §5 的 `export=FAIL reason=component_not_installed` + 退出码 13）。「静默忽略」在这里是明确禁止项，因为 D-23 的默认行为正好就是静默忽略。
- (c) **凡新写的导出门禁一律带 `--strict-args`**，并在断言里同时检查 `export=` 行与 `args: unrecognized=0`。理由：D-23 让拼错的参数默认不报错，本轮已经因此类「默认不报错」的设计踩过坑（见 `docs/DEFECT-REGISTER-ADDENDUM-D51-D56-2026-11-15.md` D53/D55）。

### 12.4 顺带确认

- 本文件 §1.4 说 `install_lifecycle.cpp:794-797` 的注释引用了一个失效行号（真实断言在 `install-lifecycle-check.sh:412-413` 与 `:428-430`）：**已复核该说法成立**，属「注释当证据」家族（与方法学教训七同源），记为文档行号漂移，不新开缺陷号；但它必须在本文件与 EXEC-PLAN 各留一行，防止下一个人照着那句注释去改门禁。
- 本文件 §1.6 的「`native/src` 里 0 处组件代码」与 §1.1 的 `install_manifest.cpp:384` 未知键拒绝：两点我都核过，成立（`else return bad("unknown manifest key");`）。因此「每组件一份清单」是**被机制逼出来的选择**，不是风格偏好 —— 这一条要在实施卡的 Acceptance 里原文引用，避免后人改回「核心清单加字段」。
