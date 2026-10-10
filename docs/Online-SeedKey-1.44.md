# V1.44 在线 seed/key 修订（2026-10-10）

## 行为

用户要求真实硬件下载未配置 27 DLL 时将 seed 原样作为 key，因此默认提供者改为 seed-as-key。界面预检查与 worker 两处移除空 DLL 路径阻断；空白路径也视为未配置。旧 external-generatekeyex 配置中的空路径无需重新选择算法即可使用。

收到正响应 `67 <seed子功能> <seed>` 后，发送 `27 <seed子功能+1> <seed>`。seed 字节的长度、顺序、00、80、FF 等值原样保留，不做异或、补零、分块对齐或字节序转换。APP 和 Boot 的安全访问使用相同规则。空 seed 报错，全零 seed 仍表示已解锁；key 步骤开启、seed 步骤关闭时停止，避免此前无 DLL 分支生成 00 key。

DLL 路径非空时使用 PluginKey / GenerateKeyEx，仍检查文件和桥接程序，调用失败不回退 seed-as-key。模拟通道继续使用模拟 XOR fixture，CAN 在线下载目标适配限制保持。

## 缺失 EXE

本次检查发现两套程序目录均未部署 seedkey/SeedkeyBridge32.exe。此前 CMake 的 BUILD_SEEDKEY_BRIDGE 默认关闭，本机预设也没有开启；打包脚本亦未复制可选桥接程序。现两套本机预设启用构建并指定现有 32 位 MinGW 编译器，host_deploy 将程序与测试需要的桥接程序分别部署到对应输出目录，打包脚本携带已构建桥接程序。

未配置 DLL 的下载不依赖这个 EXE；它仅用于已配置 DLL 的分支。桥接源码不含算法，调用使用者提供的 32 位 GenerateKeyEx DLL。测试算法只编入 qttemp，不部署到应用目录或公开包。

## 分层

SeedAsKey 位于协议服务层；makeDownloadKeyProvider 负责模拟 / 原样 seed / DLL 提供者选择，worker 组织调用。ViewModel 检查已配置的外部依赖，View 只收集路径并显示说明。未增加界面对 SDK 或 worker 的直接访问。

## 验证

Qt 5.15.19 / GCC 8.1.0 和 Qt 6.8.4 / GCC 14.2.0 Release 构建完成，各 16 个 CTest 套件最终均通过。Qt 5 完整回归一次 16/16；Qt 6 初轮新增协议测试未等发送确认就注入响应，修正该测试时序并显式开启所验证的 key NRC 判定后，protocol 完整重跑通过，其他 15 个套件已通过。未更改原有步骤负响应/超时判定策略。

- 在线状态注入与命令观测确认空 DLL 路径可启动，新默认与旧 external-generatekeyex 元数据均覆盖；已填写但无效的路径仍在通知 worker 前拒绝。
- 协议测试确认 APP 27 02、Boot 27 12 等请求保留完整 seed；覆盖 1 字节、含 00/80/FF 的 7 字节、17 字节、全零 seed、空 seed、禁用 seed 却启用 key，以及开启 NRC 判定时的 35 拒绝。
- 模拟 LIN / CAN 下载、恢复连接顺序、UI、语言、线程与协议原有回归通过。DLL 模式用专用合成 32 位 GenerateKeyEx fixture 验证实际进程/ABI 调用、中文带空格路径及加载失败；fixture 不部署到程序目录。
- 两套应用目录的 SeedkeyBridge32.exe 均验证为 PE32 / i386，且不依赖 Qt 运行库即可启动。Qt 6 公开包的 EXE、桥接程序启动和 SHA256 清单核查通过，包中未携带测试 DLL 或模拟素材。
- scripts/check_mvvm_boundaries.py 与 git diff --check 通过。

日志保存在对应 Qt 的 C:/Documents/0_Qt/build/qttemp/Qt-GeneralController-qtN 内，文件名以 seed-as-key 开头。Qt 6 协议最终结果为 seed-as-key-protocol-final.log；其余回归在 seed-as-key-ctest.log。应用与桥接程序已部署到约定的 Qt 5 / Qt 6 程序目录，版本保持 1.44。

本次未执行真实硬件下载；模拟响应、命令观测和桥接 ABI fixture 不能代替实际 ECU 下载验收。
