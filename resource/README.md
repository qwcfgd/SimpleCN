# 本项目独立通信资源

V1.44 将 GeneralController 所需通信源码与 SDK 资源纳入本目录。构建不再读取 Qt-ACTestController 或 communication-provider 的目录。

- `communication/`：通道配置与连接状态、PEAK 后端、DLL 加载及 LIN 调度扩展。
- `driverCan/`、`driverLin/`：CAN / LIN API 包装与驱动。
- `global/`：驱动所用公共类型。
- `dll/`：原有 PEAK API 头文件及 Windows x64 CAN / LIN DLL；部署到程序目录 `dll/`。不包含厂商驱动安装服务或私有 27 算法。

来源：Qt-ACTestController 本机 Git stash `0d1dbc4`（`codex-before-v1.62-nad-analysis-20260922`）。`communication/` 来自其未跟踪文件树 `ff68988`；驱动、公共类型和 SDK 来自 stash 跟踪树。该通信目录未曾提交到原工程主分支，未发现主分支删除提交。原 stash 和原工程工作区保持原状。

本次对恢复源码的调整仅涉及本项目目录布局和文本空白：SDK 头文件与部署 DLL 改用 resource/ 内相对路径，清理行末空白；不保留已不使用的 qmake 通信 .pri。后续修改直接在本仓库维护，历史 patches/ 文件仅用于溯源，不再作为构建步骤。

厂商 API 头文件和 DLL 保留其原有版权与使用条件，详见 [许可说明](../docs/Licensing.md)。
