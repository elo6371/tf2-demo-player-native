# TF2 本地资源盘点与 VPK 可查询能力（2026-10-03）

## 结论

本机 `D:\SteamLibrary\steamapps\common\Team Fortress 2\tf` 的基础 VPK 资源可被当前原生工程打开并按路径读取。为支持后续模型、音效和粒子按类别索引，本轮给 `VpkArchive` 增加了确定性 `list(prefix, extension)` 查询接口；它只扫描已经加载到内存的 VPK 索引树，不读取或复制整包数据。

这一步不是模型、材质、粒子或音频运行时完成的证明；它只证明资源索引层可以稳定提供下一层需要的候选路径。

## 真实资源读数

测试目录：`D:\SteamLibrary\steamapps\common\Team Fortress 2\tf`

| VPK | entries | `.mdl` | `.vvd` | `.vtx` | `.phy` | `.vmt` | `.vtf` | `.pcf` | `.wav` | `.mp3` |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| `tf2_misc_dir.vpk` | 104090 | 14407 | 14363 | 43086 | 4757 | 26058 | 0 | 135 | 0 | 0 |
| `tf2_sound_misc_dir.vpk` | 3230 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 2757 | 472 |
| `tf2_sound_vo_english_dir.vpk` | 12728 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 60 | 12668 |
| `tf2_textures_dir.vpk` | 29636 | 0 | 0 | 0 | 0 | 0 | 29636 | 0 | 0 | 0 |

上述数据来自临时独立 C++ 检查程序，使用工程相同的 `VpkArchive::open/list` 实现。检查程序只读真实 VPK，不改游戏目录。

## 本轮改动

- `native/include/vpk_archive.h`
  - 新增 `list(prefix, extension)`。
  - 返回规范化的小写 `/` 路径。
  - `prefix` 按目录边界匹配，避免 `models/player2` 被误匹配到 `models/player`。
- `native/src/vpk_archive.cpp`
  - 对前缀和扩展名沿用现有规范化规则。
  - 返回结果排序，保证扫描结果不依赖 `unordered_map` 顺序。
  - 不改变 `open/contains/read` 行为。

## 验证

- `cmake --preset windows-release -S native`：通过。
- `cmake --build native/build --config Release --parallel 2`：通过，生成 `native/build/Release/tf2_demo_native.exe`。
- 独立 VPK 检查程序：成功打开 `tf2_misc_dir.vpk`，读到 `104090` 条目、`14407` 个 MDL、`26058` 个 VMT、`135` 个 PCF；成功打开声音和纹理 VPK 并读到上表计数。
- `git diff --check`：通过。

- 原生 Release 真实 Demo 烟测：`pl_upward_f12.dem` 启动 4 秒保持响应，标题读到 `temp=35566`、`tefail=0`、`sounds=22928`、`entities=78093`；该样本的地图 BSP 精确版本仍缺失，标题将 BSP 标为 invalid。`r`n`r`n## 资源边界与后续接入

- `VtfTexture` 当前可以解码既有支持的 VTF 格式，但 VTF 路径不等于材质着色器已经接入；VMT 的 bumpmap、Phong、selfillum、envmap、Water 仍需渲染器实现。
- VPK 中存在完整的模型配套文件，但当前工程没有 MDL/VVD/VTX 解析器，不能把文件计数当作人物、武器或 ViewModel 已完成。
- VPK 中存在武器音效、脚步相关音频和其他音频，但当前工程没有音频解码、事件映射和 Demo tick 播放链，不能宣称声音已完成。
- `list` 的前缀是目录前缀；例如查询 `models/weapons` 后再按文件名筛选 `v_`/`w_`，不会把文件名片段误当目录。
- PCF 仅证明索引可见；PCF 二进制解析、粒子算子、材质采样和 TempEntity 驱动仍未完成。

## 推荐下一步

1. 由资源层调用 `list("sound", ".wav")` 和 `list("particles", ".pcf")` 建立小型按需索引，不复制 VPK。
2. 音频先实现 WAV 头/PCM 解码和按事件路径读取，再接 Demo tick 时序。
3. 模型先实现 MDL/VVD/VTX 头和依赖路径校验，再做骨骼与蒙皮。
4. 粒子只先覆盖火箭尾迹、爆炸和枪口火焰所需的 PCF renderer/operator 子集。

