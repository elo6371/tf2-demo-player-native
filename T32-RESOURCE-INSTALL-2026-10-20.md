# T32 — 资源发现与安装就绪度 交付报告

- 任务：T32（Resource discovery and installation readiness），team board `task-4`
- 分支：`task/t32-resource-install`
- worktree：`D:\TF2_Native_Worktrees\t32-resource-install`
- 基线：`36303c2`（"test: record post-merge T26 closure"）
- 本轮提交：产品修复 → 门禁/测试 → 证据 → 报告（提交号见 §13）
- 推送状态：**未推送**；Lead 负责 review 与合并到 `p0-entity-protocol`
- 日期：2026-10-20

## 0. 结论摘要

**一句话：** 干净 staging 目录可运行、可迁移，首次启动/重启/显式 root/缺失 root 都有具名读数；安装后的二进制上逐项验证了 store 优先级与损坏 VPK；但**没有安装器**——staging 不是安装包，5 项 installer 特性缺席，全部列为 release blocker（owner=T28）。

1. **包本身**：`cmake --install` 只产出 3 个文件（`tf2_demo_native.exe` 1,819,136 B、`native_install_smoke_probe.exe` 297,984 B、`install_smoke_probe.ps1` 812 B）。安装的 exe 与构建 exe SHA-256 逐字节相同；每个 exe 的 import 全部命中 `System32`，MSVC runtime 静态链接（没有 vcruntime140/msvcp140 依赖）→ 包目录自带除 TF2 资源以外的一切。
2. **产品修复**：以前"磁盘上存在但打不开的 VPK"被静默跳过，最终读数退化成 `store=none reason=not found in maps, download/maps or a vpk`，把**资源损坏**误报成**地图不存在**。现在产品用 `VpkArchive::open` 的具名原因报告（`vpk: refused file=... reason=...`）并计数（`vpk: archives opened=N refused=M`）；标题在 refused>0 时追加 `, VPK refused: N`；缺 `--tf-root` 时标题带失败路径；新增 `resources: requested=... tf_root=... valid=...` trace。三者都只增不改：干净安装的读数与修复前完全一致（refused=0 时标题不变）。
3. **生命周期**（安装后的 exe，cwd=安装目录）：首次启动（无 `--tf-root`，自动发现本机 Steam 安装）→ 关闭 → 重启（复用首次保存的 root）→ 显式 `--tf-root` → 缺失 root（valid=0，不回落 Steam，标题带请求路径）→ 空 root（存在但无资源，同样 valid=0）→ 迁移（整目录复制到另一父目录后仍连接）→ 卸载（删目录，无进程、无安装目录残留）。产品运行不向安装目录写任何文件（运行前后 hash 清单一致）。
4. **store 矩阵**（干净安装目录里的 exe + 由钉住 demo/地图生成的合成 root）：loose > VPK 优先级成立（同名地图：loose 是有效 BSP、VPK 里是坏字节 → `store=maps` 且 BSP 解析出正三角形数）；VPK-only → `store=vpk`；`download/maps` → `store=download-maps`；缺失地图 → `store=none`；空/截断/损坏（inline offset = 0xFFFFFFF0 的 32 位回绕边界）三种 VPK → 三个不同的具名拒绝，标题带 refused 计数。
5. **mutation**：M1 优先级反转、M2 接受越界条目（32 位窄化）、M3 `install(TARGETS)` 少装文件，三个都被门禁抓住；源码逐字节恢复（cmp）并重建，恢复后的参考读数复现。
6. **不可宣称**：这不是安装器验收。无 MSI/Inno/NSIS/CPack、无卸载器、无版本/升级元数据、无快捷方式/文件关联；TF2 资源不打进包（设计如此，产品必须定位或提示）。详见 §9。

## 1. 输入与命令

### 1.1 精确输入

| 项 | 值 |
|---|---|
| 钉住 demo | `D:\SteamLibrary\steamapps\common\Team Fortress 2\tf\demos\autorecord_2026-07-02_13-26-46.dem`（21,856,667 B，header map=`pl_odyssey`，SourceTV/POV 分类见 T9） |
| 参考地图（demo 自己的地图） | `D:\SteamLibrary\steamapps\common\Team Fortress 2\tf\maps\pl_odyssey.bsp`（131,192,482 B） |
| fixture BSP | `D:\SteamLibrary\steamapps\common\Team Fortress 2\tf\maps\itemtest.bsp`（2,897,004 B，官方地图；只作为合成 root 的"有效 BSP 载荷"，见 §5.1 的理由） |
| 资源根（真实安装） | `D:\SteamLibrary\steamapps\common\Team Fortress 2\tf` |
| 安装目录 | `<out>/install`（`cmake --install native/build-nmake --config Release --prefix ...` 从零创建，不来自开发者树） |
| 合成 root | `<out>/fixtures/roots/{precedence,vpk-only,download-maps,missing-map,vpk-corrupt,vpk-empty,vpk-truncated,empty}`，由 `install-readiness-fixtures.py` 每次从上面两项重建 |
| 安装包清单/哈希 | `<out>/package-files.txt`、`<out>/package-hashes.txt`（冻结进 `evidence/t32-install/`） |

### 1.2 命令行

```bash
cd D:/TF2_Native_Worktrees/t32-resource-install

# 只构建受影响的 4 个 target（增量；rc=0，10 个既有 warning、0 error）
bash build-target.sh tf2_demo_native native_install_smoke_probe vpk_integrity_probe map_lookup_probe

# 前向门禁（一次 host lock 序列），staging + 13 段断言
bash /d/TF2_Native_Worktrees/_shared/hostlock.sh run t32 \
  bash .scratch/t32/run-forward.sh .scratch/t32/forward-20261020

# mutation 门禁（第二次 host lock 序列）
bash /d/TF2_Native_Worktrees/_shared/hostlock.sh run t32 \
  bash .scratch/t32/run-mutation.sh .scratch/t32/forward-20261020
```

`build-target.sh` 的完整构建日志见 `evidence/t32-install/build.log`；forward/mutation 输出见同目录 `gate-forward.log`、`gate-mutation.log`。退出码与 verdict 行见 §6、§7。

## 2. 改动文件与理由

| 文件 | 改动 | 理由 |
|---|---|---|
| `native/src/main.cpp` | ① 在 root 发现后新增 `resources: requested=... tf_root=... valid=...` trace；② 固定 VPK 名单打开失败时区分"文件不存在"（正常）与"文件存在但打不开"（资源损坏），后者用 `VpkArchive::open` 的具名原因写 `vpk: refused ...` 并计数，另写 `vpk: archives opened=N refused=M`；③ 标题在 refused>0 时追加 `, VPK refused: N`；④ `--tf-root` 请求被拒时标题带失败路径 | 目标要求"资源缺失/损坏时明确失败"。修复前坏 VPK 被静默跳过，读数退化成"地图不存在"；这是**归因错误**，不只缺一行诊断。正常安装（refused=0）读数逐字节不变 |
| `title-capture.ps1` | 新增可选参数 `-ExePath`（默认仍是 `native/build-nmake/tf2_demo_native.exe`）与 `-SettingsFile`（复用已有设置文件） | 安装目录的 exe 必须用同一个 record→WM_CLOSE→exit 的采集器；restart 用例必须复用首次启动写下的设置文件，否则又变成一次首次启动。默认行为不变 |
| `install-readiness-fixtures.py` | 新增：从钉住 demo 重写 260 字节 header map 字段，并生成 8 个合成 tf root（loose/VPK/download/missing/空/截断/损坏/空目录）与对应 VPK 字节 | 优先级与损坏 VPK 必须用**产品没见过的 root** 测，不能拿本机 Steam 安装冒充；fixture 每次从真实输入重建，不会漂移 |
| `install-readiness-capture.ps1` | 新增：用安装目录 exe 跑 lifecycle 6 例 + store 7 例，逐例记录命令、trace、标题、退出码 | bash 只做判定；产品运行必须由 PowerShell 采集并保证 owned-window 与 orderly close |
| `install-readiness-check.sh` | 新增门禁：13 段前向（包清单/依赖闭包/首次启动/重启/显式/缺失/空 root/迁移/卸载/store 矩阵/损坏 VPK/链优先级/installer 缺口）+ 3 个 mutation | 把每条 T32 结论变成会转红的断言；mutation 必须让前向读数离开固定值 |
| `.gitignore` | 不修改 | 机器产物按仓库既有规则放 `.scratch/t32/`（已被忽略）；`evidence/t32-install/` 只放报告引用的冻结证据 |

## 3. 干净安装包（staging）

### 3.1 包清单（`<out>/package-files.txt`）

| 文件 | 字节 | SHA-256 |
|---|---|---|
| `tf2_demo_native.exe` | 1,819,136 | `CF46FE8EFB4F704B2769B609687FDF9945B842E1363807C1E1BD6609D23ADC7B` |
| `native_install_smoke_probe.exe` | 297,984 | `1BC8D33B22931FC1D3FD5EF41AF7B5775E47A5D8ABDE0578ACFF658A497C8855` |
| `install_smoke_probe.ps1` | 812 | `9AF2E272A77F9E154474D9C681F07DBBCB5E7108DD66DA6205251640AE70FEC6` |

安装的 `tf2_demo_native.exe` 与 `native/build-nmake/tf2_demo_native.exe` 的 SHA-256 相同（门禁断言"installed product is byte-identical to the built one"）。包内没有 `settings.ini`、没有 cache 目录：用户态不落在安装目录。

### 3.2 运行时依赖闭包（`dumpbin /dependents`）

| exe | import（全部命中 `C:\Windows\System32`，包内无需附带） |
|---|---|
| `tf2_demo_native.exe` | `d3d11.dll`, `D3DCOMPILER_47.dll`, `USER32.dll`, `SHELL32.dll`, `ADVAPI32.dll`, `ole32.dll`, `WINMM.dll`, `MSACM32.dll`, `bcrypt.dll`, `COMCTL32.dll`, `COMDLG32.dll`, `KERNEL32.dll` |
| `native_install_smoke_probe.exe` | `d3d11.dll`, `KERNEL32.dll` |

- **MSVC runtime 是静态链接的**（`CMAKE_MSVC_RUNTIME_LIBRARY MultiThreaded`）：import 表里没有 `vcruntime140*.dll` / `msvcp140*.dll`，所以包不需要附带 redistributable。门禁把"出现 VC runtime import"当作包装不完整的具名失败。
- T9 的 `install_smoke_probe.ps1` 只硬编码检查 4 个 System32 DLL（`d3d11`/`dxgi`/`d3dcompiler_47`/`winmm`），漏了产品实际 import 的 `msacm32`/`bcrypt`/`comctl32`/`comdlg32`/`ole32`/`advapi32`/`shell32`/`user32`。本门禁不依赖那份名单，而是对**每个安装的 exe 重新 dump import 并逐个判 System32/包内**，因此 T9 记录的"名单不全"不再是漏洞；`install_smoke_probe.ps1` 本身按 T9 的边界保持原样。

### 3.3 安装布局的静态事实（决定"能不能迁移/会不会写安装目录"）

- 设置文件与 cache：`settings_store.cpp` 用 `FOLDERID_LocalAppData` → `%LOCALAPPDATA%\TF2 Demo Player\settings.ini`；cache 由 `main.cpp: localCacheDirectory()` 决定，同样在 LocalAppData。安装目录不需要写权限。
- 产品源码里没有 `GetModuleFileName`/exe 相对路径读取：所有资源都经 `--tf-root`/持久化 root/Steam 候选定位。因此整个安装目录可以任意搬移（迁移用例已验证）。
- 没有 registry 写入、没有服务、没有开机启动项（源码检索无 `RegSetValue`/`RegCreateKey` 调用；`advapi32` 只来自 Steam 注册表读取）。

## 4. 生命周期（安装目录里的 exe）

全部运行都是 `<out>/install/tf2_demo_native.exe`（安装目录里的 exe），工作目录=安装目录，`--hidden-window --audio-disabled --start-paused --settings-file <case>/settings.ini`。`--start-paused` 让窗口的首个标题就是最终标题；关闭用 `WM_CLOSE`（有序退出），不用 kill。

| 用例 | 启动参数 | exit | `resources valid` | 产品报告的 connected root | 其他读数 |
|---|---|---|---|---|---|
| first-launch | 无 `--tf-root`（自动发现） | 0 | 1 | `D:\SteamLibrary\steamapps\common\Team Fortress 2\tf` | 标题含 `[VPK entries: ...]`；refused 段不出现；首次启动写下 settings.ini，`tfRoot` = 同一 root |
| restart | 无 `--tf-root`，复用 first-launch 的 settings.ini | 0 | 1 | 同一 root | 复用持久化 root 成功；restart 目录没有生成第二份 settings.ini |
| explicit-root | `--tf-root <真实 tf>` | 0 | 1 | 同一 root | 显式 root 是唯一候选 |
| missing-root | `--tf-root <不存在的目录>` | 0 | 0 | （空） | 标题无 `[VPK entries:`，即没有回落连接本机 Steam 安装；标题带失败路径；`resources: requested=<路径>` 具名 |
| empty-root | `--tf-root <存在但 materials/models/maps 全空的目录>` | 0 | 0 | （空） | 与缺失 root 同类的具名拒绝（T4 曾把这种 root 报成 connected） |
| relocated | 整个安装目录复制到另一父目录后 `--tf-root <真实 tf>` | 0 | 1 | 同一 root | 复制后的包与安装目录逐文件 SHA-256 相同 |

- **关闭**：6 个用例全部 `WM_CLOSE` 后 exit 0，运行后无 `tf2_demo_native.exe` 残留进程。
- **安装目录不被写**：生命周期运行前后对安装目录做完整 SHA-256 清单，`unchanged`（设置与 cache 都在 `%LOCALAPPDATA%`，见 §3.3）。
- **迁移（relocation）**：安装包没有绝对路径/注册表依赖，复制后行为一致（上面 relocated 行）。注意这验证的是"包可迁移"，不是"安装器升级"（后者 NOT VERIFIED，见 §9）。
- **卸载**：没有卸载器；本轮把"卸载"定义为删除安装目录：目录移除后 exe 不存在、无残留进程；用户态（`%LOCALAPPDATA%\TF2 Demo Player`）按设计留在原处——这正是安装器需要决定是否清理的那部分，列为 blocker #2。
- **重装/覆盖**：对同一目录再跑一次 `cmake --install`，包清单与第一次完全一致（`reinstall leaves the package identical = same`），说明 staging 是幂等覆盖；这是升级安装的最小前提，但不等于升级测试。

## 5. store 矩阵（安装目录里的 exe + 干净合成 root）

合成 root 由 `install-readiness-fixtures.py` 从钉住 demo 与 `itemtest.bsp` 生成；fixture demo 的 260 字节 header map 字段改成 `t32_marker`，因此每个用例请求的是同一个地图名，唯一变量是"哪个 store 持有它"。所有运行都用安装目录里的 exe。

### 5.1 store 结果（产品自己的 `map: lookup` 读数）

| 用例 | root 内容 | store | selected | bytes | BSP 标题段 | VPK opened/refused |
|---|---|---|---|---|---|---|
| store-precedence | `maps/t32_marker.bsp`=有效 BSP；`pak01_dir.vpk` 内同名条目=坏字节 | `maps` | `maps/t32_marker.bsp` | 2,897,004 | `2512 tris` | 1 / 0 |
| store-vpk-only | 只有 `pak01_dir.vpk` 内联的 `maps/t32_marker.bsp`=有效 BSP | `vpk` | `maps/t32_marker.bsp` | 2,897,004 | `2512 tris` | 1 / 0 |
| store-download-maps | `download/maps/t32_marker.bsp`=有效 BSP；`maps/` 只有别的名字 | `download-maps` | `download/maps/t32_marker.bsp` | 2,897,004 | `2512 tris` | 0 / 0 |
| store-missing-map | 结构合法但没有任何 store 持有该名字 | `none` | `-` | 0 | `invalid/BSP header is invalid` | 0 / 0 |
| store-vpk-corrupt | `pak01_dir.vpk` 的 inline 条目 offset=0xFFFFFFF0（>4 GiB） | `none` | `-` | 0 | `invalid/BSP header is invalid` | 0 / **1** |
| store-vpk-empty | 0 字节 `pak01_dir.vpk` | `none` | `-` | 0 | `invalid/BSP header is invalid` | 0 / **1** |
| store-vpk-truncated | 只有 8 字节 header 的 `pak01_dir.vpk` | `none` | `-` | 0 | `invalid/BSP header is invalid` | 0 / **1** |

三种损坏档案的三个具名原因互不相同（门禁断言 `distinct reasons = 3`）：

| 用例 | `vpk: refused ... reason=` |
|---|---|
| store-vpk-corrupt | `VPK entry range outside directory archive` |
| store-vpk-empty | `empty VPK file` |
| store-vpk-truncated | `truncated VPK header` |

损坏 VPK 用例的产品标题带 `, VPK refused: 1`（门禁断言 `corrupt-vpk: the title carries the refusal count = 1`）；干净安装（真实 tf root）标题没有这一段，`vpk: archives opened=4 refused=0` —— **修复是加法，干净安装的读数逐字节不变**。

### 5.2 32/64 位 offset 边界

`store-vpk-corrupt` 就是历史 32 位回绕的边界夹具：目录树声明 inline 条目 `offset=0xFFFFFFF0`、`length=4`，文件本身只有 53 字节。

- forward（当前代码，64 位区间校验）：`treeEnd + 0xFFFFFFF0` 远大于文件大小 → `open()` 以 `VPK entry range outside directory archive` 拒绝整档，refused=1，地图不来自 VPK。
- mutation M2 把 `treeEnd` 窄化成 `uint32_t` 后再加 offset（T15 记录的旧 bug 形态）：`53 + 0xFFFFFFF0` 回绕成 37，落入文件范围 → 条目被接受、读出 4 字节"地图"，refused=0、store=vpk。门禁捕获了这一变化（§7）。
- 分卷数据的 64 位边界（`entry.offset > available || length > available - offset`）由 T15 的 `vpk_integrity_probe` 覆盖；本轮不重复，报告不声称安装后的产品路径覆盖它。

### 5.3 loose > VPK > BSP pakfile 的链级优先级（复用 T15 探针）

安装后的产品一次只能走 map 的三个 store（loose maps / download/maps / VPK），BSP pakfile 不进地图查找路径；它是 `AssetStoreChain` 的最后一个材质 store。链级顺序用产品自己的 `AssetStoreChain`/`VpkArchive` 经 `vpk_integrity_probe` 读，而不是写第二份实现：

| 读数键 | 值 | 含义 |
|---|---|---|
| `vpk.precedence.loose-wins` | `loose` | loose 胜过两个 VPK |
| `vpk.precedence.directory-before-volume` | `vpk-directory` | 先加入的归档胜 |
| `vpk.precedence.pakfile` | `pakfile` | pakfile 在最后回答 |
| `vpk.precedence.pakfile-shadowed` | `loose` | pakfile 不能越过 loose |
| `vpk.precedence.absent` | `none` | 都没有时是 none，不是拒绝 |
| `vpk.summary` | `... failures=0` | 探针自身无失败 |

这一段的**安装后产品**替代读数就是 §5.1 的 `store-precedence`（loose 在安装目录的 exe 上胜出）；BSP pakfile 腿只在链级探针上可观察，报告不把它说成安装后端到端路径。

## 6. Forward gate

命令（一次 host lock 序列，见 §1.2）：`bash install-readiness-check.sh --forward .scratch/t32/final`。

- **退出码 0**；verdict 行：`INSTALL-READINESS=PASS`；`cases=13 checks=102 mutations=0`。
- 完整输出：`evidence/t32-install/gate-forward.log`（本报告所有读数都可逐行对照）。

13 段断言（每段都钉具名数值，不只看退出码）：

| 段 | 断言内容 | 关键读数 |
|---|---|---|
| 1/13 前置 | 钉住输入存在、构建与源码一致、运行前无产品进程 | `build fresh = fresh`、`no product process = 0` |
| 2/13 staging | `cmake --install` 到全新目录 | `rc=0` |
| 3/13 包清单 | 文件列表/数量/非空/哈希/无用户态 | `list = 3 files`、`installed product == built product`（`cf46fe8e…`）、`settings/cache = absent` |
| 4/13 依赖闭包 | 每个安装 exe 逐 import 判 System32/包内 | `tf2_demo_native.exe` 12 个全 System32；smoke probe 2 个；`missing = none`；`MSVC runtime = none`（静态链接） |
| 5/13 出厂脚本 | 在安装目录运行 `install_smoke_probe.ps1` 与 `native_install_smoke_probe.exe` | `install_layout=ok`、`tf2_assets_packaged=0`、`install_executable=1 tf_root_missing_prompt=1 warp_fallback=1` |
| 6/13 链优先级 | 复用 `vpk_integrity_probe`（产品自己的 chain） | `loose`、`vpk-directory`、`pakfile` 最后、`pakfile-shadowed=loose`、`absent=none`、`failures=0` |
| 7/13 fixtures | 从钉住 demo+地图重建 8 个合成 root | `roots=8 files=31`，5 个 VPK 的 SHA-256 见 `fixtures-inputs.sha256` |
| 8/13 产品运行 | 安装后的 exe、cwd=安装目录 | `capture rc=0`，13 个 case 全部 `TITLE-CAPTURE=PASS` |
| 9/13 生命周期 | 首次/重启/显式/缺失/空/迁移 + 安装目录不被写 | 全部 exit 0；valid 1/1/1/0/0/1；connected root 全部正确；`install dir unchanged`；`relocated copy same`（§4） |
| 10/13 store 矩阵 | loose>VPK、VPK-only、download/maps、缺失、空/截断/损坏 | `maps`/`vpk`/`download-maps`/`none`；坏 VPK 三个具名 reason 且互异；真实安装 `opened=4 refused=0`（§5） |
| 11/13 重装覆盖 | 对同一目录再 `cmake --install` | `reinstall leaves the package identical = same` |
| 12/13 卸载 | 删除安装目录 | 目录/exe 消失、`no product process = 0` |
| 13/13 installer 缺口 | 源码检索固定 5 个 blocker | `installer defs= none`、`CPack= none`、`uninstall= none`、`version metadata=0`、`gaps=5`（§8） |

其中"产品行为"的钉住读数与 §4/§5 的表格一一对应；门禁把每个用例的 `exit-code.txt`、`trace.txt`、`title-last.txt` 都留在 `<out>/cases/<case>/` 下（机器产物，不提交），所以任何一条断言都可以被复核者重读。

## 7. Mutation gate

命令（同一 host lock 序列的后半段）：`bash install-readiness-check.sh --mutation .scratch/t32/final`。

- **退出码 0**；verdict 行：`INSTALL-READINESS-MUTATION=PASS (3 mutations caught, sources restored)`；`cases=4 checks=39 mutations=3`。
- 完整输出：`evidence/t32-install/gate-mutation.log`。

每个 mutation 的固定流程：改源码 → `build-target.sh` 重建（**先断言 build rc=0**，T9 记录过"构建失败→旧二进制→假绿"）→ 从新构建重新 `cmake --install` → 用**安装后的产品**跑对应 case → 要求读数离开前向固定值 → `cp` 恢复源码 + `cmp` 逐字节证明 → 重建 → 复跑同一 case 得回参考读数。

| mutation | 缺陷 | 前向固定读数 | mutation 下的读数（捕获证据） | 恢复后参考读数 | 源码恢复 |
|---|---|---|---|---|---|
| **M1** 优先级反转 | `map_lookup.cpp` 候选顺序改成 `vpk → maps → download-maps` | `store=maps`、BSP `2512 tris` | `store=vpk`、BSP `invalid/BSP header is invalid`（VPK 里是坏字节，被当成地图） | `store=maps`、`2512 tris` | `cmp` 逐字节相同 |
| **M2** 损坏条目被接受 | `vpk_archive.cpp` 的 `treeEnd + offset` 窄化回 `uint32_t`（T15 记录的 32 位回绕形态） | `store=none`、`refused=1`、reason `VPK entry range outside directory archive` | `store=vpk`、`refused=0`（`offset=0xFFFFFFF0` 的条目回绕后被接受，读出 4 字节当地图） | `store=none`、`refused=1`、同一具名 reason | `cmp` 逐字节相同 |
| **M3** 少装文件 | `native/CMakeLists.txt` 的 `install(TARGETS)` 去掉 `native_install_smoke_probe` | 包 3 文件 | 包 **2** 文件、缺 `native_install_smoke_probe.exe`；生成的 `cmake_install.cmake` 里该名字 hits=0（证明 mutation 真的进了安装脚本） | 包 3 文件、hits=1 | `cmp` 逐字节相同 |

捕获证据（提交在 evidence 里）：`mutation-m1-trace.txt` / `mutation-m1-title.txt`（M1 下 `store=vpk` 与 invalid BSP）、`mutation-m2-trace.txt`（M2 下无 refusal 且 store=vpk）、`mutation-m3-package-files.txt`（2 文件包）。

**恢复的语义边界：** `cmp` 证明的是源码逐字节恢复；紧随的 rebuild 复现的是**参考读数**（`store=maps`+2512 tris、`refused=1`+具名 reason、3 文件包）。没有宣称"恢复后的二进制哈希与前向二进制相同"——本项目的链接段带时间戳，源码相同的两次构建不是逐字节相同（构建树当前 `tf2_demo_native.exe` 的 SHA-256 与前向包里的 `cf46fe8e…` 不同，源码状态由 `git status` 证明相同）。

## 8. 发布 blocker（安装器特性缺席，逐项给 owner）

门禁 §13 用源码检索把这些缺口固定成读数（`installer definition files=none`、`CPack rules=none`、`uninstall rule=none`、`version metadata=0`），不是凭印象列清单。

| # | blocker | 证据 | owner | 为什么 T32 不在本轮实现 |
|---|---|---|---|---|
| 1 | **没有安装器包**（无 MSI/Inno/NSIS/CPack/自解压） | 门禁 §13 `installer definition files in the tree = none`、`CPack/package rules = none`；T9 §8 已记录同样的缺口 | T28（release packaging） | T32 的退出条件允许把 installer 特性列为带 owner 的 blocker；造安装器属于 T28 发版打包 |
| 2 | **没有卸载器** | 门禁 §13 `uninstall rule in native/CMakeLists.txt = none` | T28 | 卸载语义（是否删 `%LOCALAPPDATA%` 用户态）需要发版决策 |
| 3 | **没有版本/升级元数据** | 门禁 §13 `installer version metadata = 0` | T28 | 升级/降级路径需要版本编号与安装器，二者都还没有 |
| 4 | **没有快捷方式/文件关联/Start Menu** | 树内无安装器定义可承载这些项 | T28 | 同上 |
| 5 | **TF2 资源不打进包（设计如此）** | `tf2_assets_packaged=0`（T9 起就是产品自己的声明）；`AssetRoot::valid()` 要求 materials/models/maps 三个 store | 产品 + T28 | 版权与体积都不允许打包 TF2 资产。产品已实现"自动发现 Steam 安装 / 显式 `--tf-root` / 缺失时具名失败"，本轮验证了这三条；安装器只需要调用/提示 |

**明确不可宣称：** `cmake --install` 的 staging 目录不是安装包验收。T32 验证的是"安装目录形态的运行与资源就绪"，installer 生命周期（安装到 Program Files、注册表、卸载残留、升级覆盖）全部 `NOT VERIFIED`，见 §10。

## 9. 限制与 NOT VERIFIED

按"不可用输入/不可观察路径 → NOT VERIFIED + 最便宜下一步"记录，未把任何一项改写成通过。

| # | NOT VERIFIED | 原因（不可用输入/不可观察） | 最便宜的下一步 |
|---|---|---|---|
| 1 | 真正的**干净机器**（目标机没有任何 Steam/TF2 安装）上的首次启动 | 本机有 Steam 安装，首次启动的自动发现能成功是"有 TF2 的干净目录"场景；"完全没有 TF2"只能用显式缺失/空 root 模拟（已验具名拒绝），无法观察安装器在目标机上的提示 UI | 一台无 Steam 的 Windows VM，跑同样命令；`run-forward.sh` 里把 `-TfRoot` 换成不存在路径即可 |
| 2 | **安装器生命周期**：Program Files/UAC 安装、注册表项、Start Menu、文件关联、卸载残留、升级/降级、多用户/非管理员 | 没有安装器，输入不存在 | 实现 MSI/Inno 后：把本门禁的 `--prefix` 换成安装器输出目录，并把 §8 的 5 条 blocker 断言翻成"必须存在" |
| 3 | 安装包**签名/版本校验**、静默安装参数 | 同 #2 | 同 #2 |
| 4 | **分卷数据文件**的 64 位越界（`entry.offset > available`）在安装后的产品路径上 | 安装后的产品只经 map 查找走 inline 条目；分卷读取由 T15 的 `vpk_integrity_probe` 覆盖，本轮没有把分卷 VPK 放进合成 root | 在 `install-readiness-fixtures.py` 加一个 `vpk-volume` root（T15 已有卷夹具字节），用例断言 `vpk-volume` store |
| 5 | **BSP pakfile 材质**在安装后产品上的端到端优先级 | 需要一张 pakfile 内含材质的 BSP 与引用它的 demo；本轮只在链级探针上覆盖（§5.3） | 用 pl_odyssey 的 pakfile-only `$bumpmap` 路径跑 `world-material-check.sh`（T24 已有夹具） |
| 6 | **loose 地图文件本身损坏**（存在但 BSP 坏）的具名读数 | 本轮覆盖的是"缺失地图"与"损坏 VPK"；loose 坏 BSP 会走 `BSP: invalid/...` 但未单独立 case | 在 fixtures 加 `root-bsp-corrupt`（maps/t32_marker.bsp=坏字节）一个 case，约 5 分钟 |
| 7 | 默认（非 `--settings-file`）**用户态路径**的真实写入与卸载清理 | 本轮所有运行都用 `--settings-file` 指向 case 目录，避免污染真实 `%LOCALAPPDATA%`；`FOLDERID_LocalAppData` 路径是静态核对的 | 在 VM 上跑一次不带 `--settings-file` 的启动，检查 `%LOCALAPPDATA%\TF2 Demo Player\settings.ini` 与卸载后是否残留 |
| 8 | 音频设备路径 | 全部运行 `--audio-disabled`（T31 的范围；本任务不碰音频设备） | 由 T31/T28 的设备验收覆盖 |

另外两条"读了代码但没有端到端读数"的边界，明确写出以免被当成读数：

- 产品对 `download/` 目录的 VPK 说法：`download/` 不是 VPK 候选（`AssetRoot::valid()`/map 查找只把 `download/maps` 当 loose；VPK 只能位于 tf root 顶层）。本轮没有构造"download 目录里放 VPK"的用例，因为产品的查找计划里根本没有这个 store。
- `install_smoke_probe.ps1` 的 System32 名单仍是 T9 的 4 项（`d3d11`/`dxgi`/`d3dcompiler_47`/`winmm`）。本门禁不依赖它，改用逐 exe 的 import 闭包（§3.2）；没有修改出厂脚本，以免越过 T9 的边界。

## 10. 工作区与进程状态

- worktree：`D:\TF2_Native_Worktrees\t32-resource-install`，分支 `task/t32-resource-install`，基线 `36303c2`。
- 提交后 `git status --short` 为空（未跟踪文件全部进提交；机器产物在 `.scratch/t32/`，已被 `.gitignore` 忽略）。
- 本序列以 `LOCKED-COMMAND-EXIT rc=0 owner=t32` 结束，`hostlock.sh` 的 trap 在命令退出时释放锁（之后锁被同主机其它任务持有是共享主机的正常现象，与本任务无关）；序列自身的 13 个 capture 全部 exit 0，运行后没有本任务留下的 `tf2_demo_native.exe` / `*_probe.exe` 进程。
- mutation 的源码备份在 `.scratch/t32/<run>/mutation/backup/`（不提交）；三份源码在序列结束时与备份 `cmp` 逐字节相同，并由紧随的 rebuild + restore 参考读数复现。

## 11. 复核方式

```bash
cd D:/TF2_Native_Worktrees/t32-resource-install
bash build-target.sh tf2_demo_native native_install_smoke_probe vpk_integrity_probe map_lookup_probe
bash /d/TF2_Native_Worktrees/_shared/hostlock.sh run t32 \
  bash .scratch/t32/run-forward.sh .scratch/t32/forward-20261020
# 期望：INSTALL-READINESS=PASS，退出码 0，日志与 evidence/t32-install/gate-forward.log 对照
bash /d/TF2_Native_Worktrees/_shared/hostlock.sh run t32 \
  bash .scratch/t32/run-mutation.sh .scratch/t32/forward-20261020
# 期望：INSTALL-READINESS-MUTATION=PASS (3 mutations caught, sources restored)
```

复核者可直接看 `evidence/t32-install/gate-forward.log` 与 `gate-mutation.log` 的 `RESULT`/`VERDICT` 行；合成 root 与 capture 是机器产物，按 §1.1 的命令可重建。

## 12. 证据清单

| 文件 | 内容 |
|---|---|
| `evidence/t32-install/gate-forward.log` | 前向门禁完整输出（13 段、全部 OK/FAIL 行、verdict） |
| `evidence/t32-install/gate-mutation.log` | mutation 门禁完整输出（3 个 mutation、restore、verdict） |
| `evidence/t32-install/package-files.txt` | 包清单（相对路径 + 字节数） |
| `evidence/t32-install/package-hashes.txt` | 包内每个文件的 SHA-256 |
| `evidence/t32-install/runtime-imports.txt` | 两个安装 exe 的 import 闭包 |
| `evidence/t32-install/fixtures-inputs.sha256` | 8 个合成 root + fixture demo 的 SHA-256 |
| `evidence/t32-install/cmake-install.log` | `cmake --install` 输出 |
| `evidence/t32-install/install-smoke.log` | 出厂 `install_smoke_probe.ps1` 在安装目录上的输出 |
| `evidence/t32-install/fixtures.log` | fixture 生成器输出 |
| `evidence/t32-install/mutation-m1-trace.txt` / `mutation-m1-title.txt` | M1 反向优先级下的真实 trace/标题（捕获证据） |
| `evidence/t32-install/mutation-m2-trace.txt` | M2 接受越界条目下的真实 trace |
| `evidence/t32-install/mutation-m3-package-files.txt` | M3 少装文件的包清单 |
| `evidence/t32-install/source-hashes.txt` | 改动源码与门禁脚本的 SHA-256 |
| `evidence/t32-install/build.log` | `build-target.sh` 的 4 target 构建日志（10 warning、0 error） |

## 13. 提交

分支 `task/t32-resource-install` 基于 `36303c2`，共 3 个提交，**未推送**（Lead 负责 review 与合并）：

| commit | 标题 | 内容 |
|---|---|---|
| `4e1a23d` | `fix(product): report a present but unreadable VPK instead of skipping it` | `native/src/main.cpp`（§2） |
| `aceb852` | `test(t32): install-readiness gate, clean-room store fixtures and capture` | `install-readiness-check.sh`、`install-readiness-fixtures.py`、`install-readiness-capture.ps1`、`title-capture.ps1`、`evidence/t32-install/`（15 个证据文件） |
| 本报告提交 | `docs: record T32 install-readiness acceptance and installer blockers` | `T32-RESOURCE-INSTALL-2026-10-20.md` |

提交后的工作区状态与残留进程状态见 §10；`git status --short` 为空、分支只有这 3 个提交。
