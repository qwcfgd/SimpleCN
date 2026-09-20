# 同星 TC1016 / TC1016P 适配

## 范围与使用

本轮接入经典 CAN（标准/扩展 ID、收发、时间戳与发送回显）及 LIN 的主节点、从节点、观测模式和调度接口；CAN FD 的发送及完整采集不在本轮范围。FD 控制器的接收队列可能包含经典 CAN 和错误帧，因此读取其 80 字节结构，但 FD 数据会明确标为不支持，不作为经典信号解析。

安装与程序位数匹配的 TSMaster 运行库。程序依次查找 `TOSUN_SDK_DIR`、程序目录 `dll/tosun`、TSMaster 默认安装目录（64 位使用 `bin64`）。本机使用 `C:/Program Files (x86)/TOSUN/TSMaster/bin64/libTSCAN.dll`。需要保留厂商配套依赖；工程不复制或分发专有 SDK DLL。

在硬件配置中选择“在线硬件”，刷新后选择 TOSUN 设备及物理通道，再设置波特率并连接。设备按序列号和物理通道持久化；同一适配器的多个 CAN/LIN 通道共享原生连接，关闭其中一路不会断开仍在使用的其他路。设备内置 CAN 终端电阻默认关闭，应按实际总线配置外接终端。

LIN 单独连接默认进入观测模式；启动任务时应用所选角色。停止任务时切换观测模式并清理调度；运行库提供响应配置重置接口时同时调用。本机 SDK 未提供该可选重置接口，从节点切表后旧响应的清除行为仍需真实 LIN 验收。LIN 调度使用主机精确定时器，支持帧边界与整表边界切换，但 Windows 调度不是硬实时，具体时序需实机验收。真实 ECU 发送前需给出已确认的波特率、角色和报文/调度参数。

## 架构

`ChannelWorker` 通过 `CanHardware` / `LinHardware` 接口进行硬件访问。`BusHardware` 保留 PEAK 实现并选择同星实现；`TosunRuntime` 管理 DLL、ABI、全局初始化、串行 SDK 调用和物理连接引用计数；`TosunHardware` 负责报文转换与 LIN 调度。厂商原始结构限制在基础设施内部。View/ViewModel 不包含厂商 SDK 调用，仍使用既有命令和状态模型。

参考同星官方 [libTSCAN 示例及公开头文件](https://github.com/TOSUN-Shanghai/libTSCANDemos)。运行时兼容厂商 `tslin_set_node_funtiontype` 的历史拼写。未使用缺少公开签名的接口。

## 验证

- Qt 5.15.19、Qt 6.8.4 构建成功，完整 CTest 各 13/13 通过。
- 模拟 SDK 测试覆盖设备枚举、共享连接释放、经典 CAN/FD 队列转换、错误帧、时间戳、LIN 角色恢复、有限调度边界、响应更新、停止清理及发送错误保持。
- 实机：2026-09-20，TC1016P，CAN1 与 CAN2 外部回环，500 kbps，内置终端关闭；标准帧/扩展帧双向各四帧，共 8/8 匹配，关闭一路后另一通道仍连接。
- LIN1 接真实 ECU，本轮尚未进行真实 LIN 发送验证；主/从节点响应与 ECU 时序不能以模拟 SDK 测试替代。
- CAN 下载仍沿用原项目限制，仅支持模拟模式；本轮未扩大下载功能范围。

手动工具 `../build/qttemp/Qt-GeneralController-qtN/tests/tosun_probe.exe` 默认只枚举设备。`--can-loopback <序列号>` 会打开 CAN1/CAN2 并最多提交 8 帧测试数据，遇到错误或无响应即停止；仅可用于已确认的隔离回环。`--termination` 仅供无外部终端的测试接线使用。该工具不属于自动 CTest，不打开 LIN 通道。

程序运行目录固定为 `C:/Documents/0_Qt/build/Qt-GeneralController-qt5`、`C:/Documents/0_Qt/build/Qt-GeneralController-qt6`。

CMake 缓存、测试和探测工具均保存在 `../build/qttemp/Qt-GeneralController-qtN`，不进入程序运行目录。
