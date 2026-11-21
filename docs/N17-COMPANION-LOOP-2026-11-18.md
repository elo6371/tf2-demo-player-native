# N17：companion 循环元数据修复与验证（2026-11-18）

## 结论与身份

已复现并修复一处实际采样错误：合并动画 companion 时，只更新了 animDesc/blendCount，保留了基模 stub 的非循环标记；跨过一个周期后，应该循环的序列被钳在末帧。未改变默认动画开关、运动时钟、活动映射或多 blend 实现。

- 工作树：`t105-integrate` / `work/integrate`，改动前 HEAD `3e3f47f32e204491279d7a6ca0977ba1c89de396`，开工 clean。
- 修后产品 SHA-256：`8b1ab1f0d3773d52bbb6148c6a9e1b66dce741c621ef19cae353d43507677641`。
- 修后 model_pose_probe SHA-256：`59d38b95ae2a2d5ce8d8d1a16c6dc52f2e80775c04623b05f7274eb9bf6f1a23`。
- 以上是提交前构建、有限验证身份，不宣称发布 canonical；本轮未抓新的产品外观帧。

## 源码依据（实际从 GitHub 固定 revision 读取）

- [Valve SDK studio.cpp / CStudioHdr::pSeqdesc](https://github.com/ValveSoftware/source-sdk-2013/blob/e1f1fd42271805072f052051c26c21caa2c87b04/src/public/studio.cpp#L932)：virtual sequence 返回所属 group 的原始 seqdesc，包含其循环语义。
- [Valve SDK studio.h / STUDIO_LOOPING](https://github.com/ValveSoftware/source-sdk-2013/blob/e1f1fd42271805072f052051c26c21caa2c87b04/src/public/studio.h#L3078)：`0x0001`。
- [OthmanAba studio.cpp / CStudioHdr::pSeqdesc](https://github.com/OthmanAba/TeamFortress2/blob/1b81dded673d49adebf4d0958e52236ecc28a956/tf2_src/public/studio.cpp#L918)：同样返回 included group 的描述符。此镜像用于交叉核查，不视为独立运行真值。
- 本项目 `sampleAnimation` 按 `sequence.looping` 在 wrap / clamp 之间选择；缺失标记实际改变取帧，而不只是显示计数。

## 改了什么

| 文件 | 改动 | 原因 |
|---|---|---|
| `native/src/animation_binding.cpp` | 同名有效 companion 绑定时同步 looping 与 flags 的 bit 0 | 采样必须使用帧来源的循环语义；保留基模序号、activity 与其余 flag，不盲目替换所有元数据 |
| `native/tools/model_pose_probe.cpp` | 增加 `--player-animation-test --companion-loop-test` | 对照 companion 本身的序列表，覆盖全体可绑定项；直接从源描述符取预期并检测跨周期取帧，加入循环/非循环两种反向变异 |
| `class-pose-family-check.sh` | 每职业追加该检查，非零 rc 或缺 PASS 都失败 | 防止只在一个周期内采样而漏掉 clamp 缺陷；继续使用 scratch 输出 |

测试用独立 cache 读取 companion 本身，避免第二次 resolve 导致原 cache 的 vector 引用失效。循环标记比较覆盖全体绑定项；跨周期帧测试与反向变异各覆盖每职业一个可解码的循环与非循环序列，不宣称遍历所有帧。

## 修前复现（rc=1）

```text
companion-loop sample label=ragdollspawn looping=0 tick=34 expected=30 actual=30 mutated=3 killed=1
companion-loop sample label=layer_reload_standing_arms_primary_loop looping=1 tick=23 expected=3 actual=19 mutated=19 killed=1
COMPANION-LOOP=FAIL compared=303 mismatches=75 loopSample=1 clampSample=1 loopMutation=1 clampMutation=1
```

Scout 共 75 条绑定序列的循环标记不符；其中上述循环 clip 在第 23 tick 应取第 3 帧，实际取第 19 帧。负控翻转标记后必须改变取帧，不接受“只有元数据断言变红”。

## 修后结果

| 职业 | 比对序列数 | mismatch | 循环与非循环采样 | 两类变异 |
|---|---:|---:|---|---|
| scout | 303 | 0 | PASS | KILLED / KILLED |
| soldier | 353 | 0 | PASS | KILLED / KILLED |
| pyro | 266 | 0 | PASS | KILLED / KILLED |
| demo | 273 | 0 | PASS | KILLED / KILLED |
| heavy | 255 | 0 | PASS | KILLED / KILLED |
| engineer | 348 | 0 | PASS | KILLED / KILLED |
| medic | 210 | 0 | PASS | KILLED / KILLED |
| sniper | 237 | 0 | PASS | KILLED / KILLED |
| spy | 271 | 0 | PASS | KILLED / KILLED |
| 合计 | 2516 | 0 | 18 个样本 | 18 个变异检出 |

定向构建 `model_pose_probe tf2_demo_native` rc=0；matcher、helper-skin、entity-id 自测通过。扩展后的 pose-family 门禁：

```text
POSE-FAMILIES classes=9 crouchClasses=9 flinchSequences=14 sampled=40 decoded=40 failed=0
POSE-FAMILIES=PASS every sampled crouch/flinch sequence decodes and at least one crouch clip moves
```

构建与最终回归经 hostlock；最终回归 rc=0，锁已释放；`evidence/` 与 `native/tests/fixtures` 未改动。首次包装命令的嵌套引号错误导致 `bash: -c: option requires an argument`，该次不是门禁结果；改用独立 scratch 脚本后重新取到上述完整 verdict，未改产品逻辑绕过问题。

## 复现命令

在仓库 Git Bash 中执行；TF 资产路径按本机配置，输出不得指向 tracked evidence。

```bash
bash tools/lockrun.sh n17-loop-build -- bash build-target.sh model_pose_probe tf2_demo_native
native/build-nmake/model_pose_probe.exe --player-animation-test \
  --tf-root 'D:/SteamLibrary/steamapps/common/Team Fortress 2/tf' \
  --model models/player/scout.mdl --companion-loop-test
T62_POSEFAMILY_OUT=.scratch/n17-loop/families bash class-pose-family-check.sh
```

原始日志保留在本机 `.scratch/n18-loop-audit/`，不新增大体积签入证据；完整复跑可从上述命令生成。`before.log` SHA-256 `13d9fd0e843814ca4b2fa1e89ed76411cb50ad706d2590fab579934764c7b9bf`；最终 `regression.log` SHA-256 `2c09945b4ec794c5f79c3136dfe772c6d92cedb6be0dc03dca25693f4bd8a196`。

## 没证明什么与接续

- 未证明 Scout 外观等于 TF2；未取得新同机位参考，未验证 GPU 像素、LOD/bodygroup/UV 或武器外观。
- 未实现 sequence 内 9/12-way blend、完整 pose parameters、循环接缝插值或引擎活动状态机；对照双方仍共享当前单 clip 解码器，不能发现双方共有的解码错误。
- 默认 bind pose、实验时钟/自定义姿态组合仍按 N17 原任务待收口；不自动启用未验收的动画分支。
- 本轮没有合并 `work/n17-anim` 的未提交实现，也没有把其中 `[[STATES]]` / `[[PIXELS]]` 占位结果当 PASS。
- 当前产品的地图/UI/音频整门与 verify-all 未复跑；历史 exe 的 PASS 不迁移。

下一步按[11-21 交接与计划（单一入口）](HANDOVER-2026-11-21.md)：接回现有动画分支已验证部分，验证循环边界、固定帧负控、真实姿态参数，再决定默认动画策略。
