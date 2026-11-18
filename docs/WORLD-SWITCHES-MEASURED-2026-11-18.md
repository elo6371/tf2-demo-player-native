# 世界渲染的四个开关：实测（2026-11-18）

这份记录回答一个很具体的问题：世界渲染里那些"看着像没实现"的项，到底是**代码里没有**，
还是**开关没开**，还是**素材不在**。做法是同一机位、同一 tick 跑六次抓帧，只改开关，
比 trace 行和像素。

机位与输入：`--debug-camera 500 -2240 240 17.8 127.97 --capture-tick 1`，demo 为
`E:/demo测试/pl_upward_f12/STV/a9630c65...pl_upward_f12.dem`，帧 1264×681。
六次 rc 全 0。

| 跑 | 开关 | BMP sha256 | 关键 trace | 像素差 |
| --- | --- | --- | --- | --- |
| A | 基线 | `caef7fa60b02…` | `fog enabled=0 … source=off reason=default off` | 基准 |
| B | `--world-fog` | `dbadd2a0c252…` | `fog enabled=1 start=100.00 end=6000.00 maxdensity=0.850 … verticesFogged=200121 featureMaskFogBit=32 source=bsp passes=world-brush-only` | **173706 px / 20.18%**，maxdiff 134，bbox 纵跨 y≤421 |
| C | `--world-detail on` | `caef7fa60b02…` | `detailSwitch=1 detailDeclared=20 detailResolved=0 detailNoVmt=20`、`layer=detail … status=refused reason=the $detail VMT is absent from this install` | **0** |
| D | `--world-bump2 on` | `caef7fa60b02…` | `bump2Switch=0`（按位与门控，需同时开 material-bump） | **0** |
| E | `--world-material-bump on --world-bump2 on` | `caef7fa60b02…` | `bump enabled=1 tiles=10 materials=10 vertices=6702`；`bump2Switch=1 bump2Resolved=0 bump2SsBump=1`；`layer=bump2 … refused reason=$ssbump: layer 2's texel is a bumpBasis weight, not a tangent-space normal` | **0** |
| F | `--world-geometry sky` | `b17a7c74e1ec…` | `world sky state=active vertices=36 triangles=12 faces=6 draws=1 side=512 farplane=1 shader=dedicated` | 860511 / 99.97%（A↔F 相同像素仅 273） |

## 结论

1. **`$detail` 不是"没有消费者"，是素材不在。** 开关一路传到下游
   （`detailSwitch=1`、`detail enabled=1`），地图引用的 20 个 `$detail` VMT 在本机
   全部缺失（`detailNoVmt=20`）所以 `tiles=0`、画面与基线逐字节相同。要用它得先补
   detail 素材；代码路径不该被记成"只计数不画"。
2. **`$bumpmap2`（`$ssbump`）的拒绝理由成立。** 两个开关都开时唯一一个 bump2 材质
   仍被按名字拒绝：layer 2 的 texel 是 bumpBasis 权重、不是切线空间法线，本 pass 只有
   一张 lightmap。这是能力边界，不是开关没开。
3. **雾是真的、但覆盖不全。** `--world-fog` 让 20.18% 的像素变化（远景整片），可
   trace 自己写着 `passes=world-brush-only`：实体模型、武器、天空盒都不吃雾。默认开启
   会得到"远景有雾、前景道具和天空没雾"的不一致，所以**默认关是对的，先补覆盖再谈默认**。
4. **天空盒：cube 已 active（6 面、12 三角、1 draw），但这个机位几乎看不到天空**
   （A↔F 相同像素仅 273 px），所以本图无法量化 3D 天空盒（sky_camera）的缺口——要量化
   得换一个朝天、朝场外的机位。也就是说：**"3D 天空盒缺失"在这张图上是不可见的**，
   不能拿这张图当证据。

## 顺带纠正两条文档口径

- `native/` 下 `sky_camera` 零命中、也没有 `UNSUPPORTED` 字样的实现标记；"UNSUPPORTED"
  只存在于早期文档。代码里表达"不做"的方式是具名拒绝字符串 / 零命中 / 静默降级分支，
  读文档时应按 trace 行判断而不是按文档标签。
- 世界雾的默认关不是缺陷：`worldFogGiven=false`（`native/src/main.cpp:3418`）是产品缺省，
  开关打开即生效（本表 B）。

## 复现

产物在 `.scratch/world-probe/`（A–F 的 bmp/trace/log 与 REPORT.md），不签入。
注意 trace 含 NUL 字节，文本工具要先按二进制处理（`grep -a`）。
