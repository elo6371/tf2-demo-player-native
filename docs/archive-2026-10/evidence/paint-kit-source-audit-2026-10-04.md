# Paint Kit 来源三层审计（2026-10-04）

本审计只读取本机 TF2 安装目录，不把 Source SDK 2013 或第三方代码当作
TF2 实现，也不从文件名推测 PaintKit 映射。

## 1. Schema 层

目录：`D:\SteamLibrary\steamapps\common\Team Fortress 2\tf`

| 查询 | 结果 |
| --- | ---: |
| `scripts/protodefs/proto_defs.vpd` | 存在，9,830,027 字节；二进制字段尚未解码 |
| `paintkits_master.txt` | 0 |
| `*.manifest` | 0 |
| `*schema*.txt` / `*schema*.json` | 0 |
| `items_game.txt` 顶层数字 `paint_kits` | 0 |

`items_game.txt` 中仍有 `paintkitweapon` 品质、Paintkit case/collection 和
相关属性文本，但这些内容没有提供 `PaintKit ID + ItemDefIndex ->
material override` 记录。

本机 `scripts/protodefs/proto_defs.vpd` 确实存在（9,830,027 字节）。对原始
二进制做字符串核验得到：`Paintkit 390=1`、`Warpaint=12`、
`patterns/paint_=5280`。这些命中只能证明 VPD 包含相关字符串；当前工程
尚未解码其 protobuf 字段，也尚未从中得到合法的
`PaintKit ID + ItemDefIndex -> MaterialOverride` 映射。

当前探针已增加边界安全的 protobuf wire reader：可读取 varint、fixed32、
fixed64 和 length-delimited 字段，保留数字字段路径与嵌套路径，并对截断
varint/长度字段执行自测。VPD 先按 12 字节头解析，已确认 little-endian
头值为 `0,111,95`；首段候选从偏移 12 开始、长度取 `header1=111`，且偏移
12 的首字段为 `0x0a`、长度 28。探针只把这 111 字节首段交给 wire reader，
不再把整文件直接当作 protobuf。首段当前仍报告 `wire_ok=0` 和具体错误，
因此不把部分字段误报为完整 PaintKit 定义，输出继续保持 `decoded=false`
与 `mapping=false`。

## 2. 武器关联层

当前 Demo 资产引用可以读取 `itemDefIndex`、`paintKit` 和 `skin` 字段，
`ItemSchema` 可以读取物品 `visuals` 声明；本机没有额外 schema/manifest
把这几个身份字段关联到 PaintKit 定义或武器材质目标。因此关联状态保持
`Unknown`，不能从物品名称、模型路径或目录名补全。

现有 probe：

```powershell
cmake --build native/build --preset windows-release --config Release
native/build/Release/item_schema_probe.exe `
  'D:\SteamLibrary\steamapps\common\Team Fortress 2\tf\scripts\items\items_game.txt' 1151
```

关键结果：

```text
entries=11592
paint_kits=0
appearance ... manifest_status=3 replacement_status=3
```

其中 `3` 是 `Unavailable`；它表示没有 manifest，不表示替换成功或失败后
可以安全回退到某个猜测材质。

## 3. 实际 VMT/VTF 层

递归文件统计得到：

| 扩展名 | 数量 |
| --- | ---: |
| `.vmt` | 3123 |
| `.vtf` | 3027 |

这些文件证明本机存在材质和纹理资源，但没有 PaintKit ID、ItemDefIndex、
武器基材质三者的合法绑定。VMT/VTF 的存在不能单独升级为
`replacementStatus=Available`。

## 结论

`items_game` 物品/visuals 声明可读，VPD 文件存在且含相关字符串，但 VPD
字段尚未解析；PaintKit 定义、武器关联和材质替换 manifest 仍不可用。
播放器应继续报告结构化 `Unavailable`/`Unknown` 状态，不应用材质替换。
