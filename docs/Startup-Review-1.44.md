# V1.44 启动窗口与 MVVM 复核

## 启动小窗口

确认是 View 层控件初始化缺陷。`ChannelPage::build()` 创建 LIN 的 `rxdEnabled` 和 `scanHeaders` 时没有指定父控件，随后立即调用 `setVisible(true)`。Qt 会先将它们作为独立顶层窗口显示，再在加入报文监视布局时重新设为子控件，从而出现很小的窗口闪烁。

修复前，Windows Qt 6 的启动事件记录如下：

| 阶段 | 顶层窗口 | 尺寸 |
|---|---|---|
| 构造默认页面 | `QCheckBox / rxdEnabled` | 72 × 21 |
| 构造默认页面 | `QPushButton / scanHeaders` | 80 × 24 |
| 显示主体 | `host::MainWindow` | 1320 × 850 |

启动时恢复项目配置还会在重建 LIN 页时再次显示这两个临时窗口。主体窗口在构造完成后已设置尺寸，日志未发现它自身先以小尺寸显示再展开。

修复为创建两个控件时直接指定报文监视卡片 `frameCard` 为父控件。可见性规则保持 CAN 隐藏、LIN 显示。其他启动显示调用经事件捕获未发现额外顶层窗口。

`HostUiTest::startupShowsOnlyMainWindow` 覆盖默认启动及恢复包含 CAN/LIN 的项目：构造与恢复阶段不得显示顶层窗口；首次显示只能是已经设置尺寸的主窗口；切换 CAN/LIN 后验证两个控件的可见性。测试只使用模拟通道。事件 JSON 与测试日志保存在对应 Qt 构建目录的 qttemp 中。

## 架构结论

当前仍采用 Qt Widgets MVVM 的主要数据流，但不能描述为完全严格的 MVVM 分层。完整复核、已修正边界及剩余耦合见 [MVVM 复核](MVVM-Audit.md#2026-10-09-当前代码复核)。

本次将负责创建枚举下拉框及编辑提交的 `SignalValueDelegate` 从 ViewModel 源文件拆到 View 层，表格角色、验证和编码继续由 ViewModel / Model 处理。分层检查增加编辑委托、常见控件头文件、相对包含路径与本项目 resource 通信代码覆盖。

## 验证

Qt 5.15.19 / GCC 8.1.0 与 Qt 6.8.4 / GCC 14.2.0 的 Release 构建完成，分别通过 **16/16 CTest**。新增启动回归在修复前两个数据用例均失败，在修复后两套 Qt 均通过：首次顶层显示只包含 1320 × 850 主窗口，默认启动和恢复项目均没有提前显示的控件窗口。CAN 隐藏 / LIN 显示规则也通过。

信号枚举输入、编辑草稿保持、编码与语言回归通过；编辑委托函数体与移动前逐字一致。增强后的 MVVM 检查通过，额外的负例核查成功拒绝 5 个违规依赖，覆盖编辑委托、QtWidgets 控件、相对路径、界面对 SDK 直接依赖以及 resource 对展示层的反向依赖。

构建及完整测试日志位于 `C:/Documents/0_Qt/build/qttemp/Qt-GeneralController-qt5`、`Qt-GeneralController-qt6` 内的 `startup-after-build.log`、`startup-after-ctest.log`；窗口记录在各自 `tests/artifacts/startup-windows-defaults.json` 与 `startup-windows-restore.json`。修复前证据保留于 Qt 6 的 `startup-before-results.txt`。应用输出更新到约定的 Qt 5 / Qt 6 程序目录，产品版本保持 1.44。
