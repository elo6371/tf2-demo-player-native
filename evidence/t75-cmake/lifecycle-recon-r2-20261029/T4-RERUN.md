# T75 安装生命周期门禁 —— 候选树重跑（T4）

- task: T4（候选树重跑 T75 install lifecycle forward + M4 mutation）
- status: PASS
- branch/worktree: candidate/p0-final @ D:/TF2_Native_Worktrees/candidate-final
- head: b3af4b6a3bd3a3f3bad0a867908a11a1e42ef02a（重跑期间未变）
- git status: 重跑前干净；重跑后只有本 run-id 目录未跟踪（源码已恢复，见下）
- files: 本目录（runner.log、build.log、forward-gate.log、mutation-gate.log、cases/、fixture-inputs.sha256、package-hashes.txt、T4-RERUN.md）
- commands:
  - bash build-target.sh tf2_demo_native native_install_smoke_probe install_cli_probe vpk_query（候选树无 build-nmake，首次自动 configure；NMake Makefiles + Release）
  - bash install-lifecycle-check.sh --forward evidence/t75-cmake/lifecycle-recon-r2-20261029
  - bash install-lifecycle-check.sh --mutation <同一 out-dir>
- tests: forward 15 cases / 214 checks；mutation 4 cases / 34 checks / 4 mutations
- rc: BUILD-RC=0、FORWARD-RC=0、MUTATION-RC=0
- evidence:
  - forward-gate.log 末行：`INSTALL-LIFECYCLE=PASS`（cases=15 checks=214 mutations=0）
  - mutation-gate.log 末行：`INSTALL-LIFECYCLE-MUTATION=PASS (4 mutations caught, sources restored)`（cases=4 checks=34 mutations=4）
  - 时间窗：2026-10-29 17:06:14（构建完成）→ 17:10:39（mutation 结束）
  - 源码 sha256（重跑前 = 重跑后，逐字节一致）：
    - native/src/main.cpp = 57cc0ac3b555bcb04c7432f30ba7d4a88024ac83e23dc4cdafdd9a11fb59d126
    - native/src/install_lifecycle.cpp = 36dba4b7f52ff51a211e3091edce77153fae9790622cae95bccc4564c5ba082b
    - native/src/install_manifest.cpp = e53da57e4e38a570e3237aa4a5fa25f8b70ffe38e62aaa32fdfe485fe7149efb
  - 产物：native/build-nmake/tf2_demo_native.exe，2,657,280 字节，sha256=427dd705890f6ed9df4bdaa79349d091bb2f6cdc3cf46bb295a8aaad5cdad594（forward 阶段记录的 reference exe sha256=4ad1c0835660a9f8b4039cbd377268f8a1545a9965e05f7a90fa6bfca68589a5；M4 restore 重建后二进制重新链接，故最终哈希不同，两者都记录在案；二进制哈希不能单独作为来源证明，源码哈希 + 门禁日志才是）
  - 原始大产物（stage/ 6.0M、clean/ 26M、mutation/ 22M、fixtures/ 32M，共 86M）未入库，保留在 D:/TF2_Native_Worktrees/_shared/t4-run/artifacts-r2/；入库的只有日志、断言、哈希与用例目录（约 0.4M）
- failures: 无。两次先期尝试失败已排除且原因只与新建工作树的环境有关（不是代码回归）：
  1) 首次直接跑 forward：候选树无 build-nmake，cmake --install 无产物 → 171 项断言红；
  2) 第二次只构建了 tf2_demo_native/install_cli_probe/vpk_query：package 规则还要 native_install_smoke_probe，cmake --install rc=1、payload=0；
  3) 第三次补齐 native_install_smoke_probe 后全绿。失败日志保留在 _shared/t4-run/attempt2-failed-missing-smoke-probe/。
- restore: 源码哈希前后一致；git status 除本 run-id 目录外无改动；无残留 mutation
- lock: host lock owner=t4-t75-gate，2026-10-29 17:00:57 获取，门禁结束后释放（product_processes=0）
- processes: 门禁结束后扫描，产品进程 0
- commit/merge/push: 本目录随后提交到 candidate/p0-final；未 push
- next: T5 冻结审计引用本证据；T6 最终全链仍在等待 T2 删除范围决定
- decision_needed: 无（候选树源码/HEAD 未因本次重跑改变）
