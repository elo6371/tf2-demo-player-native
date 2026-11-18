# G1/G2 可复现入口

这些是本轮（2026-11-14）用过的运行脚本。原先只存在于 `.scratch/`（不入库），
按 N03 的要求移到这里，让命令可复建、可审阅。

| 脚本 | 作用 | 需要 host lock |
|---|---|---|
| `minimap-five/run.sh` | 七图小地图：每图开关/对照两次抓帧（`MAPS=` 限定，`EXTRA=` 追加参数如 `--window-size 800x600`） | 是（14 次产品运行） |
| `minimap-five/check.py` | 七图判定：面板矩形、控制点环、朝向、`no-camera-pose` 回归 | 否（纯离线读帧） |
| `minimap-five/paused-panel.sh` | 暂停缺陷正/负控（`EXE=` 指定二进制） | 是（2 次运行） |
| `g2-probe-matrix.sh` | 七图几何/材质探针矩阵 | 建议（14 次探针，纯读） |
| `g2-world-frames.sh` | 七图世界帧（STV 相机、tick 30000、8x 播放） | 是（7 次运行） |
| `../lockrun.sh` | 取 host lock 后运行一条命令（`tools/lockrun.sh <owner> -- <cmd>`） | — |

约定：

- 所有脚本给 `--settings-file` 传**副本**，不传 tracked 的 `native/tests/fixtures/gate-settings.ini`（D40）。
- `lockrun.sh` 在命令结束后校验 `native/tests/fixtures/` 未被写脏，脏了直接报错（N03）。
- 输出目录用 `OUT=` 指定，默认写到 `D:/_shared/EXEC-2026-11-14/...`；仓库内不留帧。
