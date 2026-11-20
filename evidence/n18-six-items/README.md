# N18 六项验收证据（2026-11-20）

本目录是 `docs/N18-SIX-ITEM-MATRIX-2026-11-20.md` 的证据。所有文本都是从 `.scratch/n18/*.trace` / `*.bmp` 里抽出来的，**大 trace 不入库**（每份 18–33 MB），要复核逐帧行为就用矩阵文档 §6 的命令重跑。

| 文件 | 内容 |
|------|------|
| `readings.txt` | 11 跑的 per-run 读数（grip / body / bodygpu / companion / path / vmanim / pass / spectate / filter / pma / 两张直方图），逐字来自 `.scratch/n18-batch.log` 的 `== readings` 段 |
| `diffs.txt` | 8 组像素差（`bmp-diff-bbox.py` 输出）：差异像素数 + bbox |
| `bmp-md5.txt` | 12 张抓帧的 md5（11 跑 + body-default 控制跑） |
| `raw-weaponBodyReading.txt` | body-machete / body-rifle-left / body-rifle-anim 的 `gpu: viewmodel weaponBodyReading` 末行（逐字） |
| `raw-switch-lines.txt` | sw-before / sw-after 的 clock 与 eye 行、tick 2634 的切换行、body-rifle 的三条 cameraFilter 行（逐字） |
| `raw-fallback-lines.txt` | body-rifle-off 与 missing-body 的 weaponBodyReading / scene: weaponBody 末行（逐字） |
| `crop-weapon-*.png` | 同一区域（x=540 y=220 w=480 h=440）的四种武器形态：步枪 / body off / 左手 / 砍刀 |
| `crop-zoom-rifle*.png` | 步枪区 3× 放大（x=590 y=260 w=130 h=250），用来确认「直立的长枪就是武器体」 |
| `crop-anim-vs-seq29.png` | 基线姿态 vs 序列 29 左右并排（x=700 y=340 w=310 h=300） |

读法：单跑读数先看 `scene: weaponBody`（实体/类/worldModelIndex/path）确认「画的是哪个实体」，再看 `gpu: viewmodel weaponBodyReading`（材料/顶点/半径/clip）确认「画出来的是什么形状」，最后看 `gpu: viewmodel pass order`（drawCalls/skinning/boneTableCoverage）确认「怎么画的」。像素差只回答「哪一块变了」，不回答「谁画的」。
