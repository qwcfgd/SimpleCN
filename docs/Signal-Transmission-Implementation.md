# 信号发送实施与验证

更新：2026-09-20。已使用 testsrc 中的真实 DBC/LDF 完成软件验证；物理 CAN/LIN 时序尚需实机验收。

## 实现内容

每个软件通道新增第三页“信号发送”，与下载、UDS 共用连接、报文监视及日志。支持经典 CAN 的 DBC / 自建帧单次和周期发送，以及 LDF 的主节点调度、指定从节点响应和监听。源文件只读；每个通道独立工作副本。

信号按位初始化，文件初值只覆盖使用值；64 位整数不经浮点或 JSON number 往返。物理反解使用十进制有理数运算，向零截断；位宽溢出保留错误草稿，发送上一完整有效值。支持整帧 HEX、枚举、分段换算显式选择、回退与配置基准恢复。Q03 已落实：自建 CAN 始终按 ID 自动判型，不提供手工覆盖。

CAN 调度使用绝对到期时间和最小堆；每次处理有预算，漏期计数，不补发堆积请求。LIN 只装载一张硬件调度表，使用全部 256 槽池；使能更新通过旧表首槽前的硬件断点在整轮后应用；手动切表在当前帧结束后从新表首槽开始。从节点响应集合是所选表与实际发布者的交集；安装失败关闭全部响应。热更新使用 `UpdateByteArray`，不在硬件调度中插入 `Write`。

全局通道配置保存数据库路径、摘要、有效值、自建帧、发送使能、调度表和角色；全局通道配置升级至版本 3，兼容版本 1/2。64 位 raw 使用字符串。配置事务完整校验后应用，源路径缺失或摘要变化时拒绝应用并保留现状；错误草稿保存时采用上一有效完整帧。读取、重连、重启均为停止状态。

MVVM 依赖与职责见 [架构检查](MVVM-Audit.md)。操作方法见 [使用说明](User-Guide.md)。

## 支持范围

| 类型 | 首版范围与限制 |
|---|---|
| CAN | 经典 CAN，标准/扩展 ID，0–8 字节；CAN FD 不可发送。DBC 标准和扩展帧可有相同数值 ID；自建帧按全库数值 ID 查重。 |
| DBC | dbcppp 完整语法解析后映射整数信号、Intel/Motorola 位序、枚举、初值/周期属性、单层复用。浮点、扩展/嵌套复用等帧显式标为不可发送。 |
| LDF | 1.3 / 2.0 / 2.1 / 2.2 / 2.2A 声明；节点、普通帧、1–16 位标量、8–64 位字节数组、信号初值、逻辑枚举、分段物理换算、普通调度槽。按发布节点协议决定逐帧 checksum。 |
| LDF 未执行语义 | 事件/偶发帧、配置命令槽、ASCII / BCD 换算显式诊断，不静默执行。部分已识别辅助块只保留诊断；未知语义拒绝。 |
| LIN 时序 | 使用通道波特率；按完整帧时间、整数毫秒、PLIN 4–65535 ms 和 1–256 槽校验，拒绝小数 delay，不舍入。 |
| 文本编码 | 首版要求 UTF-8（可含 BOM），拒绝无效编码；不改写源文件。单数据库读取上限 32 MiB。 |

LIN 切表采用调度暂停确认和保守的在途帧等待时间。可用时记录旧末帧至新首帧的硬件时间戳间隔，否则使用主机到达时间估计。帧事件间隔不等于线路空闲时间；无完整事件时明确标为不可测。准确的物理间隙、周期误差和负载极限仍需目标硬件测量。

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

- DBC/LDF 导入、解析摘要及诊断文本、回退/撤销、DBC 节点/方向和 LIN 角色/调度编辑统一移至“通信配置…”窗口。主页面保留数据库树、待发送表（含可编辑 HEX 报文列）和信号值表及发送操作。
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

更新后该 LDF 有 8 张可执行调度表（包括原先受限的 4 张诊断表）；1 张涉及重复 ID 的表仍不可执行。

复现：

```powershell
cmake --build --preset release-qt6
ctest --preset release-qt6 -R "^signal_" --output-on-failure
```

Qt 5 改用 `release-qt5`。测试默认从原工程 `testsrc` 读取，可用 `HOST_SIGNAL_SAMPLE_DIR` 指定其他样本目录；未提供原件时仅跳过这两个本地样本用例。各构建目录的 `tests/testsrc-dbc.json`、`tests/testsrc-ldf.json` 保存原生探针报告，`tests/artifacts/testsrc-*.png` 保存页面及配置窗口截图，`testsrc-tests.log` 保存本轮回归结果。

### DBC 节点发送列表

导入 DBC 后待发送列表为空。在通信配置中选择节点及 Tx、Rx 或 Tx/Rx：Tx 包括报文发布者和 BO_TX_BU_ 附加发送者，Rx 包括任意信号接收者；Tx/Rx 为去重并集。切换节点或方向替换数据库报文，保留自建报文及其数据。方向相对于所选节点，列表内的报文均作为本通道发送内容。

CAN 列表空白处右键“自建报文…”创建并加入列表，报文行右键“删除报文”移除该项。删除数据库报文只移出列表，不修改数据库；删除自建报文移除其本地定义。运行时禁止结构操作。原自建按钮、启用/已提交/漏期列及独立帧 raw 输入框已移除；CAN 和 LIN 的“报文”列可编辑线序 HEX，与信号值同步，非法输入保留错误草稿及上一有效发送数据。

配置保存节点、方向和当前列表（包括手动删除结果），兼容旧配置的 enabled 字段；回退和恢复基准覆盖列表变更。新增界面回归覆盖方向筛选、切换保留自建项、右键创建/删除、配置恢复、HEX 编辑及运行时锁定；本地 testsrc 样本验收随界面更新。

2026-09-20 验证：Qt 5.15.19 与 Qt 6.8.4 Release 编译通过；两套 CTest 各 10/10 套件通过。testsrc/test.dbc 与 testsrc/test.ldf 均通过后台导入、界面显示、配置往返和模拟通信测试，源文件字节保持不变。已检查样本主界面和通信配置截图。构建及回归日志位于对应构建目录的 queue-*.log，截图位于 tests/artifacts/testsrc-*.png。

### 信号界面与诊断调度更新（2026-09-20）

主页面移除所选帧 ID/发布者/修订/RX 详情和已停止/已提交/漏期提示。CAN 列表按数值 ID 升序排序（同 ID 标准帧在前），自动连续编号，删除状态列，报文 HEX 占用末列剩余宽度。信号表初值标题分别为 dbc初始raw、ldf初始raw；发送物理值提供枚举下拉框（精确 raw 位模式提交，有物理换算时也可输入数值）；只读值标灰，注释列显示 DBC 信号注释或 LDF 信号声明的行尾/紧邻前置注释，无注释留空。

LIN 方向列为“帧头 · 数据发送类型”：主节点为 Tx · Tx / Tx · Rx，从节点为 Rx · Tx / Rx · Rx，观测为 Rx · Rx。MasterReq 发布者为主节点，SlaveResp 发布者为所选从节点。

诊断帧 60/61 使用 8 字节、经典校验和，并逐槽保留调度顺序、重复项和 delay；不存在显式诊断定义时，标准 MasterReq/SlaveResp 调度引用会创建对应的原始帧。按用户要求，硬件使用 unconditional 槽，不使用会抑制未更新请求的诊断槽类型，见 [PLIN API 文档 §3.7](https://www.peak-system.com/produktcd/Develop/PC%20interfaces/Windows/PLIN-API/PLINAPI_enu.pdf)。旧配置中首版诊断限制会在恢复时迁移。

共享通信驱动 `C:/Documents/0_Qt/Qt-ACTestController/resource/communication/SignalLinExtensions.cpp` 已同步允许诊断 ID 调度和热更新，并校验诊断长度及 checksum。变更副本保存于 `patches/communication-lin-diagnostic-slots.patch`；其他环境需在通信模块所在仓库应用该补丁。未改动该外部仓库中的其他现有改动。

本次最终验证：Qt 5/6 Release 编译成功，各 10/10 CTest 套件通过；信号界面 28 项、解析/调度 22 项、驱动 7 项各套均通过。testsrc 的 DBC/LDF 源文件保持不变；LDF 可执行诊断调度表全部通过模拟启动，旧诊断配置迁移通过。已检查普通与诊断表截图。日志为对应构建目录 presentation-final-build.log、presentation-final-tests.log，截图为 tests/artifacts/testsrc-*.png。诊断物理总线时序未做实机测量；当前证据来自模拟调度与 PLIN API 驱动桩。

### RAW 编辑、发送使能与手动切表（2026-09-20）

UDS 删除手动 HEX 开关、服务条件说明和 CDD 版本/项目数提示。RAW 区域空闲时可直接修改；“发送”旁的“撤销”恢复修改前参数生成的请求，包括抑制正响应设置。自定义 RAW 期间参数冻结，选择另一服务会重置自定义状态；CDD 请求校验仍生效。下载镜像与下载任务改为上下布局；开始下载、取消与进度条同一行。

通信配置移除“信号初值/配置文件”按钮、子窗口、独立配置文件读写及其异步入口，仅保留全局通道配置的序列化/恢复；确认按钮为“确认”。DBC 树按节点下 Rx、Tx 分组，表头为“节点/发送属性/报文”和“ID”。CAN 待发送表新增发布节点；LIN 新增 ID、帧类型、字节数，“发布者”改为“发布节点”。

两个发送表第一列为“使能”，新加入的报文默认勾选，取消勾选保留报文及配置。配置中的 sendEnabled 保存勾选状态，旧配置缺少该字段时默认勾选；原 enabled 字段继续表示 CAN 报文是否在待发送列表中。

CAN 单次发送仅提交启动快照中勾选的报文。周期发送的使能更新在一轮结束后生效：一轮指当时全部使能且未失败的报文各完成一次提交；不同周期的报文仍保持各自绝对期限。当前轮中取消勾选的项仍完成本轮；启用新项从下一轮参与。全部取消时任务保持空闲，可重新勾选恢复。

LIN 主节点按当前完整调度轮次处理使能更新，下轮调度仅包含勾选项，保留它们的原顺序、重复槽及 delay。硬件使用首槽断点确认整轮完成；SIM 使用显式轮末。LIN 从节点通过接收事件识别所选有效表的完整顺序，在观察到轮末后更新响应集合；没有外部主节点事件时不推测轮次。观测节点不响应。

LIN 待发送表上方增加与通信配置同步的调度选择框。手动切表优先等待当前帧完成，然后从新表首槽开始；同时待应用的使能状态在新表启动时合并。SIM 使用当前帧完整传输时间；PLIN 先 SuspendSchedule，确认暂停并等待最多 8 字节帧的保守传输时长，再排空旧事件、安装新表。等待期间保留旧响应数据。停止及旧运行代次命令不会重新启动调度。

共享驱动新增接口已更新到原共享模块；增量补丁为 `patches/communication-lin-frame-boundary.patch`，在已有 signal-lin-shared 与 diagnostic-slots 补丁的基础上应用。SDK 替身测试确认暂停不清空在途帧响应、整轮断点与帧边界分离、切表前排空队列。物理线路的帧完成与切表间隔仍需 PEAK 硬件测量。

回归日志保存为各 Qt 构建目录中的 `controls-final-build.log`、`controls-final-tests.log`；界面截图在 `tests/artifacts/`。新增覆盖 RAW 撤销、下载布局、勾选持久化、CAN 分批整轮更新/空表恢复、LIN 整轮更新/当前帧切表、从节点外部轮次识别和两个调度下拉框同步。

本轮结果：Qt 5.15.19、Qt 6.8.4 均完成 Release 编译和 10/10 完整套件回归，最后的列宽与观测模式修正后，两套信号相关 3/3 套件再次通过（controls-polish-*.log）。最终信号解析/调度 27 项、驱动 8 项、信号界面 30 项、UDS 界面 19 项通过。testsrc 的 DBC/LDF 均正常导入及显示并保持源字节不变，已查看最终样本、通信配置、UDS 与 1366×768 下载布局截图；MVVM 边界检查及 git diff --check 通过。

## 1.3 构建目录更新

以上原构建记录保留为历史。1.3 起程序本体仍位于 `../build/Qt-GeneralController-qt5` / `qt6`；CMake 构建树、测试、调试和验证工具统一改到 `../build/qttemp/Qt-GeneralController-qt5` / `qt6`，详见根目录 README。
