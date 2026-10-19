# SimpleCN V1.45

## 1. CAN 与 LIN 通道视觉重点色与标题同步

- **色彩差异化**：为 CAN 与 LIN 类型的软件通道配置了淡雅且舒适易区分的重点色：
  - **CAN 通道**：雅致板岩蓝 / 晴空蓝（选中左侧通道与任务标签背景 `#E6EEF6`，强调线 `#2B6CB0`，深蓝文字 `#1E4E79`，未使能背景 `#EDF2F7` / `#EBF0F6`，主按钮 `#2B6CB0`，进度条 `#3B82C4`）。
  - **LIN 通道**：柔和鼠尾草绿 / 苍松绿（选中左侧通道与任务标签背景 `#E7F2EB`，强调线 `#2D7A5A`，深绿文字 `#1E5E3A`，未使能背景 `#EDF4F0` / `#EBF2ED`，主按钮 `#2D7A5A`，进度条 `#38A169`）。
- **左侧通道导航栏**：`ChannelTabBar` 重载绘制事件，针对 CAN 和 LIN 通道分别渲染淡雅背景、左侧高亮强调线、状态指示图标及高对比度标题文字，未选中状态保持通透无侵入。
- **任务选项卡同步**：同步 LIN 通道的第 3 标签页标题为“信号发送”（保持与 CAN 一致；监听模式依然准确显示“信号接收”）。

## 2. 报文发送列表空白提示框消除

- 修复 `CanTxTableModel::data()` 与 `LinScheduleTableModel::data()` 中 `Qt::ToolTipRole` 处理逻辑：
- 当报文无注释（`comment`）、无诊断问题（`issue`）且无帧发送错误（`frameError`）时，严格返回无效 `QVariant()`，杜绝鼠标悬停在待发送报文上时弹出空白提示方框的问题。
- 当存在有效注释或错误信息时，干净换行拼接，保留完整诊断提示。

## 3. MVVM 架构审查与边界合规

- 运行 `scripts/check_mvvm_boundaries.py` 静态检查，各层包含关系及抽象边界严格合规。
- 确认 View、ViewModel、Model / Protocol / Infrastructure 分离良好：View 只负责展示、控件与事件委托；ViewModel 管理草稿、命令与状态；底层 Worker 串行驱动硬件。
- 优化测试套件环境超时配置，确保 Qt 5 与 Qt 6 全套测试在各运行环境下 100% 稳定通过。

## 4. 版本与验证记录

- 项目版本统一升级至 **1.45**（Windows 资源文件版本 **1.45.0.0**）。
- 验证记录：
  - `cmake --preset qt6` 及 `cmake --build --preset qt6` 编译成功。
  - `ctest --preset qt6` 全部 16 个测试套件 100% 通过（16/16 passed）。
  - `cmake --preset qt5` 及 `cmake --build --preset qt5` 编译成功。
  - `ctest --preset qt5` 全部 16 个测试套件 100% 通过（16/16 passed）。
