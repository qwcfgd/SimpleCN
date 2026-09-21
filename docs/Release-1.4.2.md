# Qt-GeneralController V1.4.2

> 后续修订：用户确认原来的毫秒显示正确，最新要求改为秒。当前主线的时刻/s、绝对时间/s、完整会话缓存、暂停历史浏览及回归结果见 [报文监视](Trace-and-Graphics.md)。后续修订按用户要求不进行实机测试。下文记录初次 V1.4.2 的变更及当时验证数据。

GitHub 仓库已更名为 [SimpleCN](https://github.com/qwcfgd/SimpleCN)。本次保持应用名称及固定工程 / 构建路径不变。

## 文件选择

- 镜像 BIN / HEX、数据库 DBC / LDF、CDD、ASC / BLF 回放、JSON 通道配置载入，以及报文 BLF / ASC / CSV 和运行日志 TXT 导出，均通过 Qt 的 Windows 现代原生文件对话框。
- 删除 V1.4.1 的 PathFileDialog 及额外顶部路径栏、转到按钮和 Ctrl+L 定位逻辑，移除 main 和 UiLanguageController 中禁用原生对话框的全局属性。
- 保留初始路径、扩展名筛选、导出选定类型、取消返回空路径的行为，以及原有导入 / 导出与文件处理逻辑。路径输入使用系统地址栏和文件名输入框。
- 应用提供的任务标题继续随简中 / Eng 切换；原生控件、导航、错误和覆盖提示语言由 Windows 决定。

## 报文监视时刻

时刻（rt）以毫秒显示，从首条记录起算；省略小数部分末尾无效的 0，保留整数末尾的 0 和有效小数精度。例如 `1000000 μs → 1000 ms`、`1001250 μs → 1001.25 ms`、`1200123.674 μs → 1200.123674 ms`。CSV 的 rt 列保持与监视显示一致，ASC / BLF、采集、回放和协议时间单位不变。

代码核查确认已有微秒到毫秒的除以 1000 换算。本次不再额外缩小数值；修正固定补齐六位小数的表现，并用实际经过时间核对真实接收结果。

## 验证

- Qt 5.15.19 与 Qt 6.8.4 Release 构建成功；每套 16 项 CTest 最终均通过。Qt 5 host_ui 在首次整套运行时达到 60 秒超时限制，独立重跑 43.36 秒通过；Qt 6 trace_plot 更新一处旧的六位小数期望值后通过，其余 15 项已通过。
- 两套 Windows 桌面探针均验证现代原生打开、保存、中文与空格完整路径、筛选器返回，以及打开 / 保存的取消返回空路径；留存原生窗口截图。探针以 DirectUIHWND 确认系统对话框，并通过限定到测试进程的控件句柄操作，不使用全局按键。
- 两套界面均使用同星 CAN1、500 kbit/s 接收已有真实总线流量，未主动发送测试帧，结束后正常断开：

| 构建 | 真实接收记录 | 经过时间/ms | rt 增量/ms | 采集增量/μs |
| --- | ---: | ---: | ---: | ---: |
| Qt 5 | 66 | 3066 | 2989.588 | 2989588 |
| Qt 6 | 66 | 3002 | 2989.029 | 2989029 |

rt 增量统计到最后一条报文，因报文间隔和轮询不要求与墙钟完全相等；结果与毫秒单位一致。真实监视截图和逐条格式核验均确认没有无效小数尾零。本次不作为在线发送中修改负载、真实 LIN ECU 收发或实机下载的验收。

- MVVM 边界检查通过。版本资源、窗口版本、打包元数据及中英文 README 同步为 1.4.2。

## 产物与复验

程序与必要运行依赖仍放在 `C:/Documents/0_Qt/build/Qt-GeneralController-qt5` 和 `Qt-GeneralController-qt6`；构建、测试、截图、日志、探针与发布包放在 `C:/Documents/0_Qt/build/qttemp/Qt-GeneralController-qt5` 和 `Qt-GeneralController-qt6` 下，使用仓库 CMakePresets.json。

自动回归不会打开阻塞性原生弹窗。桌面核查需显式执行 `scripts/check_native_dialogs.ps1`，传入对应 `tests/native_file_dialog_probe.exe` 和一个全新的 qttemp 输出目录；使用 Windows PowerShell。硬件接收核查工具 `tests/trace_receive_probe.exe --can-monitor <hardware-key> <bitrate> <artifact-dir>` 只在操作者明确指定硬件后运行，不属于 CTest。
