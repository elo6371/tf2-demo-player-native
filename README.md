# TF2_Native_Test

P0（SourceTV PacketEntities 状态重建）的隔离测试树。

- 源目录 `D:\TF2_Demo_Player` **本次未改动**（`work/native-mvp-source` 仍是
  `d585af8`，`git status` 干净）。
- 本目录由 `git archive d585af8 native | tar -x` 建立，独立 git 仓库，
  分支 `p0-entity-protocol`。

## 入口

| 文件 | 用途 |
|---|---|
| `ACCEPTANCE-P0-2026-10-06.md` | **主验收文档**，按清单的强制验收清单逐项填写 |
| `HANDOFF-P0-2026-10-06.md` | 可直接并入主线的交接条目 |
| `fix-P0-entity-messages.patch` | 只含 `native/` 的协议修复补丁 |
| `verify-all.sh` | 一条命令跑完整证据链（构建 → 普查 → fixture → oracle → 变异） |

## 命令

```bash
cd /d/TF2_Native_Test

bash build-cmake.sh          # 干净全量 Release 构建（NMake，21 个 exe）
bash run-demos.sh evidence/final          # 9 份 demo 协议普查
bash check-oracle.sh "<oracle.exe>" evidence/final   # 与 Rust oracle 逐值对照
bash mutate.sh               # 变异验证（证明判据能变红）
bash verify-all.sh           # 上面全部串起来
```

## 探针

| exe | 说明 |
|---|---|
| `native/build-nmake/entity_protocol_probe.exe` | 协议普查：包数、消息类型直方图、实体失败坐标、实例基线 |
| `native/build-nmake/entity_message_fixture_probe.exe` | 58 个 bit-exact 合成 wire fixture，覆盖 baseline/delta/Preserve/Leave/Delete |
| `native/build-nmake/tf2_demo_native.exe` | 主程序 |

## 注意

- 会出声的测试**必须**带 `--audio-device 6`（`耳机 (xduoo audio)`）。
  不传会落到系统默认设备（`WAVE_MAPPER`）。
- 本机 `-G "Visual Studio 17 2022"` 生成器不可用（`vcvars64.bat` 要调被沙箱
  拦掉的 `reg.exe`）。`build-cmake.sh` 顶部注释写了 NMake 走通的两个前置条件。
- 本机 exe 哈希**不可复现**（两次强制重链接差 2 字节）。
  复核锚点用源码哈希 + 编译命令，见验收文档 §8。
- `evidence/final/oracle-*.txt` 约 94 MB，已 gitignore，可随时用
  `check-oracle.sh` 重新生成。
