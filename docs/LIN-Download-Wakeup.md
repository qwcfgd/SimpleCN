# LIN 自动下载前的唤醒

## 1.66 正式版

每次真实 LIN 自动下载开始时，控制器在占用通道后执行一次唤醒。首条正式诊断请求和它的协议定时器都在准备期结束后启动。

- **PEAK 主机**读取适配器模式、配置的 break 位数和适配器时钟。按通道波特率换算，break 必须至少 13 bit 且落在 250 µs 到 5 ms 之间。主机提交一次标准从节点响应帧头（ID 0x3D，PID 0x7D，Classic），无主机业务载荷。19.2 kbit/s 下 13 bit 约为 677 µs。
- **PEAK 回读**要求同一适配器、标准帧、正确 PID、订阅方向、长度 8、Classic，且回读的 break 起点时间晚于提交前采样。上次尚未确认的同类帧头会阻止新提交。正常响应，或硬件明确报告该时隙没有从节点数据，均可确认帧头已处理。部分响应超时、总线或校验错误导致本轮失败。
- **等待起点**为软件读到时隙结果的时刻。硬件已处理完 break、同步字节、PID 和响应时隙，因此在这个时刻再用精确定时器等待 100 ms 已覆盖从 break 结束沿算起的准备期。排队及主机读取延迟只会增加等待。适配器时间戳标记 break 的起始沿，仅用于关联回读。
- **Vector**提交一次原有的主机唤醒请求，通过同通道本机发送的 XL 唤醒事件确认。从收到事件开始保守预留 5 ms 脉宽上限，再等待 100 ms。
- **失败与取消**：未提供可确认唤醒能力的后端（包括当前 TOSUN）、提交失败、回读错误或 1 秒内未取得确认均终止本轮。取消、断开、任务替换使旧任务的延迟启动失效。CAN、手动诊断和 LIN 正常传输槽继续使用各自既有入口。

LIN 2.0 §5.1、§7.1 的 150 ms 是再次请求唤醒的门槛。实际显性宽度、结束沿到首个正式帧的总线间隔和 ECU 的就绪状态仍须由物理采集验证。工程候选的离线验证覆盖软件时序及错误路径。[LIN Protocol Specification Rev. 2.0](https://forums.ni.com/attachments/ni/30/3619/1/LIN.pdf) · [PEAK PLIN-API 手册，§3.2、§3.5、§4.4](https://www.peak-system.com/produktcd/Develop/PC%20interfaces/Windows/PLIN-API/PLINAPI_enu.pdf)

## 历史记录 · 1.56 LIN download wake-up candidate

Scope: prepare the bus once at the start of each physical LIN download round,
after the existing image checks and channel ownership checks. The FlashJob,
CAN flow, single-file 0x34 contract, 0x36 segmentation, ECU ROM/CRC and security
behavior are unchanged. Project protocol baseline remains LIN 2.0.

## Backend capability

- PEAK at the project's 19200 bps: issue one standard diagnostic slave-response
  header (raw ID 0x3D, protected PID 0x7D, Classic) using the existing owned
  LIN_Write path. It has no UDS request payload. No extra client, channel or
  schedule is made. Other bitrates retain the previous native XmtWakeUp path
  and are not qualified by this target's tests.
- Vector: optionally bind official `xlLinWakeUp(XLportHandle, XLaccess)` and
  invoke it on the existing active master port/mask. Missing export does not
  remove existing CAN/LIN enumeration or frame support.
- TOSUN: the currently packaged libTSCAN has no verified dedicated wake API.
  The application explicitly reports this and retains the existing download
  path. Its ECU must already be awake. TSMaster's separate tsapp API is not
  imported or guessed. Automatic wake-up is not qualified for this backend.

SDK wake failure stops the download before the first business request. An
unsupported optional primitive keeps the existing path and emits a notice.
The adapter's own Active/Sleep status is not treated as proof of ECU readiness.

## Timing and cancellation

On successful wake API return, the worker waits 120 ms using a nonblocking
Qt PreciseTimer, then starts the unchanged FlashJob. The first LIN header is
sent by the existing slot timer. Thus N_As and UDS P2/P2* are not consumed by
the preparation interval. There is one wake per download round, not per UDS
request or 0x36 block. Repeated download rounds prepare independently.

The delayed continuation is tied to the worker QObject, a QPointer to the
specific FlashJob, the current job identity, running state and connected
session. Cancel, disconnect, replacement, shutdown or destruction cannot
release an old queued start into a later task. No FlashIo lease is involved.

IMPORTANT: 120 ms is measured from API return, not from a measured physical
pulse ending edge. API success does not prove transmission completion or ECU
readiness. Software timing tests cannot establish this physical interval.
The pulse-end-to-first-header interval still requires hardware capture before
this wake path can be accepted. Windows timers can expire late.

## Offline checks and pending hardware acceptance

Qt5/Qt6 use the same source. Fake SDK tests cover current PEAK client/hardware,
closed/slave rejection and native errors; Vector port/mask, mode, native errors
and missing export; TOSUN retains existing role/frame behavior while reporting
unsupported wake. Injected fake LIN hardware tests cover no early first frame,
no premature protocol timeout, cancel, disconnect, task replacement and native
failure versus unsupported capability. Existing CAN/LIN protocol regressions
remain applicable. Results and exact binary/source hashes are in the task's
`lin-wakeup-r1` evidence folder; no hardware opt-in test is enabled.

Pending hardware procedure, only after the user's detached cold-start
confirmation: confirm actual ECU sleep after bus inactivity, start the normal
download directly without a sacrificial F189 or unrelated application header,
capture wake dominant duration and pulse end to first 0x3C header, and verify
the first 0x1001 response. Repeat for App and Boot entry plus the existing
27/0203/1002/full A/B CAN/LIN regression, protecting fresh nonce/state.
The prior four successful downloads used a separate wake precondition and
therefore do not qualify this new host start path. Version 1.55's native-only
PEAK pulse returned SDK success but its first 1001 had no ECU response; CAN
1001 was positive. Separate D6-header/F189 and standard-3D-header/F189 controls
were positive. These observations support the narrow header preparation
candidate but do not establish native pulse shape or a unique hardware cause.
The product path has no sacrificial F189 and still begins its UDS flow at 1001.

## References

- Local PEAK PLIN API manual: section 3.2.2 (ordinary master frames can wake
  the bus), section 4.4.34 (LIN_XmtWakeUp).
- Vector official XL Driver Library manual: xlLinWakeUp.
- LIN 2.2A section 2.6.2 is a cross-check only; it does not upgrade the project's
  LIN 2.0 acceptance baseline. Final physical limits use the target baseline
  and actual capture, not a host API timestamp alone.

## 1.57：发送显示与NAD入口

下载设置的LIN参数复用通道NAD，范围01–7D；不改变默认01或其他目标的已保存值。真实诊断3C的有效硬件回读始终显示Tx；软件请求历史仍标识TxPending，SDK写成功不代表实际发送。诊断错误记录保持可见，CAN和信号发送的既有RxD策略保持。

未发布1.57方向修订：3C发送确认只接受publisher方向、8字节的标准接收事件，再沿用pending和完整payload匹配；匹配的硬件错误仍走原失败路径。subscriber同数据不确认，短帧不确认并显示错误。监视Tx表示有效publisher实际事件，独立于当前是否有匹配请求；错payload或取消后迟到的有效publisher事件可以记Tx，但不能推进当前请求。没有新增硬件操作，也不把这处缺陷认定为本次用户下载失败根因。

未发布1.57 TIMEOUT修订：按PLIN手册§3.5.1–3.5.2，仅subscriber/auto的纯SLAVE_NOT_RESPONDING作为无数据静默；TIMEOUT及其组合均作为不完整帧错误，不能在抑制正响应时当成功。TIMEOUT中的Checksum是实际收/发字节数，现有监视记录按此数保留前缀，并将校验和标为不可用；publisher失败匹配仍用原完整Data，未改pending匹配。Vector/TOSUN现有generic错误映射保持，未推测新错误码。以上为离线修复，未部署或证明本次App失败根因。
