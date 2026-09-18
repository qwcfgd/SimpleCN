# 信号发送实施与验证

日期：2026-09-18。软件实现及双 Qt 验证已完成；真实数据库样本与物理 CAN/LIN 验收尚未完成。

## 实现内容

每个软件通道新增第三页“信号发送”，与下载、UDS 共用连接、报文监视及日志。支持经典 CAN 的 DBC / 自建帧单次和周期发送，以及 LDF 的主节点调度、指定从节点响应和监听。源文件只读；每个通道独立工作副本。

信号按位初始化，文件初值只覆盖使用值；64 位整数不经浮点或 JSON number 往返。物理反解使用十进制有理数运算，向零截断；位宽溢出保留错误草稿，发送上一完整有效值。支持整帧 HEX、枚举、分段换算显式选择、回退与配置基准恢复。Q03 已落实：自建 CAN 始终按 ID 自动判型，不提供手工覆盖。

CAN 调度使用绝对到期时间和最小堆；每次处理有预算，漏期计数，不补发堆积请求。LIN 只装载一张硬件调度表，使用全部 256 槽池；整轮切换通过旧表首槽前的硬件断点确认。从节点响应集合是所选表与实际发布者的交集；安装失败关闭全部响应。热更新使用 `UpdateByteArray`，不在硬件调度中插入 `Write`。

独立通信配置保存数据库路径、摘要、有效值、自建帧、调度表和角色；全局通道配置升级至版本 3，兼容版本 1/2。64 位 raw 使用字符串。配置事务完整校验后应用，源路径缺失或摘要变化时拒绝应用并保留现状；错误草稿保存时采用上一有效完整帧。读取、重连、重启均为停止状态。

MVVM 依赖与职责见 [架构检查](MVVM-Audit.md)。操作方法见 [使用说明](User-Guide.md)。

## 支持范围

| 类型 | 首版范围与限制 |
|---|---|
| CAN | 经典 CAN，标准/扩展 ID，0–8 字节；CAN FD 不可发送。DBC 标准和扩展帧可有相同数值 ID；自建帧按全库数值 ID 查重。 |
| DBC | dbcppp 完整语法解析后映射整数信号、Intel/Motorola 位序、枚举、初值/周期属性、单层复用。浮点、扩展/嵌套复用等帧显式标为不可发送。 |
| LDF | 1.3 / 2.0 / 2.1 / 2.2 / 2.2A 声明；节点、普通帧、1–16 位标量、8–64 位字节数组、信号初值、逻辑枚举、分段物理换算、普通调度槽。按发布节点协议决定逐帧 checksum。 |
| LDF 未执行语义 | 事件/偶发帧、诊断/配置槽、ASCII / BCD 换算显式诊断，不静默执行。部分已识别辅助块只保留诊断；未知语义拒绝。 |
| LIN 时序 | 使用通道波特率；按完整帧时间、整数毫秒、PLIN 4–65535 ms 和 1–256 槽校验，拒绝小数 delay，不舍入。 |
| 文本编码 | 首版要求 UTF-8（可含 BOM），拒绝无效编码；不改写源文件。单数据库读取上限 32 MiB，独立配置 8 MiB。 |

LIN 切表日志区分三种证据：硬件断点确认的轮末、旧末帧至新首帧硬件时间戳间隔、主机配置/到达时间估计。帧事件间隔不等于线路空闲时间；无完整事件时明确标为不可测。准确的物理间隙、周期误差和负载极限仍需目标硬件测量。

## 源码与共享模块

本工作树包含原工作区未提交的 UDS/CDD 基线，复制时的文件摘要见 `Signal-Transmission-Source-Baseline.json`。未用 Git HEAD 覆盖原工作区内容。

共享驱动改动交付为 `patches/signal-lin-shared.patch`，相对于当时共享模块的现有工作副本生成，包含 `SignalLinExtensions.cpp`、CMake 接入、`tstPeakLin` 新接口及已暂停调度的重复停止修复。当前使用的共享模块已应用该改动，没有在本仓库复制第二套驱动。

对另一份匹配基线的共享模块，先检查补丁，再应用一次：

```powershell
git -C <共享模块仓库> apply --check <本仓库>/docs/patches/signal-lin-shared.patch
git -C <共享模块仓库> apply <本仓库>/docs/patches/signal-lin-shared.patch
```

补丁不包含共享模块此前已有的其他未提交修改；那些基线修改仍需按原项目流程交付。不能直接假定该补丁适用于任意旧版本。

## 固定依赖与构建

运行时保持原生 C++，不启动 Python。`cmake/SignalDependencies.cmake` 固定：

| 依赖 | 固定版本 / commit | 许可 |
|---|---|---|
| dbcppp | 3.8.0 / `b520607559223ac02a7ca87d47b4932cd9f3d21b` | MIT，随包 `docs/licenses/dbcppp-MIT.txt` |
| Boost Multiprecision | 1.84 / `de3aded8632e0ef0f17dcaf274f5699a25139738` | Boost Software License 1.0 |
| Boost Math | 1.84 / `44af29a78c85ee89ce37f7f43d532afd05c3d981` | Boost Software License 1.0 |

只构建 dbcppp DBC 库；Boost 其余头文件来自其固定源码。MinGW 字节交换使用小型兼容头。GCC 8.1 的 Windows `filesystem` 头无法编译，因此在生成目录中去掉未使用的 dbcppp 文件路径便捷 API；解析器不改动，应用仍由 QFile 读取 Unicode 路径并调用流解析 API。Qt 5/6 的 DLL 分别构建，不能混用。

首次 CMake 配置自动下载依赖。离线构建可设置：

```text
FETCHCONTENT_SOURCE_DIR_SIGNAL_DBCPPP=<dbcppp 固定源码>
FETCHCONTENT_SOURCE_DIR_SIGNAL_MULTIPRECISION=<multiprecision 固定源码>
FETCHCONTENT_SOURCE_DIR_SIGNAL_BOOST_MATH=<math 固定源码>
COMMUNICATION_SOURCE_DIR=<共享仓库>/resource/communication
```

构建预设以用户指定的原工程目录为基准，固定输出到 `C:/Documents/0_Qt/build/Qt-GeneralController-qt5` 与 `C:/Documents/0_Qt/build/Qt-GeneralController-qt6`；可执行文件为各目录下的 `QtBootloader.exe`，不随 Codex worktree 位置变化。主分支同步后已在这两个目录重新配置并完成 Release 编译，CMake 源码目录与依赖缓存均已切回原工程及其上一级构建目录。此前的 `build/signal-qt5` / `build/signal-qt6` 和 worktree 上一级的构建目录均为历史验证目录，后续构建不再使用。Qt 5.15.19 使用 GCC 8.1.0；Qt 6.8.4 对应本机 `mingw1200_64` / `MinGW/12.0.0` 路径，但编译器实际报告 GCC 14.2.0。

部署需随带 `libsignal_dbcppp.dll`、Qt/MinGW、PEAK API 及许可文件，`host_deploy` 和打包脚本已接入。

## 软件验证

Qt 5.15.19 和 Qt 6.8.4 均完成完整应用与测试构建，各 10 个 CTest 套件通过：`communication`、`host_ui`、`protocol`、`can_protocol`、`download_revision`、`cdd`、`uds_ui`、`signal_driver`、`signal_codec`、`signal_ui`。架构调整及后续信号修正后，受影响套件已重新通过。

新增测试覆盖：精确十进制、位宽与 64 位边界、跨字节及 Motorola 布局、复用分支保留、LIN 数组/分段、编码和非法输入、源文件不变、周期漏期/预算、整轮边界、响应白名单与失败清理、停止幂等、旧运行命令失效、配置事务/撤销基准、后台导入、RX/TX 分离、角色状态绑定、通道互斥及独立运行。

`tests/fixtures/signals-synthetic.dbc` / `.ldf` 为自行构造样本，不代表用户数据库或真实 ECU。驱动测试使用 SDK 替身，界面和 worker 测试使用 simulation；没有访问物理总线。

固定 cantools 44.1.0 / ldfparser 0.26.0 的开发对照结果：两套 Qt 各匹配 **9 个帧布局、24 组 payload 向量**；浮点帧只比布局，明确保持不可发送。其余依赖固定于 `tests/requirements-signal-reference.txt`。复现：

```powershell
python -m pip install -r tests/requirements-signal-reference.txt
cmake --build <构建目录> --target signal_fixture_probe
python scripts/compare_signal_references.py --probe <构建目录>/tests/signal_fixture_probe.exe --report <构建目录>/tests/reference-comparison.json
python scripts/check_mvvm_boundaries.py
ctest --test-dir <构建目录> --output-on-failure
```

对照工具在 Python 3.12 下验证，安装仅用于开发环境。比较脚本不自动连接设备。软件报告和界面截图位于各构建目录 `tests/` 与 `tests/artifacts/`。

此前 worktree 完整回归每套有 3 项按条件跳过：本地 CDD 样本的模型/界面验收各一项，以及需显式启用的真实 LIN 测试。部署目录的 `VerifyRelease` 已在 Qt 5/6 的 Windows 平台插件下通过启动、相对路径配置和三通道模拟下载自检。发布目录不附带测试专用的 offscreen 插件。本次主目录回归结果见下文。

尚需用户样本与实机验收：真实 DBC/LDF 方言覆盖、目标设备 checksum 和收发回显、主从角色切换、硬件断点行为、队列溢出、停止后的在途帧、切表间隙、最小周期误差与长时间高负载。仿真和对照库通过不能替代这些结果。


## 通道导航与二级配置更新（2026-09-18）

- DBC/LDF 导入、解析摘要及诊断文本、回退/撤销、自建 CAN 和 LIN 角色/调度编辑统一移至“通信配置…”窗口。主页面保留数据库树、发送表、所选帧 raw 和信号值表及发送操作。
- 移除创建通道按钮；标题列空白处右键新建，已有标题右键按“修改通道、删除通道”排序。底部保留一行空白，列表支持滚轮滚动且不切换当前通道。
- 创建/修改共用硬件配置编辑器和独立草稿 ViewModel，确认才提交，取消不影响原通道。修改保留当前通道的下载、诊断和信号配置；已创建通道类型固定，连接或运行期间不允许修改硬件配置。
- 默认硬件选择移入 ChannelViewModel，避免依赖主页面控件是否存在；重名、占用和忙状态校验位于 ChannelConfigurationViewModel。
- 此次 UI 更新最初仅完成静态检查和测试源码调整；主分支同步后的验证已覆盖这些改动，结果如下。

## 主分支同步与编译验证（2026-09-18）

- 核对 worktree 保存的源文件摘要，确认原工程未在该基线之上产生冲突修改；同步前备份主目录改动，保留主目录已有的旧图标删除。
- 修正两处 UI 回归用例仍从主页面查找硬件控件的问题，改由 `ChannelHardwareEditor` 验证端口占用、释放和模式选择，避免控件迁移后的空指针访问。
- Qt 5.15.19 / Qt 6.8.4 均从 `C:/Documents/0_Qt/Qt-GeneralController` 完成 Release 编译，各 10 个 CTest 套件全部通过；每套仅真实 LIN 测试按条件跳过。日志为各构建目录内的 `main-sync-tests.log`，XML 报告位于 `tests/`。
- MVVM 静态规则通过；两个新生成的 `QtBootloader.exe --version` 均成功返回 `GBoot 0.2.2`。

## 本地 DBC / LDF 样本验证（2026-09-18）

使用原工程 `testsrc/test.dbc` 和 `testsrc/test.ldf` 原件执行测试，未将样本加入版本控制，测试前后 SHA-256 一致。

| 文件 | 导入结果 | 原生编解码探针 |
|---|---|---|
| `test.dbc` | 2 个节点、5 个报文、5 个信号 | 15 组 payload 向量 |
| `test.ldf` | 8 个节点、58 个帧、283 个帧内信号、9 张调度表 | 156 组 payload 向量 |

Qt 5/6 的解析及 payload 报告完全一致。两套 Release 重新编译成功，`signal_codec`、`signal_driver`、`signal_ui` 三个套件各自全部通过。新增本地样本用例覆盖后台导入、报文树/信号表/配置窗口显示、配置保存与恢复、CAN 单次及 LIN 默认调度模拟发送、停止和源文件保持不变；界面截图已核对。没有访问物理总线。

本轮修正：

- dbcppp 不支持的 `BA_DEF_REL_` / `BA_DEF_DEF_REL_` 关系属性声明经过独立语法检查后，从传给上游的内存副本中屏蔽，并在导入摘要中说明。文件原文、行号和信号精确十进制定义保持不变；畸形声明仍报错。
- LDF 的不同命名帧可以共享 ID 并分别浏览；这类帧使用不同内部键保存，明确标记为暂不支持发送或按 ID 接收解码，防止工作副本覆盖或错误关联。重复名称和未知发布节点仍报错。
- `10.000 ms` 等整数值十进制时延采用精确转换；仿真和硬件调度共用转换逻辑，避免字符串整数转换失败而得到 0。非整数时延仍拒绝、不舍入。

该 LDF 有 4 张当前支持执行的调度表；另有 4 张包含诊断槽，以及 1 张涉及重复 ID，后 5 张可查看但当前信号发送功能不执行。文件能够打开不代表其所有调度语义都已实现。

复现：

```powershell
cmake --build --preset release-qt6
ctest --preset release-qt6 -R "^signal_" --output-on-failure
```

Qt 5 改用 `release-qt5`。测试默认从原工程 `testsrc` 读取，可用 `HOST_SIGNAL_SAMPLE_DIR` 指定其他样本目录；未提供原件时仅跳过这两个本地样本用例。各构建目录的 `tests/testsrc-dbc.json`、`tests/testsrc-ldf.json` 保存原生探针报告，`tests/artifacts/testsrc-*.png` 保存页面及配置窗口截图，`testsrc-tests.log` 保存本轮回归结果。
