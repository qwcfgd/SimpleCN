# SimpleCN 1.48

- 下载文件表支持 Driver、Boot 和 App；必须选择 Driver 以及 Boot 或 App。
- 下载步骤仍可独立选择。
- 双 Boot 包包含完整 A、B 槽镜像。A 槽 RequestDownload 收到精确响应 `7F 34 70` 时回退到 B 槽；启用对应步骤时，Boot 校验后通过 `31 01 FF 02` 提交。

Qt 5、Qt 6 的 configure/build 均退出 0。合并最终六项回归结果，host_ui、can_protocol、release_runtime、download_revision、t008_target 通过；protocol 为 54 pass、1 fail，唯一失败为既有 eraseRangeValidationBeforeSend。该负例检查零长度、32 位溢出及镜像范围覆盖；本版保持既有下载行为，未新增范围检查，不能宣称全部回归通过。

R4 两套 download_revision 各 39 pass，t008_target 各 200 pass、5 skip。5 项 skip 为 copiedConfigurationPreservesAddresses、actualFirmwareFiles、actualBinHexEquivalence、flashGeometry(actual-section)、actualGoldenUiReview，相应 copied 配置、旧格式或 golden 输入未提供；当前双 Boot、App、Driver 真实文件夹具及模拟 worker 已通过。

R3 host_ui 两套各 48 pass、2 skip（Candidate config not supplied；Opt-in physical LIN test）；can_protocol 各 78 pass、1 XFAIL（相同请求的旧响应缺少事务标识，无法区分）。本轮未执行物理 CAN/LIN 下载或新增 ECU 实板验收。
