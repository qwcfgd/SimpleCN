<div align="center">

# SimpleCN

**CAN / LIN signals, messages and diagnostics in one workbench**

[简体中文](README.md) · English · [GitHub: SimpleCN](https://github.com/qwcfgd/SimpleCN)

![Version](https://img.shields.io/badge/version-1.4.3-2563eb)
![Platform](https://img.shields.io/badge/platform-Windows_x64-475569)
![Qt](https://img.shields.io/badge/Qt-5_%7C_6-41cd52)
[![License](https://img.shields.io/badge/license-LGPL--3.0--only-blue)](LICENSE)

Developed with assistance from OpenAI Codex.

[Quick start](#quick-start) · [User guide (Chinese)](docs/User-Guide.md) · [V1.4.3 release notes (Chinese)](docs/Release-1.4.3.md)

</div>

## V1.4.3: SimpleCN and message monitor updates

The window title is SimpleCN. The home page displays SimpleCN V1.4.3 and SIMPLE CONTROLLER FOR CAN/LIN.

- Relative and delta time now use seconds, with trailing fractional zeros omitted.
- Keep and export all frames since startup or the last clear. Only the table window is limited to 10,000 records.
- Pause updates defaults to off. While paused, recording continues and a left history scrollbar can load any window from the full cache. Resume returns to current data. New controls, counters and tooltips support 简中 / Eng. See the [monitor guide](docs/Trace-and-Graphics.md).
- All 16 suites ultimately passed on each Qt version, including complete exports of over 30,000 records, paused browsing and bilingual UI checks. Physical hardware testing was omitted for this follow-up at the user's request.

## New in V1.4.2: native Windows file dialogs

- Use modern native Windows open/save dialogs for firmware, DBC / LDF, CDD, replay and channel configuration imports, and message / runtime log exports.
- Remove the V1.4.1 custom top address bar. Use the system address bar and file-name field, retaining initial paths, filters, cancellation and file processing.
- Application-provided dialog titles and the rest of the UI retain 简中 / Eng support. Windows controls the language of native buttons, navigation and system prompts.
- The current follow-up uses seconds as requested: `1000000 μs → 1 s`, `1001250 μs → 1.00125 s`. Significant fractional precision is retained.

## Fixes in V1.4.1

- Preserve in-progress raw, physical and enum edits during transmission, incoming frames, status updates and language changes; committed values update subsequent payloads.
- Keep physical coordinates for enum signals. Defined positions show enum labels; other ticks show integers, with matching grid lines.
- Unify all three work-area backgrounds. The custom address bar introduced in this version was removed in V1.4.2 in favor of native Windows path input.
- Clarify rt milliseconds: one second corresponds to about 1000 ms. V1.4.1 hardware acceptance was pending when the device was removed; V1.4.2 also removes trailing fractional zeros.

## New in V1.4

- Switch the application UI between **简中 / Eng** beside the main title. The selection is remembered. Database names, enum labels and user input retain their original text.
- Resize and drag columns in CAN message, LIN frame and signal tables. Layouts are saved per software channel in project configuration.
- Single-click a transmit raw/physical value to edit. Enum editors accept text, provide completion and accept it with Tab. Raw values outside the enum table remain valid; undefined enum labels display `-`.
- Arrange plots within the available height without a vertical scrollbar. Enum axes and grids adapt to zoom; tick labels appear only for defined enum values. The signal list’s y column displays matching enum labels; cursor differences remain numeric.
- Display rt in seconds, omitting trailing fractional zeros, including fractional-microsecond input: `1200123.674 μs → 1.200123674 s`. Recording starts at zero.

## Features

| Area | Capabilities |
| --- | --- |
| Channels | Multiple CAN / LIN software channels, simulation and hardware modes, independent tasks, project saving and unsaved-change prompts. |
| Signal transmission | Import DBC / LDF; edit raw, physical and enum values; create messages, schedules and frames without a database; repeated and periodic transmission. |
| Message monitor | t / rt / dt columns, filtering, follow mode, in-place updates by ID, nibble change colors and decoded signal expansion. Complete session cache, pause and history browsing; up to 10,000 records per display window. |
| Logs and replay | Export ASC / BLF / CSV; replay ASC / BLF with channel mapping, enabled-message overrides and additional messages. |
| Graphics | Signal groups, drag ordering, All / Marked / GrayNoMarked, Fit / arrange / All axes, cursor differences, grids and zoom. Up to 1,000 rendered points per signal, resampled for the viewport. Gaps longer than 5 seconds are not connected. |
| UDS and download | Import CDD, select ECU / variant, generate service requests and parameters, simulated CAN / LIN download, configurable online LIN download and external security-access integration. |

DBC / LDF support UTF-8 and Windows GBK / GB18030. CDD has been verified through CANdela 16.x. Newer versions are attempted using the available parser, with a warning on success and an error on failure. See [database validation](docs/Database-Import.md).

## Screenshots

These V1.3 screenshots use simulated channels and synthetic signals; they are not evidence of real ECU testing.

![Signal workbench](docs/screenshots/signal-workbench.png)
![Graphics and dual cursors](docs/screenshots/signal-plot.png)
![Message monitor](docs/screenshots/message-monitor.png)

## Hardware and scope

- **Simulation:** no hardware required; replay runs as fast as possible and retains Sim Tx / Sim Rx directions.
- **PEAK:** PCAN / PLIN backends require compatible vendor drivers and APIs.
- **TOSUN:** classic CAN and LIN for TC1016 / TC1016P with TSMaster / libTSCAN runtimes. CAN1 / CAN2 external loopback was verified previously; real LIN ECU acceptance remains outstanding.
- **Hardware replay:** original 1× timing, manually mapped channels, excluding only IDs explicitly published by the current database node.
- **CAN FD:** supported in relevant log handling and simulated replay, but real CAN FD transmission/reception is not enabled.
- **ECU download:** CAN download currently supports simulation only. Online LIN download requires target-specific settings and an authorized security-access implementation.

V1.4.2 Release builds and all 16 CTest suites ultimately passed with both Qt 5.15.19 and Qt 6.8.4. Native open/save dialogs, Unicode/space paths and cancellation were checked on the Windows desktop. Both builds received real frames on TOSUN CAN1 at 500 kbit/s and verified the initial millisecond rt values without trailing fractional zeros; the current follow-up switches display to seconds as requested. This release did not validate live transmit editing, LIN ECU traffic or physical downloads. See the [release notes](docs/Release-1.4.2.md) for details and reruns.

## Quick start

1. Run `QtBootloader.exe`. CAN01 and LIN01 are created initially without automatic hardware connection.
2. Select simulation or installed hardware, configure the channel and bitrate, then connect.
3. Import a DBC / LDF on the signal transmission page, select a message or schedule, edit values and enable entries.
4. Use repeated or periodic transmission. Open Graphics to add database signals.
5. To replay, import an ASC / BLF file in communication settings and map its channels. Save project configuration to persist the setup.

Python is not required at runtime. Public fixtures and profiles are for simulation. Private security algorithms, real seed/key vectors and target ECU configurations are not included.

## Build from source

Windows x64, CMake / Ninja, matching Qt / MinGW, and an external communication module providing `peak_communication` and `peak_deploy` are required. CMake searches these locations in order:

```text
../Qt-ACTestController/resource/communication
../communication-provider/resource/communication
```

Set `-DCOMMUNICATION_SOURCE_DIR=<module-directory>` if necessary. The module must include the LIN extensions described in [communication module documentation](docs/Communication-Module.md).

```powershell
cmake --preset qt6
cmake --build --preset qt6
ctest --preset qt6
# Use qt5 for Qt 5.
```

Presets use Release builds and fixed toolchain/output paths based on `C:/Documents/0_Qt/Qt-GeneralController`. Adjust paths for another machine. First configuration fetches pinned dbcppp / Boost versions; see [build documentation](docs/Signal-Transmission-Implementation.md) for offline dependencies.

```text
../build/
├── Qt-GeneralController-qt5/     # Application and runtime dependencies
├── Qt-GeneralController-qt6/     # Application and runtime dependencies
└── qttemp/
    ├── Qt-GeneralController-qt5/ # Build cache, tests, debug tools and logs
    └── Qt-GeneralController-qt6/ # Separate Qt 6 build and test output
```

Paths remain fixed across worktrees. Do not share a build directory between simultaneous checkouts or create build output in the source directory. Use `-DBUILD_TESTING=OFF` for application-only builds; `HOST_RUNTIME_OUTPUT_DIR` controls runtime deployment, while CMake `-B` belongs under `../build/qttemp`.

Keep required Qt / MinGW DLLs, plugins and hardware APIs with the executable. The optional 32-bit security bridge requires `BUILD_SEEDKEY_BRIDGE=ON` and `SEEDKEY_CXX32`; users supply their own algorithm.

### Packaging

```powershell
cmake --build --preset qt6 --target package_release
ctest --preset qt6 -R release_runtime
```

Release copies, ZIPs and SHA256 files are written under `qttemp/Qt-GeneralController-qtN/release/`. Existing release directories are not overwritten; set `HOST_RELEASE_OUTPUT` to a new path. `VerifyRelease.exe` stays in the tests directory; `HOST_RELEASE_DIR` selects an alternative release copy for verification.

## Documentation

Most detailed documentation is currently in Chinese: [user guide](docs/User-Guide.md), [trace and graphics](docs/Trace-and-Graphics.md), [workbench and replay](docs/Signal-Workbench-Replay.md), [CDD / UDS](docs/CDD-UDS.md), [V1.4.2 changes](docs/Release-1.4.2.md), [MVVM review](docs/MVVM-Audit.md), [hardware scope](docs/Tosun-Hardware.md).

## License

Project-owned code is licensed under **LGPL-3.0-only**. See [LICENSE](LICENSE) and the referenced GPL v3 terms in [LICENSE.GPL](LICENSE.GPL). Third-party components and vendor SDKs retain their own licenses; see [licensing and third-party components](docs/Licensing.md).
