# src1.6.4

独立基线为已完成的 src1.6.3。此版修复状态一致性，没有增加决策策略、任务或评分启发式；strict interval dominance、legacy priority fallback、完整投影准入和 5000 ms 平台期限不变。

- [开发报告](docs/RELEASE_1.6.4.md)
- [验证汇总](test-results/validation-20260927/summary.json)
- [修复前复现](test-results/validation-20260927/probe-ctest.log)
- [源码差异与哈希](test-results/validation-20260927/manifest.json)

本地与官方语义测试（WSL/Linux，SDK 只读，构建目录独立）：

```sh
mkdir -p /tmp/rdfw-164-unit
cd /tmp/rdfw-164-unit
cmake /path/to/src1.6.4/tests -DOFFICIAL_SDK=/home/yifan/env-release-2026
cmake --build . -- -j4
ctest --output-on-failure
```

包含原有十组本地测试、新增二十个状态用例及一组含十六个案例的官方评分语义测试，共 31 个 CTest；sanitizer 不链接未插桩的官方评分库，共 30 个。

官方配对：

```sh
python3 tests/run_interval_validation.py --output /tmp/rdfw-164-release-final-full --full --repeats 1 --diagnostics off
python3 tests/run_interval_validation.py --output /tmp/rdfw-164-release-final-target --cases 03 06 29 --repeats 3 --diagnostics off
python3 tests/run_interval_validation.py --output /tmp/rdfw-164-release-final-off --quick --group-mode off --repeats 1 --diagnostics off
python3 tests/package_state_validation.py
```

默认配对为 src1.6.3 / src1.6.4，外部种子 20260927，平台期限 5000 ms。`--binaries` 可复用已核对源码哈希的两个构建。`tests/baseline_probe` 直接编译未改动的 src1.6.3，只替换测试 stub，以复现新增测试；其中失败是预期的审计证据。

继承的 RELEASE_1.6.3、RELEASE_1.6.2、AUDIT 是历史基线记录；其旧验证目录位于原版本中。其他继承的历史测试工具仍可能固定使用旧版本，当前验收入口为上列命令。
