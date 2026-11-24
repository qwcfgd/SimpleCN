# Vector VN16xx 硬件支持

## 部署

程序通过 Vector XL Driver Library V3 接口访问实际硬件，不需要为各个 VN16xx 型号分别建立设备名称白名单。设备是否提供 CAN、LIN 以驱动报告的通道兼容能力和可激活能力为准。无 LIN 收发器或无 LIN 能力的端口不会显示为 LIN 通道。

- 64 位程序：`driver/vector/x64/vxlapi64.dll`。
- 32 位程序：`driver/vector/x86/vxlapi.dll`。
- DLL 必须来自 Vector 官方安装包，并满足其许可和再分发条件。不要从无关 DLL 下载站点获取。
- 目标电脑仍须安装匹配的 Vector 硬件设备驱动。应用目录中的 XL DLL 不是 Windows USB/内核驱动安装程序，也不能替代硬件驱动。
- 加载使用程序目录下的绝对路径，只允许该 DLL 所在目录和 Windows 系统目录解析依赖，不搜索当前工作目录或 PATH。

本次实现使用本机已有 Vector 安装作为库来源的候选；库是否随交付物分发以 `driver` 目录的部署说明和许可证核对结果为准。

## 使用

1. 安装官方 Vector 硬件驱动并连接设备，在 Vector Hardware Configuration 中确认设备及收发器正常。
2. 启动上位机，刷新硬件列表，选择 `Vector VN... [序列号]` 对应的设备和通道，设置总线类型与速率，再连接。
3. 如果提示初始化权限不足，请先关闭占用该通道的 CANoe、CANalyzer 或其他应用，然后重连。本程序不擅自接管或修改其他程序正在使用的总线参数。
4. LIN 连接后先进入不启用响应的观察状态；按业务需要明确选择主节点或从节点、安装帧表并启动发送。

持久身份由硬件类型、序列号、总线及物理端口组成，不依赖当前的全局驱动通道序号。无序列号的设备仅使用运行期身份。相同物理端口不能同时由本程序的两个逻辑通道占用，包括 CAN/LIN 交叉占用。

## 支持范围与限制

- 经典 CAN：11/29 位 ID、标准/扩展帧、RTR、收发与发送完成回显，纳秒硬件时间戳转换为微秒；不提供 CAN FD、CAN XL 或总线控制器自动复位。
- LIN：1–20 kbit/s，主/从角色，经典/增强校验，诊断 ID 0x3C/0x3D 强制经典校验，响应表、即时主节点请求、从节点响应数据更新。
- LIN 自动长度报头扫描使用 XL 未定义 DLC / 校验模式，只发送报头并关闭本机该 ID 的响应。诊断 ID 0x3C/0x3D 仍使用硬件固定经典校验；业务接口仅接受六位原始 ID。
- DLC/校验定义变化时先停用通道、更新定义再启用；固定帧表的正常调度不会因此反复切换通道。
- LIN 调度仅支持无条件槽。Qt 定时器驱动的软件调度不是硬件实时调度，Windows 调度抖动可能延长周期，不能用于严格实时认证。槽延迟须覆盖帧线速时间及余量。
- 帧/轮边界停止在槽延迟到期后执行，不会补发积压槽。停止会关闭所有 ID 的响应；从节点数据更新成功表示缓存已接受，不等同于总线上已经发送，实际响应仍需要主节点报头。
- 接收会过滤非当前通道事件，保留错误标志；不支持的事件和协议不会伪装成普通 CAN/LIN 数据。

## 验证边界

`test_vector` 使用注入的模拟 XL API，不加载真实 DLL、不打开硬件、不发送总线报文。覆盖能力枚举、稳定身份、64 位通道掩码、初始化权限、失败清理、独占通道、CAN 编解码和 LIN 角色/调度/停止。编译及模拟测试不等于实机验收；本次未进行物理总线收发验证。投入实际设备前，应在获授权的隔离测试总线上验证收发、拔插恢复、总线故障与各型号收发器行为。

## API 来源

- Vector 官方 XL Driver Library 产品与下载入口：https://www.vector.com/en/business-unit/testing-validation-diagnostics/products/network-interfaces/xl-driver-library/
- ABI 对照：Vector `vxlapi.h`（BUSMASTER 项目保存的外部 SDK 头文件）：https://github.com/rbei-etas/busmaster/blob/master/Sources/BUSMASTER/CAN_Vector_XL/EXTERNAL/vxlapi.h

实现仅声明所需的固定宽度 ABI 类型和函数入口，并对结构大小及关键偏移作静态断言，未把外部 SDK 整包或参考头文件加入工程。
