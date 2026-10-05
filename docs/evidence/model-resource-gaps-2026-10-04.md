# 模型挂点、饰品、bodygroup 与 ViewModel 缺口诊断（2026-10-04）

## 结论

当前原生工程没有可验证的饰品实体绑定、bodygroup 选择或第一人称 ViewModel 渲染链。模型资源层现在会把 attachment、bodygroup、ViewModel 三类状态显式写入 inspection 和窗口诊断；这些状态仍未进入玩家姿态、饰品或 GPU 蒙皮绘制。

## 已验证能力

| 能力 | 证据 | 状态 |
| --- | --- | --- |
| MDL attachment 表 | `native/include/model_loader.h` 的 `ModelAttachment`；`native/src/model_loader.cpp` 解析 attachment name/flags/bone/origin/basis | 已实现，资源检查级 |
| 骨骼元数据 | `ModelBone` 解析 name/parent/position | 已实现，未做姿态求值 |
| 模型伴随资源 | `ModelLoader::resolveAsset` 检查 MDL、VVD、DX90/DX80/SW VTX、PHY | 已实现；缺失明确进入诊断 |
| bind-pose 顶点 | `buildBindPoseMesh` 从 VVD/VTX 描述构造 `ModelDrawVertex` | 已实现，未做蒙皮 |
| 饰品挂接 | 没有饰品实体到 `ModelAttachment` 或玩家骨骼的调用链 | **Unknown**；MDL attachment 表存在时报告 `Available` |
| bodygroup | 现在读取 MDL bodypart 的 name/modelCount/base/modelIndex；运行时选择值仍未解码 | **Unknown** |
| ViewModel / 第一人称 | 没有 ViewModel 实体绑定或第一人称绘制调用 | 普通模型 **Missing**；`v_*.mdl` 仅 **Unknown** |
| GPU 蒙皮与动作 | `native_renderer.cpp` 仅上传 bind-pose 顶点；无 bone matrix/sequence evaluation | **Missing** |

## 骨骼/attachment 前置探针

`native/build/Release/model_pose_probe.exe <model.mdl>` 现在检查骨骼 parent 索引、骨骼位置、attachment 的 bone 引用、原点和三个基向量是否为有限数值，并报告 bodypart 数量、配套资源完整性、GPU 蒙皮和 ViewModel 状态。探针不会把 bind-pose 资源完整误报成已蒙皮模型。

本机实际样本 `tf/download/models/arti/player/demo.mdl` 的读数为：`mdlReadable=true`、`bones=76`、`attachments=0`、`bodyParts=25`、`invalidParents=0`、`companionSetComplete=true`、`gpuSkinning=missing`、`viewModel=unknown`。该样本没有 attachment 记录，因此不能据此宣称挂点可用。

`model_pose_probe.exe --self-test` 已验证两个损坏输入判据：`attachmentOutOfBoundsRejected=true`、`bodyPartOutOfBoundsRejected=true`。测试使用临时文件，结束后删除，不修改 TF2 安装目录。

主窗口资源标题现在显示 `posePreflight`、`bones`、`attachments`、`bodyParts`。`posePreflight=ready` 仅表示 MDL/VVD、骨骼表和 bind-pose 索引满足前置检查；动画求值、GPU skinning 和 ViewModel 仍分别保持 `Missing/Unknown`。

## 当前复核

- Release 构建：`cmake --build native/build --config Release --parallel 1`，通过。
- 损坏边界：`model_pose_probe.exe --self-test` 输出 `attachmentOutOfBoundsRejected=true`、`bodyPartOutOfBoundsRejected=true`。
- 真实样本：`demo.mdl` 输出 `bones=76`、`attachments=0`、`bodyParts=25`、`invalidParents=0`、`companionSetComplete=true`、`gpuSkinning=missing`、`viewModel=unknown`。
- 探针与窗口语义：真实样本 `posePreflight=true`；损坏自测 `corruptPosePreflight=false`。
- `git diff --check`：通过。
- 原生 renderer 新增 `ModelGpuStatus`：`NotLoaded`、`BindPoseOnly`、`SkinningMissing`、`ViewModelUnknown`。`uploadBindPoseModel` 成功只报告 `BindPoseOnly`，不会把 bind pose 误报为 GPU skinning 或 ViewModel 完成。
- 原生 renderer 新增独立 `clampViewModelFov` / `setViewModelFov` / `viewModelFov` 接口；`viewmodel_fov_probe` 验证 0→40、180→120、80 保持不变。当前 `viewModelRender=unknown`，因为没有 ViewModel 实体/网格绘制链。
- `ModelDrawVertex` 现在无损携带 VVD 的 3 个权重、3 个骨骼索引和 `boneCount`，renderer 上传结构也保留这些字段；真实样本 `skinningVertexCount=9318`、`invalidSkinVertexCount=0`，但当前 VTX 索引未产出 bind-pose draw vertices（`bindPoseSkinningAttributes=0`），因此 GPU skinning 仍为 `missing`，保留 bind-pose fallback。
- 当前 `ModelBone` 仅有 parent/position，没有 `poseToBone` 3x4 bind matrix；probe 输出 `bindPoseMatricesAvailable=false` 并记录阻断原因。因此不能安全填充 128-bone constant buffer，GPU skinning 保持 `missing`。
- 本轮按已有格式证据读取骨骼记录 `+96` 的 12 个 `poseToBone` float；真实 `demo.mdl` 输出 `mdlVersion=48`、`boneStride=216`、`poseToBoneOffset=96`、`invalidPoseToBone=0`、`bindPoseMatricesAvailable=true`。这只证明 bind matrix 字段边界可靠；动画序列矩阵仍未解码，GPU skinning 继续保持 `missing`。
- 损坏变异现在覆盖 attachment、bodypart、bone 三张表：三项 `OutOfBoundsRejected=true`，`corruptPosePreflight=false`。
- ViewModel 真实样本 `tf/download/models/player/sm_mia_the_fem_spy/v_watch_spy.mdl`：`viewModelPathDetected=true`、`mdlReadable=true`、`bones=26`、`bodyParts=3`、`invalidPoseToBone=0`、`companionSetComplete=true`、`posePreflight=false`、`gpuSkinning=missing`。普通 `demo.mdl` 的 `viewModelPathDetected=false`、`posePreflight=true`。

## 资源事实

- 资源解析只接受实际存在的 loose 文件或已索引 VPK 条目；不会把预期路径当作已存在资源。
- `resolveAssetReference` 没有模型路径而只有模型索引时返回 `Unknown`，诊断为 index-to-path mapping unknown。
- `ModelInspection::renderableResourceSet` 只表示 MDL + VVD + 至少一个 VTX 结构完整，不表示已能渲染人物、饰品或 ViewModel。
- 本仓库没有把 TF2 安装目录中的资源复制进版本库；具体模型路径必须由运行时 `AssetRoot` 解析。缺少本机资源时应报告 `Missing`，不能推断为可用。

## 下一步所需的可验证增量

1. 从 Demo 实体字段解析 wearable/item 与 owner 关系，并将 item model path 送入 `resolveAssetReference`。
2. 将已读取的 MDL bodypart 数值与 Demo 实体的运行时选择值关联，覆盖变体资源检查。
3. 实现骨骼姿态矩阵和 attachment transform，先用 `ModelAttachment::bone` 做数值探针。
4. 接入 `v_*.mdl` 的资源选择、独立 ViewModel 相机/FOV 和 GPU 绘制后，再宣称第一人称 ViewModel 支持。

## 构建证据

本文件只记录缺口，不改变模型运行逻辑。提交前使用以下命令验证当前原生 Release 目标：

```powershell
cmake --preset windows-release -S native
cmake --build native/build --config Release --parallel 2
git diff --check
```

