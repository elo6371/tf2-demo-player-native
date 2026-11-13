# Paint Kit 来源状态（2026-10-04）

## 结论

当前本机 TF2 安装没有可供播放器直接索引的顶层 `paint_kits` 表：
`scripts/items/items_game.txt` 的解析结果为 `paint_kits=0`。文件中仍有
`paintkitweapon` 品质、paintkit 掉落集合和相关属性文本，但它们不是
`paint kit id -> 材质/VMT/VTF` 的替换清单。

播放器当前只能报告这些声明和物品 `visuals` 叶字段，不能据此声称已经
替换模型材质。

## 本机资源盘点

- `tf/scripts/items/items_game.txt`：存在 `paintkit`/`warpaint` 相关字符串，
  不存在顶层数字 `paint_kits` 节点。
- `tf/scripts/protodefs/proto_defs.vpd`：文件存在，大小 9,830,027 字节；原始
  二进制可见 `Paintkit 390`、`Warpaint` 和 `patterns/paint_` 字符串，但字段
  尚未解码，不能当作映射表。
- `tf/download/materials`：存在下载的 VMT/VTF 文件，但没有发现把 paint kit
  编号、物品 defindex 与目标材质绑定的 manifest/schema。
- `tf/custom`：发现 HUD 和用户自定义资源；它们没有可验证的 paint-kit
  替换索引，不能作为播放器的物品外观数据源。
- 仓库：没有 paint-kit manifest、override 表或可再分发的第三方材质映射。

## `custom` / `download` 清点（2026-10-04）

清点范围只包含文本资源（`.txt`、`.vmt`、`.res`、`.cfg`、`.json`、`.kv`），
并排除 VPK、缓存和二进制文件：

| 目录 | 文件总数 | 文本文件 | paintkit 命中 | warpaint 命中 | material_override 命中 |
| --- | ---: | ---: | ---: | ---: | ---: |
| `tf/custom` | 1373 | 908 | 20 | 0 | 0 |
| `tf/download` | 9846 | 2740 | 0 | 0 | 0 |

`tf/custom` 的 20 个 `paintkit` 命中全部来自 HUD 的两个 `.res` 文件：
`charinfoloadoutsubpanel.res` 中是 `paintkit_preview` 按钮，
`clientscheme.res` 中是 PaintkitWeapon 品质颜色；它们没有 paint kit ID、
物品 defindex 或 VMT/VTF 目标路径，因此不是替换 manifest。

`tf/download` 的文本资源没有任何上述关联字段；其中存在的 VMT/VTF 文件
只能证明下载材质存在，不能证明它们属于某个 paint kit。

## 可验证 probe

```powershell
cmake --build native/build --preset windows-release --config Release
native/build/Release/item_schema_probe.exe `
  'D:\SteamLibrary\steamapps\common\Team Fortress 2\tf\scripts\items\items_game.txt' 1151 1
```

关键输出：

```text
entries=11592
paint_kits=0
paint_kit=1 status=missing
```

同一 probe 对 defindex `1151` 能读取物品 `visuals`，例如
`attached_models_festive.0.model`；这只是物品声明，不是材质替换结果。

## 后续接入条件

只有在获得来源明确、可再分发且包含 paint kit ID 到材质目标的 manifest，或
从 Demo/运行时实体得到可验证的材质覆盖字段后，才可以增加替换解析。当前
状态应继续显示为 `Unknown`/`Unavailable`，不得从文件名或素材目录结构猜测
paint kit 映射。
