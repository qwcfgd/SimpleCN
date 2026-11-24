# 数据库导入与验证

## 兼容行为

- DBC / LDF 优先按 UTF-8 读取；Windows 下还支持严格 GB18030 解码，兼容 GBK。转换只发生在内存中，原文件保持不变；不合法的字节序列报错。
- DBC 的 `VECTOR__INDEPENDENT_SIG_MSG` 独立信号容器保留字段供浏览，不作为实际 CAN 报文收发；其他非法 CAN ID 仍拒绝。
- LDF 未引用编码中的非法无符号 raw 区间产生诊断提示，不阻断有效帧导入。如信号引用该编码，则对应帧禁止发送或解码。重复 LIN ID 保留各帧定义并提示歧义，不任意选择一个定义。
- CDD 支持有符号线性转换范围，以及由前置计数字段控制的固定字节长度 `NUMITERCOMP` 记录。重复记录用 HEX 表示，校验计数、记录宽度和数据总长度。
- CDD 最高已验证版本为 16.x。更高版本不再直接拒绝，而是用现有最高版本规则尝试解析；结构与引用校验通过时显示兼容性警告，失败则报错并保留原数据库。不会通过删改源文件版本号绕过校验。

解析和编解码位于模型层，ViewModel 传递结果，设置窗口只展示警告；不在界面层增加数据库解析逻辑。

## 本地逐文件验证

将待验收数据库放入源码根目录的 `dbc/`、`ldf/`、`cdd/`。这些目录中的真实业务数据不随源码提交。测试只在本地读取，不连接硬件、不发送报文。

先按仓库预设构建，再分别运行：

```powershell
cmake --build --preset qt5 --target signal_fixture_probe
cmake --build --preset qt6 --target signal_fixture_probe

python tests/database_import_audit.py --source . --probe ../build/qttemp/Qt-GeneralController-qt5/tests/signal_fixture_probe.exe --output ../build/qttemp/database-import-audit/qt5
python tests/database_import_audit.py --source . --probe ../build/qttemp/Qt-GeneralController-qt6/tests/signal_fixture_probe.exe --output ../build/qttemp/database-import-audit/qt6
```

工具逐个调用程序使用的原生导入器，并检查：

1. 导入成功、源文件 SHA-256 在测试前后不变。
2. DBC / LDF 的帧名及帧内信号清单与源文件一致；LDF 调度表名称一致。
3. 可执行帧使用默认值、全零及非零位模式进行编码和解码往返验证。
4. CDD 服务限定名称与源文件的有效服务定义一致。
5. 报告单独列出受限帧、调度限制和警告，不把“导入成功”等同于“所有定义都可发送”。

该独立清单比较器针对当前本地样本的普通 LDF 调度和无继承 CDD Variant；遇到不覆盖的结构会明确失败，不能用它替代通用语法解析器。Variant 继承和其他边界由原生 QtTest 合成样本回归覆盖。

完整结果保存在指定输出目录的 `summary.json` 和各文件 JSON 中，可能包含业务定义，不应提交到公开仓库。Python 仅用于开发验证，发布程序没有 Python 依赖。

## 本轮样本范围

| 类型 | 文件数 | 导入定义 |
| --- | ---: | --- |
| DBC | 5 | 77 个报文/独立信号容器，466 个信号 |
| LDF | 6 | 103 个帧，856 个帧内信号，24 张调度表 |
| CDD | 4 | 274 项诊断服务 |

Qt 5 与 Qt 6 的逐文件导入及清单结果一致。现有样本中的独立信号容器、4 个重复 ID 的 LIN 帧及一个未使用编码中的负数 raw 区间仍明确提示，不修改原始业务定义。
