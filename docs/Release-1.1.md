# ReleaseVer: 1.1

本版定版于 2026-09-20。界面版本为 ReleaseVer: 1.1，程序及 CMake 版本为 1.1.0。

## 主要变更

- 设备连接、断开移至通道标题右键菜单；保留处理中和硬件占用限制，连接状态仍由通道标题指示灯呈现。
- 主页面顶部仅显示“运行模式 · 硬件选择 · 硬件通道 · 波特率”，例如“模拟模式 · 模拟LIN双通道适配器 · 通道1 · 19200 bit/s”。
- 信号发送页统一命名“报文工作台”，标题采用与 UDS 诊断控制台一致的 sectionTitle 样式；删除信号值/枚举弹窗及其逻辑，信号和枚举在表内直接编辑。
- LIN 待发送表删除类型列，调度选择框前增加“调度表”文字；表格列宽根据内容分配，报文与注释列使用剩余宽度。
- 保留 CAN/LIN 发送使能按下一轮生效、LIN 当前帧完成后手动切表、UDS RAW 编辑与撤销，以及 testsrc DBC/LDF 支持。
- 同步界面、命令行及打包元数据中的版本号。

## 构建与验证

构建目录固定为原工程上一级的 `build/Qt-GeneralController-qt5` 和 `build/Qt-GeneralController-qt6`。程序均为对应目录下的 `QtBootloader.exe`。

本版验证包括通道右键连接/断开、设备摘要格式、下载和 UDS 回归、testsrc DBC/LDF 导入显示、表内信号/枚举编辑、CAN/LIN 调度以及布局截图。编译与完整测试日志为构建目录内的 `release11-build.log`、`release11-tests.log`。硬件通信时序未在本轮做物理设备验证。

定版结果：Qt 5.15.19 与 Qt 6.8.4 Release 编译均成功，完整 CTest 各 10/10 通过；Qt5 最后硬件名称摘要修正后的 host_ui、signal_ui 复核通过。两套可执行文件 --version 均返回 GBoot 1.1.0。已检查 testsrc 的 DBC/LDF 页面和 1366×768 下载布局截图；MVVM 边界检查及 git diff --check 通过。
