# SimpleCN V1.4.3 空闲 CPU 修复

日期：2026-09-21。用户报告只打开 Qt 6 程序时，任务管理器 CPU 持续超过 8%。此前的报文模型基准不能覆盖空闲进程的负载，因此本次单独启动实际程序并做事件级对照。

## 复现与根因

使用 Qt 6.8.4 Release、独立空白配置、后台启动、不连接或发送硬件报文；硬件枚举保持正常运行。机器有 20 个逻辑处理器。预热 10 秒后采样 15 秒，实际程序消耗 13,765.625 ms 进程 CPU 时间，对应归一化总 CPU 平均 **4.588%**。这是独立配置下的测量，不替代用户原配置的观察值。

根因在 View 层的 `UiLanguageController`：过滤器处理 Paint/LayoutRequest 时，无条件调用 `QTabBar::setTabText()`，即使翻译结果与当前文字完全相同。标签栏设置再次使布局/绘制失效，触发下一轮过滤器处理，形成持续循环。`QSignalBlocker` 只能阻断信号，不能阻止布局和绘制事件。

在同一个未连接进程中，只改变语言过滤器路径，每阶段预热后测量约 5 秒：

| 修复前阶段 | 进程 CPU 时间/ms | GUI 线程 CPU 时间/ms | 绘制+布局事件 |
| --- | ---: | ---: | ---: |
| 正常启用 | 4546.875 | 4546.875 | 53,998 |
| 暂时移除语言过滤器 | 0 | 0 | 0 |
| 恢复过滤器并触发一次刷新 | 4390.625 | 4390.625 | 53,534 |
| 保留过滤器，仅排除标签栏翻译 | 0 | 0 | 0 |

这组对照将根因定位到标签栏的重复翻译写入；硬件枚举没有被停用。极小 CPU 时间受系统计时粒度影响，表中 0 不代表绝对无开销。

## 修复

仅当标签文字或提示的翻译结果确实改变时调用设置函数。保留语言过滤器、即时中英文切换、动态文字翻译、绘制逻辑和硬件轮询频率，未关闭任何功能或降低刷新频率来隐藏问题。

新增回归 `repeatedTabTranslationDoesNotInvalidateLayout`：重复翻译相同标签不应产生布局请求；同时验证中英文往返切换、提示翻译和动态修改文字。旧实现实际产生 1 次布局请求，回归失败；修复后通过。

## 修复后的测量

同一探针正常启用过滤器以及恢复过滤器后，GUI 线程 CPU 时间均降到计时粒度以下，采样期内无持续绘制/布局事件。后台枚举仍有少量进程 CPU 开销。

实际 `QtBootloader.exe` 使用与修复前相同的独立配置和启动方式，预热 10 秒后连续测量三个 10 秒窗口：

| 窗口 | 进程 CPU 时间/ms | 平均总 CPU | 工作集/MB |
| --- | ---: | ---: | ---: |
| 1 | 312.500 | 0.1562% | 140.45 |
| 2 | 265.625 | 0.1328% | 140.45 |
| 3 | 296.875 | 0.1484% | 140.43 |

计算方式为 `进程 CPU 时间增量 / 墙钟时间 / 逻辑处理器数 × 100%`。上述结果是本机空闲平均值，不是所有配置或实时任务下的占用承诺。

## 回归验证

Qt 5 / Qt 6 完整构建成功，两套 CTest 均为 16/16 通过。新增布局回归在两版 Qt 中均通过；`scripts/check_mvvm_boundaries.py` 和 `git diff --check` 通过。生产修改仅位于 View 层，不改变 MVVM 调用边界。

## 诊断工具

`idle_cpu_probe` 为手动构建的 Windows 工具，不加入 CTest，不部署到程序目录。它启动完整主窗口但不连接/发送报文，记录主线程和进程 CPU 时间，并统计绘制/布局事件。`--demo` 可绕开真实硬件枚举，`--english` 可检查英文界面。

```powershell
cmake --build --preset qt6 --target idle_cpu_probe
Start-Process C:/Documents/0_Qt/build/qttemp/Qt-GeneralController-qt6/tests/idle_cpu_probe.exe -ArgumentList C:/Documents/0_Qt/build/qttemp/Qt-GeneralController-qt6/idle-cpu/result.json -WindowStyle Hidden -Wait
```

本次原始记录位于 Qt 6 的 qttemp 构建目录下 `idle-cpu/`：`baseline-process.json`、`tab-ablation-before.json`、`tab-ablation-after.json`、`fixed-process.json` 和 `regression-before.xml`。
