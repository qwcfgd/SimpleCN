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


## 发布目录与构建目录

`build/Qt-GeneralController-qt5` 和 `build/Qt-GeneralController-qt6` 是开发构建目录，包含依赖源码缓存（`_deps`）、目标文件以及测试程序，不应作为程序安装包整体分发。Qt 6 构建目录中缓存较多并不代表应用运行所需空间。

配置对应 Release 预设后，执行：

```powershell
cmake --build --preset release-qt6 --target package_release
# Qt 5 使用 release-qt5
```

发布目录位于对应构建目录的 `release/QtBootloader-1.2.0-qt6`（Qt 5 为 `qt5`），同级生成 ZIP 和 SHA256 校验文件。只分发该目录或 ZIP；其中包含所需运行库、硬件 API、模拟配置、说明及 `VerifyRelease.exe` 自检程序，不包含构建缓存和整套回归测试。

脚本拒绝覆盖已有发布包，以免覆盖用户配置。需要生成另一份时，在 CMake 配置时用 `-DHOST_RELEASE_OUTPUT=<新发布目录>` 指定构建目录下的新路径，也可直接使用 `scripts/package_release.py --build <构建目录> --output <新发布目录> --qt-major 6`。在发布目录运行 `VerifyRelease.exe` 验证可启动和模拟下载。


2026-09-20 排查实测：原 Qt 6 开发构建目录约 540.93 MiB，其中 `_deps` 378.89 MiB、`tests` 86.94 MiB；Qt 5 开发构建目录约 92.71 MiB，未存放同一份依赖源码缓存。独立发布包 Qt 6 为 **64.01 MiB**（ZIP 22.21 MiB），Qt 5 为 **31.37 MiB**（ZIP 13.39 MiB）。缓存保留用于后续离线编译，发布包不收集它。

两套发布包均在仅保留 Windows 系统 PATH、清空 Qt 插件环境变量、使用外部工作目录的条件下通过 `VerifyRelease.exe`（程序启动、三通道模拟下载、配置往返及截图）；SHA256 清单核验和 `host_ui` 回归通过。同步修复了旧配置读取时向空信号配置写入 `source` 空值的问题，避免随包模拟配置触发错误导入。
