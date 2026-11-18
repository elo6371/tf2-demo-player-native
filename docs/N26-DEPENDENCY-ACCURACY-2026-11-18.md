# N26 后续：头依赖到底准不准，以及那个 stamp 守卫该不该留

2026-11-18 · 工程线（N26 账上「下一步：核实构建依赖准确性与共享头实际扇出」）

## 0. 一句话

上一轮（`docs/N26-BUILD-MEASUREMENT-2026-11-18.md`）测到「改一个公共头 = 81/86 TU = 118.88 s」，并把它归因于「`native_renderer.h` 被几乎所有 TU 包含」。**这个归因是错的。**今天实测：真正包含它的目标文件只有 19 个（占 92 个 obj 的 21%），118.88 s 来自 `build-target.sh` 的 stamp 守卫——它把头改动一律放大成 touch-all 全量重编。守卫的原始理由（依赖文件全空、nmake 没有头依赖）在当前树上**已经不成立**：`compiler_depend.make` 有真实内容，nmake 会据此只重编该重编的 obj。按这条依据改了守卫，并保留了「依赖文件里查不到这个头」时的回退触发。

## 1. 口径

| 项 | 值 |
| --- | --- |
| 构建目录 | `native/build-n26b`（本轮新建，与门禁使用的 `native/build-nmake`、上一轮的 `native/build-n26` 分开） |
| 目标集 | 7 个：`tf2_demo_native` + `presentation_probe` `entity_model_probe` `world_material_probe` `model_pose_probe` `item_schema_probe` `viewmodel_fov_probe`（和 verify-fast 的目标集一致），共 **92 个 obj** |
| 计时 | `date +%s` 包住整次 `bash build-target.sh …` |
| 重编计数 | `find "$B" -name '*.obj' -newer <marker>`（按时间戳，不数日志行：NMake Makefiles 成功时不打印编译命令） |
| 静态预测 | 扫描「本次真的编译的 TU 集合」的 `#include` 闭包，统计每个头的包含者数；TU 集合取自 obj 路径反推的源码路径（92 个 obj 全部匹配上） |
| 工具链 | 与上一轮同一台机器、同一个 MSVC 14.44.35207、CMake 4.4.3 NMake Makefiles、Release |

为什么预测要用「本次真的编译的 TU 集合」而不是全树：全树有 136 个 TU（`native/src` 82 + `native/tools` 54），但一次 `build-target.sh` 只建 7 个目标，`native/tools` 里只有一部分参与。用全树扫出来的 `native_renderer.h` 扇出是 19（src 18 + tools 1），`item_schema.h` 是 42（src 27 + tools 15），后者会明显高估——因为 15 个 tools 一般不在这 7 个目标里。按实际 obj 集合扫，预测就是下面的 19 / 12 / 31。

## 2. 读数

### 2.1 依赖准确性（绕开守卫，stamp 抬到所有头之后）

| 改哪个头 | 静态预测的重编数 | 实测重编 obj | 墙钟 |
| --- | --- | --- | --- |
| `native/include/hud_font.h` | 12 | **12** | 25 s（改后脚本再一次：24 s） |
| `native/include/native_renderer.h` | 19 | **19** | 62 s |
| `native/include/item_schema.h` | 31 | **31** | 79 s |

三个头、三次实测、预测与实测**逐条相等**。预测来自源码 include 扫描，实测来自 nmake 自己的决定，是两个独立来源——这种情况下相等，说明 `compiler_depend.make` 不是「文件里写着但没人用」，nmake 真的按它重编。

### 2.2 守卫的成本（改前 vs 改后）

| 场景 | 改前（touch-all 守卫） | 改后（信任依赖文件） |
| --- | --- | --- |
| clean 全量（7 目标，92 obj） | — | 135 s |
| no-op | — | 6 s / 12 s（0 obj） |
| `native_renderer.h` | **129 s / 87 obj** | **62 s / 19 obj** |
| `hud_font.h`（窄头） | 129 s 量级 / 87 obj | **24 s / 12 obj** |
| `item_schema.h`（中扇出） | 129 s 量级 / 87 obj | **79 s / 31 obj** |

guard 生效那一次是 87 个 obj，不是 92：守卫 touch 的是 `native/src/*.cpp` 与 `native/tools/*.cpp`，5 个 `generated/` 方向的 obj 不在其中。这个细节也写进读数，因为「87 / 92」和「全量」不是同一个数。

### 2.3 回退分支（改后脚本）

新建一个未跟踪的 `native/include/zz-n26-untracked-probe.h`（空文件，不被任何 TU 包含）：

- 不加 `TF2_IDENTITY_ALLOW_UNTRACKED=1` 时构建**直接拒绝**——D-90 的 build identity 不允许未跟踪的编译输入进入内容哈希（`native/cmake/gen-build-stamp.cmake:293`）。这不是守卫的问题，是身份脚本的既有约束。
- 加上该环境变量后构建继续，日志出现 `note: no depend file mentions native/include/zz-n26-untracked-probe.h; touching sources (header deps unknown)`，重编 88 个 obj、155 s——**回退分支按设计触发**，行为与改前一致。

## 3. 改了什么

`build-target.sh` 两处，一是守卫本身（现在是 `:143-166`），二是文件头那段已经过时的理由说明（`:34-53`）：

1. 守卫从「任何头比 stamp 新 → touch 所有源文件」改成：先收集比 stamp 新的头；逐个在 `"$BUILD"/CMakeFiles/*/compiler_depend.make` 里查（`grep -qF`，路径按 Windows 反斜杠尾部匹配）；**只要有一个头在所有依赖文件里都查不到**，才退回原来的 touch-all 并打印是哪个头；否则只打印一行 note，交给 nmake 按依赖文件重编。
2. 文件头注释从「HEADER DEPENDENCIES ARE NOT TRACKED」改成如今的实测结论，保留 2026-10-11 那次事故（`skinning=1` / `alignment=-`）作为历史，并写明回退分支同时覆盖「本地化 prefix 的 bug 复发」这一情形：依赖文件全空时，任何头都查不到，守卫自动回到 touch-all。

设计上这是「双向安全」：依赖准确 → 只重编该重编的；依赖缺失 → 退回全量。不会出现「以为依赖在、其实不在」的第三种状态，因为判据就是「这个头有没有被任何依赖文件点名」。

## 4. 这还没有证明什么

- **只测了 3 个头**，而这 92 个 obj 头上的项目头有几十个。六个头里挑的这三个覆盖了窄（12）、中（31）、宽（19）三档，但没有全表取样。
- **只测了 7 个目标**。54 个探针目标的全量构建时间、以及它们各自的依赖文件是否同样准确，没有测。
- **改后没有重建门禁用的 exe**，也没有重跑 `verify-fast.sh` / `verify-all.sh`。这一批门禁运行用的是改前那次构建的 exe；`build-target.sh` 的改动只影响「要不要 touch 源文件」，不影响编译命令与产物内容，但这是推理，不是本轮实测。
- **「预测 = 实测」是弱一致性论证**：两个来源独立（源码扫描 vs nmake 决定），但都建立在同一份 `.cpp` 文本上；它证明「nmake 用了依赖数据」，不证明「依赖数据覆盖了所有语义上的包含关系」（例如某个头经 `/FI` 强制包含、或经模块/PCH 间接生效，本轮没有这类样本）。
- **时间戳精度边界没测**：1 秒内连续两次改头、或 stamp 与头同一时间戳的情形，行为未验证。
- **回退分支的触发面**：D-90 让「未跟踪头」根本建不了，所以真实工作流里回退分支只能由「已跟踪、但从未参与过本构建树任何一次编译」的头触发（例如新拉下来的分支里的头）。这类头在真实工作里出现的频率没有统计。
- **没有改 CMake 生成器**，也没有上缓存/PCH：本轮只把「守卫的浪费」去掉，头扇出本身（`native_renderer.h` 被 19 个 obj 包含）没有动。

## 5. 复现

```bash
cd /d/TF2_Native_Worktrees/t105-integrate
bash .scratch/n26/deps.sh      # clean / 宽头 / 窄头 / 守卫生效，四个场景
bash .scratch/n26/guard2.sh    # 改后脚本：no-op / 窄头 / 中扇出 / 未跟踪头回退
python .scratch/n26/fanout-built.py   # 按实际 obj 集合算静态预测（12 / 19 / 31）
```

脚本都不在 git 里（`.scratch/` 被忽略），读数与判据以本文件为准。
