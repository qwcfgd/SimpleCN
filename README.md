# Qt-GeneralController V1.3

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

所有预设以原工程 `C:/Documents/0_Qt/Qt-GeneralController` 为固定路径基准，worktree 不改变输出位置：

- `../build/Qt-GeneralController-qt5`、`../build/Qt-GeneralController-qt6`：仅主程序 `QtBootloader.exe` 和必要运行库、插件、硬件 API。
- `../build/qttemp/Qt-GeneralController-qt5`、`../build/qttemp/Qt-GeneralController-qt6`：CMake 缓存、依赖源码、中间文件、测试/调试工具、测试素材、截图与日志。

默认构建类型为 Release。Qt 5 / Qt 6 完全分离，不在仓库根目录创建 build。原工程与 worktree 不应同时使用同一 CMake 目录；切换源码位置需重新配置。通过 `-DBUILD_TESTING=OFF` 可只构建主程序。自定义配置时使用 `HOST_RUNTIME_OUTPUT_DIR` 指定程序目录，并把 CMake `-B` 指向 `../build/qttemp` 内的独立目录。

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


## 发布与验证

```powershell
cmake --preset release-qt6
cmake --build --preset release-qt6
ctest --preset release-qt6
cmake --build --preset release-qt6 --target package_release
# Qt 5 使用 release-qt5
```

`package_release` 在相应 `qttemp/Qt-GeneralController-qtN/release/QtBootloader-1.3.0-qtN` 中生成发布副本，并在同级生成 ZIP 和 SHA256。发布包包含应用、运行依赖、使用说明和许可证；不含测试 EXE、Qt Test、模拟测试素材、缓存或内部日志。已有发布包不会被覆盖，需用 `-DHOST_RELEASE_OUTPUT=<新目录>` 选择新路径。

`VerifyRelease.exe` 位于 `qttemp/Qt-GeneralController-qtN/tests`，由 CTest 的 `release_runtime` 项调用，测试输出也留在该目录。可通过 `HOST_RELEASE_DIR` 环境变量选择另一份发布目录进行启动验证；模拟下载素材和测试配置始终由测试目录提供。

## 1.3 主要更新

- 报文监视增加 t / rt / dt 显隐、按 ID 更新、半字节变化着色及数据库信号展开。
- 支持 ASC / BLF 导出、通道映射回放、报文覆盖及日志外报文追加发送。
- 信号发送支持自定义 CAN 报文、LIN 调度与帧、ID 编辑、次数发送/调度和项目配置保存。
- 图像观测支持多轴、分组、光标差值、枚举刻度、缩放与保形抽样。
- 适配同星 TC1016 / TC1016P 经典 CAN 和 LIN；修复停止失败、日志重复、撤销与配置恢复等问题。
- 运行文件与全部测试、调试、构建内容分离。

详细变更与验证范围见 [1.3 发布说明](docs/Release-1.3.md)。
