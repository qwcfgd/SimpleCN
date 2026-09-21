# 许可与第三方组件

## 项目许可

Qt-GeneralController 项目自有代码采用 **GNU Lesser General Public License v3.0 only**，SPDX 标识为 `LGPL-3.0-only`。该选择延续仓库已经包含的 LGPL v3 许可文本，允许使用、修改和再分发，同时保留相应的许可、源码及修改分发义务。具体条件以许可原文为准。

- [LICENSE](../LICENSE)：完整、未修改的 LGPL v3 原文。
- [LICENSE.GPL](../LICENSE.GPL)：LGPL v3 引用的 GPL v3 原文，来自 [GNU 官方文本](https://www.gnu.org/licenses/gpl-3.0.txt)。
- [LICENSE.LGPL](../LICENSE.LGPL)：保留已有文件，与 LICENSE 内容一致。

现有第三方版权声明和许可证优先适用于相应文件。本项目许可不授予外部通信模块、硬件 SDK、私有算法或第三方运行库的额外权利，也不代表已满足任意二进制组合的全部再分发条件。

## 第三方组件

| 组件 | 使用方式 | 许可与来源 |
| --- | --- | --- |
| Qt | Qt Core / Gui / Widgets / Concurrent 等；运行时动态链接，Qt Test 仅用于测试 | 依据实际版本、模块及取得方式适用 LGPL / GPL 或商业许可，不能统一视为本项目 LGPL 代码。[Qt 官方许可说明](https://doc.qt.io/qt-6/licensing.html)、[LGPL 使用义务](https://www.qt.io/development/open-source-lgpl-obligations)。 |
| dbcppp | 固定提交的 DBC 解析库 | MIT；保留 [许可与版权声明](licenses/dbcppp-MIT.txt)。 |
| Boost.Multiprecision / Boost.Math 及所需 Boost 头文件 | 编译期高精度运算依赖，不单独部署 Boost Math DLL | Boost Software License 1.0；见 [许可文本](licenses/Boost-1.0.txt)。 |
| MinGW / GCC 运行库及 winpthreads | 程序运行依赖 | 按工具链所含各组件许可及适用的运行库例外条款分发；不由本项目重新授权。 |
| 外部 peak_communication 模块 | 从独立提供的源码目录编译 | 按该模块源码及其第三方文件的原有许可使用；本仓库 LICENSE 不替代外部模块许可。 |
| PEAK PCANBasic / PLIN API、同星 TSMaster / libTSCAN | 厂商硬件接口与动态运行库 | 按厂商许可使用和再分发；不是本项目 LGPL 代码。同星 SDK 由用户另行安装。 |

BLF / 图像功能的开源调研参考项目见 [实现说明](Trace-and-Graphics.md)。参考资料不等于链接依赖，也不将其许可移植到本项目；例如 vector_blf 未链接或复制进项目。

## 分发范围

打包脚本携带项目 LICENSE、LICENSE.GPL、本说明及已有第三方许可文本。分发者仍需依据实际采用的 Qt、编译器运行库和厂商 SDK，提供对应版本要求的声明、源码获取方式等材料；本清单不是对所有二进制分发条件已满足的认证。

公开包不包含私有安全访问算法、真实 seed/key 向量、目标 ECU 配置或测试固件。测试与调试内容保留在 `../build/qttemp`。
