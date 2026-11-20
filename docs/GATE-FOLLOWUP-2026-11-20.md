# 2026-11-20 门禁裁决与验证

| 裁决 | 实测 | 验证边界 |
|---|---|---|
| Capture point 钉到 signon 携带的 server tick `10320` | 第 3 段七帧全部命中，旧 signon entry tick `10252` 命中 0/7。预期来自 `demo-clock-check.sh` 的固定录制读数。 | 本次覆盖固定的 Snakewater 录制在播放 tick 0 的场景。 |
| 恒等蒙皮的不同 GPU 路径允许最多 1 LSB，差异颜色通道字节占比至多 `0.001%` | 前向恒等比较通过。1264×681×24 合成负控中，1 字节差 1 LSB 通过，1 字节差 2 LSB 被拒，26 字节分别差 1 LSB 也被拒。 | 该容差只用于单位骨矩阵与直通着色路径的比较。FOV 钳制、无效资源和短骨表回退继续逐字节比较。 |
| 文件模式 `sha256sum` 显式断开 stdin | 审计 #14 实列 9 个待修调用，汇总漏计 HUD。另核出 3 个调用（安装前置清单、职业抓帧、世界几何资产标识）。12 处改动覆盖 10 个脚本。11 个受影响脚本通过 `bash -n`。在 GNU coreutils 8.32 下用会消耗 stdin 的替身模拟 WinuxCmd，旧写法处理 1/3 行，新写法处理 3/3，第三行哈希错误可检出。 | 本轮校验了输入隔离和脚本语法。10 个哈希脚本的整门结果仍按各自运行窗口登记。 |

完整前向运行返回 0，7 段全部通过，`assertions_ok=154`，`VIEWMODEL-CONTRACT=PASS`。本轮执行合成负控，源码变异套件保留独立读数。锁已释放，运行输出在 scratch。

```bash
VIEWMODEL_CONTRACT_OUT=.scratch/vm-contract-20261120 \
VIEWMODEL_CONTRACT_TRACE_DIR=.scratch/vm-contract-20261120/traces \
bash tools/lockrun.sh vm-contract-20261120 -- bash viewmodel-contract-check.sh
```

- 基线 `630302e6` 加本轮门禁脚本改动。产品 SHA-256 `b694ef7ea8da3ceff1e7dfda60a0105e59aecd275a24fee7ec5238d634fe31a1`，探针 SHA-256 `b9ee0233b6a119276605c410eb167e2bf6137c468a8d3564c655594d9e735975`。
- 本机日志 `.scratch/vm-contract-20261120-wrapper.log`，SHA-256 `1bff122504824136e0b3145428e1e694d507f80d9d46b975dd4b9f566627ebe2`。
- 哈希修复脚本包括 `viewmodel-{companion,presentation,range,robustness}-check.sh`、`verify-audio-content.sh`、`verify-audio.sh`（三处）、`hud-check.sh`、`install-lifecycle-check.sh`、`class-pixels-check.sh`、`world-geometry-hole-capture.sh`。
- 项目总览已核正两项历史进度。N11 同一 exe 已采集 14/14 格。3D 天空盒 pass 已实装且默认关闭，已有单图四机位读数。人物外观、音频听感、雾和天空接缝由项目负责人现场验收。

取证原文见 [两条既有红](GATE-REDS-FORENSICS-2026-11-19.md) 和 [门禁前提审计 #14](GATE-PREMISE-AUDIT-2026-11-20.md)。
