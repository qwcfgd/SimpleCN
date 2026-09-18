# General Bootloader Controller

基于 Qt 的 CAN / LIN 诊断工作台，支持通道管理、报文显示、UDS 模拟下载、CDD 诊断，以及 DBC / LDF 信号编辑和发送。

此仓库不包含私有安全访问算法、真实 seed/key 向量、目标实机配置或内部验证报告。公开配置和 fixtures 仅用于模拟，不代表任何实机参数。

## 构建与验证

需要 Qt、MinGW 和另行提供的通信模块（定义 `peak_communication` 目标及 `peak_deploy` 函数）。CMake 默认自动查找同级目录 `../Qt-ACTestController/resource/communication`，其次查找 `../communication-provider/resource/communication`，无需手动设置变量：

```powershell
cmake --preset stage6-qt6
cmake --build --preset stage6-qt6
ctest --preset stage6-qt6
```

Qt 5 使用 `stage6-qt5`。本机工具链位置在 CMakePresets.json 中配置。

所有构建预设以用户指定的原工程 `C:/Documents/0_Qt/Qt-GeneralController` 为路径基准，固定输出到 `C:/Documents/0_Qt/build/Qt-GeneralController-qt5` 或 `C:/Documents/0_Qt/build/Qt-GeneralController-qt6`，可执行文件为目录中的 `QtBootloader.exe`。这些绝对路径在 Codex worktree 中也保持不变，不在仓库根目录创建 `build`。同一 Qt 版本的预设共用该目录，切换构建类型时先重新配置；原工程与 worktree 不可同时使用同一构建目录，切换源码位置前须清理旧 CMake 缓存并重新配置。

信号发送依赖固定版本的 dbcppp / Boost；首次配置需要下载依赖，也可指定本地源码目录。共享通信模块须包含本次 LIN 扩展补丁。安装、离线构建与验证命令见 [信号发送实施记录](docs/Signal-Transmission-Implementation.md)。应用运行时不依赖 Python。

通信模块放在其他位置时，在配置命令后追加 `-DCOMMUNICATION_SOURCE_DIR="<通信模块目录>"`，该目录必须直接包含模块的 `CMakeLists.txt`。Qt Creator 可在 CMake 配置中设置同名 PATH 变量；旧缓存为空时会自动探测，旧缓存指向失效目录时请清空或修正该值。

默认不构建安全访问桥接程序，也不复制算法 DLL。需要通用 32 位桥接程序时，显式设置 `BUILD_SEEDKEY_BRIDGE=ON` 和 `SEEDKEY_CXX32=<32位编译器路径>`；算法及其运行依赖由使用者从授权渠道单独提供。

公开打包脚本不收集算法目录、内部验证日志或源机器路径。旧构建产物可能含已撤下的资料，应使用清洁构建生成新包。

- [使用说明](docs/User-Guide.md)
- [CDD 自动配置与 UDS 诊断](docs/CDD-UDS.md)
- [默认配置](docs/Default-Configuration.md)
- [通信模块说明](docs/Communication-Module.md)
- [信号发送与验证范围](docs/Signal-Transmission-Implementation.md)
- [MVVM 检查与调整](docs/MVVM-Audit.md)

`private/`、`build/` 和 `dist/` 是本地目录，不应上传或直接整体分享。
