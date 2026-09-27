# src1.6.2

基于 `src1.6.1` 的独立版本，修复确定性评分与官方 grader 的语义差异，并补齐 guarded 任务组的实际执行记录和失败退出。默认仍使用原策略；设置 `RDFW_TASK_GROUP_MODE=guarded` 才启用原 Stage 1 任务组范围。没有概率或学习策略。

- [审计与修复说明](docs/AUDIT.md)
- [测试结果与剩余风险](docs/RELEASE_1.6.2.md)
- 官方配对入口：`tests/run_score_validation.py`

在 Linux/WSL 中构建本地测试：

```sh
mkdir -p /tmp/rdfw-162-tests
cd /tmp/rdfw-162-tests
cmake /path/to/src1.6.2/tests -DCMAKE_BUILD_TYPE=Debug -DOFFICIAL_SDK=/home/yifan/env-release-2026
cmake --build . -- -j4
ctest --output-on-failure
```

省略 `OFFICIAL_SDK` 可只运行 9 组本地测试；提供 SDK 增加真实官方评分实现的 16 个语义案例测试。SDK 运行文件复制到独立构建目录，不修改已安装 SDK。

官方配对使用同一固定外部种子、同一 group-mode 和同一未修改题目。默认覆盖 decision/combinatorial、30 道 tradeoff challenge，以及 6 道代表性历史题的 IT/NT；`--quick` 只测前两组。`--group-mode off` 可核对默认路径，`--reuse-builds` 可复用先前构建。
