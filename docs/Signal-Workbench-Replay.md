# 信号工作台、日志回放与项目配置

## 待发送报文和调度

未载入数据库时，CAN 待发送报文表右键可新建报文；LIN 调度选择框右键新建调度表，默认名称依次为 Schedule_Default001、Schedule_Default002 等，待发送帧表右键新建帧。默认周期或 delay 为 100 ms。

待发送表中的 ID 可编辑。当前表存在目标 ID 时撤销修改；命中数据库时采用该报文的名称、发布节点、类型、长度、默认数据及周期/delay。LIN 的 ID 修改仅影响当前调度表，内部使用独立帧实例，其他表已编辑的相同 ID 数据不会被覆盖。

CAN “多次发送”按左侧次数发送各使能报文，每个报文遵循自己的周期。“周期发送”持续发送。LIN 主节点的“多次调度”按次数执行完整调度轮次，“周期调度”持续执行；从节点、观测节点的多次调度和次数框禁用。待发送项全部取消使能时，普通发送/调度按钮与次数框禁用。

## 回放

在“通信配置…”中导入一个 ASC 或 BLF 文件，使用“通道映射…”将检测到的 CAN/LIN 日志通道分别映射到兼容的软件通道，或选择“无”以忽略。支持多个日志通道合并到同一软件通道，也支持同时映射到多个软件通道，由导入页面统一启动和停止。目标通道须已连接且空闲。

导入后按钮变为“多次回放”“周期回放”，不受待发送表使能状态限制。重置清除回放源和映射，恢复普通发送模式。启动时验证所有目标，任一参与通道失败则停止本次回放组。

- 模拟硬件尽可能快地重现日志，Tx/Rx 分别显示为 Sim Tx/Sim Rx，图像及监视保留原始时间关系。
- 真实硬件以原始 1 倍速运行，仅排除数据库明确属于当前节点发布的 ID；无数据库、未选节点或未知 ID 均允许参与。
- LIN 按实际角色处理：主节点调度帧头；从节点更新响应缓冲并等待外部主节点；观测节点只进行软件重现。
- 已使能待发送项覆盖匹配 ID 的数据和长度，保留日志时间/顺序；日志未包含的使能 ID 按配置周期额外发送，同样遵循节点过滤规则。
- 运行中调整已有待发送项的数据或使能状态，可更新回放覆盖。
- 目前日志读取支持正常经典 CAN、CAN FD、LIN 记录；非正常/不支持的对象计入跳过数量。真实硬件仍采用经典 CAN API，CAN FD 硬件回放会在启动前明确拒绝。

为限制单次导入资源，读取器限制文件和累计解压内容为 1 GiB、正常记录最多 200 万条。日志导入在后台进行。

## 项目配置

硬件配置页中的“保存项目配置”保存软件通道、硬件、数据库引用、手工帧、调度表、编辑值、次数、回放路径/映射、监视选项、图像信号实例/分组/颜色/顺序/选择及图像控件/缩放设置。启动加载已保存配置，不自动恢复发送或回放运行状态。关闭程序发现未保存配置时，提供“保存并退出”“不保存”“取消”。

程序标题为 Qt-GeneralController V1.3，右下角不再显示 ReleaseVer 标记。报文导出和图像交互详见 [报文监视与图像观测](Trace-and-Graphics.md)。

## MVVM 分层检查

本次检查将跨通道回放计划生成、映射验证、数据库节点过滤、统一启动/停止和完成状态管理，从 MainWindow 移入 ReplayViewModel。项目配置快照和变更判定移入 ChannelConfigurationViewModel。

| 层 | 职责 |
| --- | --- |
| View：MainWindow、SignalTransmitPage、SignalPlotDialog | 窗口、控件、菜单、选项绑定、文件选择及关闭确认 |
| View：SignalPlotCanvas | 画布绘制、坐标刻度、鼠标命中与缩放；不访问硬件或解析日志 |
| ViewModel：ReplayViewModel | 多通道回放协调和回放计划 |
| ViewModel：SignalTransmitViewModel、SignalTableModels | 编辑验证、使能/次数/角色、可执行状态和发送命令 |
| ViewModel：SignalPlotModel | 信号实例/分组、解码结果、采样、抽稀、光标值与差分 |
| ViewModel：ChannelConfigurationViewModel | 配置保存/加载、快照与未保存变更判定 |
| Model/Domain：SignalCodec、FrameTableModel、SignalTypes | 信号编解码、监视记录和业务数据 |
| Infrastructure/Protocol：TraceReader、TraceExporter、SignalTransmitter、LinScheduleRunner | 文件读写、后台发送、真实/模拟定时与 LIN 调度 |

Qt 的 QAbstractItemModel 用于呈现数据，颜色、显示角色等属于展示模型。ViewModel 不依赖 QWidget 或具体 View；硬件操作保留在后台 worker/基础设施中。窗口只订阅回放通知并展示，不再生成 TxPlan。布局、刻度、拖动命中保留在 View，避免将像素级交互放入业务层。

本项目是 Qt Widgets 的 MVVM 实现，并非所有历史窗口都完全没有组合逻辑：MainWindow 仍负责创建通道页面、装配 ViewModel 和管理窗口生命周期。本次新增业务逻辑已从该组合入口分离。

## 验证边界

workbench 覆盖有限次数 CAN/LIN、使能限制、ID 冲突、数据库默认值、LIN 调度隔离及配置恢复、ASC/BLF 读回、模拟回放方向和覆盖、额外 ID、通道合并、分组重复信号、保留极值的 1,000 点抽样及关闭保存。

自动化测试使用模拟硬件和合成数据库；不替代真实 CAN/LIN 总线和 CANoe 验收。

2026-09-20：Qt 5.15.19、Qt 6.8.4 构建成功，各自完整 CTest 12/12 通过；分层依赖检查未发现下层引用 View 或 QWidget 控件。独立 python-can/vblf 读回核对通过。

同星后端接入后，硬件访问仍位于 Infrastructure，View/ViewModel 未引入厂商 SDK 依赖。后端结构与实机验证边界见 [同星硬件适配](Tosun-Hardware.md)。
