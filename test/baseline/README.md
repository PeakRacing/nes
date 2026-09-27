# corpus 基线

`corpus.csv` 是 `run_tests.ps1 corpus` 的“标准答案”：每个 ROM 一行，按 **sha256** 索引，
记录目录期望 mapper、ROM 头 mapper、实际生效 mapper、运行帧数、画面判定（verdict）与逐帧哈希链。

- ROM 本体不在版本库中（`.gitignore` 排除了 `/rom` 与 `*.nes`），基线里只有哈希与文件名，可以安全提交。
- 语料持续增长也不会破坏它：baseline 中找不到的 sha256 记为 `NEW`（只记录，不判失败）；
  基线里有、本机没有的 ROM 记为 `MISSING`（条目保留）。
- 重新生成：`./run_tests.ps1 corpus -UpdateBaseline`（合并写入，不会丢掉 MISSING 条目）。

判定规则见 `test/README.md`。
