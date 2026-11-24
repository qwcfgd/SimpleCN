# 设备运行库打包（2026-11-10）

## 工程与程序目录

工程根目录新增 `driver/`，按厂商和位数隔离：

- `driver/peak/x64/PCANBasic.dll`、`PLinApi.dll`。
- `driver/tosun/x64/libTSCAN.dll`、`libTSH.dll`、`binlog.dll`、`libLog.dll`，以及该版本需要的 `msvcp140.dll`、`vcruntime140.dll`、`vcruntime140_1.dll`。
- `driver/vector/x64/vxlapi64.dll`。

`cmake/DriverRuntime.cmake` 在构建程序或测试目标时将整个源 `driver` 子目录复制到对应可执行文件旁的 `driver` 子目录。采用每个输出目录唯一的依赖目标，避免并行重复复制；只修改运行库而不修改 C++ 时也会执行复制。正常 Qt 5/Qt 6 程序路径仍使用工程现有 presets，测试与中间文件仍在 `../build/qttemp`。旧 `dll/` 目录不再被加载或收入新发布包；已有旧目录不会被构建脚本擅自删除。

运行库按程序位数选择，不能用 64 位 DLL 替代 32 位 DLL。目前原工程与本机已取得的运行库为 x64。`x86` 为预留目录，32 位 PEAK DLL 缺失会在配置阶段明确报错。

PEAK、TOSUN、Vector 使用程序绝对路径安全加载，不依赖启动工作目录或 PATH。TOSUN 只保留显式 `TOSUN_SDK_DIR` 开发回退，不再隐式借用 TSMaster 安装目录。设备缺少 DLL 或接口时报告具体问题，不使其他可用厂商后端失效。

## 系统驱动不是 DLL

这里打包的是用户态 API 运行库。目标电脑仍须安装厂商官方 Windows 设备驱动/服务，再连接相应硬件。复制目录不会安装内核驱动、修改系统配置或完成实机总线验收。本次没有修改 T009 工程，没有运行 J-Link、烧录或自动硬件发送探针。

## 许可与本机用途

`driver/runtime-manifest.json` 记录来源和 SHA-256。PEAK 沿用工程已有二进制；TOSUN 来自本机 TSMaster；Vector 来自本机 Vector Platform Manager。文档与 `driver/licenses/PEAK-EULA.html` 保留许可来源。

本次按用户要求组成本机可运行应用。已安装或免费可下载不代表可任意对外分发。同星、Vector 和 Microsoft 运行库的外部分发权尚未核实。PEAK 的免费组件、硬件适用范围、版权及 EULA 附带等条件也仍须遵守。工程 LGPL 不重新授权厂商 DLL。

`package_release` 目标生成标有 local-use-only 的本机包，复制完整 driver 树并验证所有必需 DLL 的哈希。直接运行脚本默认拒绝未声明用途的分发：本机包使用 `--local-use-only`；对外发布须先独立完成许可审核，之后才可使用 `--redistribution-rights-reviewed`。此参数只是分发者的明确声明，不是本项目对许可已满足的保证。

## 验证方式

- `driver_runtime`：只加载 DLL 和检查导出符号，验证位数、依赖解析与相对应用路径；不调用厂商初始化、连接或发送 API。
- `driver_packaging`：检查完整依赖集、缺失传递依赖、旧布局和哈希不匹配均有明确结果。
- `vector`：使用注入式假 XL API 验证枚举、权限、通道租约和 CAN/LIN 行为，不代替真实设备验收。
- Qt 5/Qt 6 的具体构建、CTest 结果以本次最终报告为准。

Vector 适配边界和操作方式见 [Vector-Hardware.md](Vector-Hardware.md)。同星已支持能力仍见 [Tosun-Hardware.md](Tosun-Hardware.md)。
