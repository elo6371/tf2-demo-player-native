# `--anim-props` 是纯加法（旧二进制 vs 新二进制，逐字节）

## 问题

`entity_model_probe` 新增了 `--anim-props` 模式。要证明的是：**默认路径（不传这个 flag）
的输出一个字节都没动**。这是本项目对「探针输出变化」的一贯要求
（见 `check-probe-output-additive.sh` 对 `entity_protocol_probe` 的同类要求）。

## 证法：旧提交的对照树 + 新旧二进制跑同一份输入

比「把新行 strip 掉再比」直接，因为它不需要猜哪些行是新的：

1. `git worktree add --detach .scratch/obs/oldtree 6d3bfba^` —— `6d3bfba` 是加 `--anim-props`
   的那次提交，`^` 就是它之前（该文件里 `grep -c "anim-props"` = 0，已核对）。
2. 在该树里单独 `cmake` + build **只有** `entity_model_probe` 一个目标（约 2 分钟）。
3. 新旧两个二进制跑同一份 POV demo（`autorecord_2026-07-02_13-26-46.dem`），
   **stdout 与 stderr 分别**落盘比对。
4. `git worktree remove --force`。

## 读数

| 产物 | sha256 |
|---|---|
| `pov-default-old.txt`（`6d3bfba^` 的二进制） | `3685bde62a109a23fb02ba04ca57fc394c7b62d9a4f32048d3e392eb0cde0735` |
| `pov-default-new.txt`（当前二进制） | `3685bde62a109a23fb02ba04ca57fc394c7b62d9a4f32048d3e392eb0cde0735` |

**逐字节相同**（1073 字节，JSON 计数器块）。

## 这条读数的强度，以及它**没有**证明什么

- ✅ **stdout 相同**：1073 字节、sha256 相同 —— 这是有意义的一半。
- ⚠️ **stderr 那一半是弱读数**：默认运行**两边都是 0 字节**（探针默认不往 stderr 写），
  所以「stderr 相同」只是「两边都空」。它能排除的是「新 flag 的代码路径无条件往 stderr
  写东西」，**不能**排除「新 flag 被传入时 stderr 有额外输出」——那个由
  `animation-availability-check.sh` 自己的断言覆盖（它读的就是 stderr 行）。
- ⚠️ **只验了一份 demo、一个 flag**。要更强，应把 `--dump-class-props` 等既有 flag 也跑一遍
  新旧对照。**本机未做**，记为可加强项。
