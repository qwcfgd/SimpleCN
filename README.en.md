<div align="center">

# Qt-GeneralController

**CAN / LIN signals, messages and diagnostics in one workbench**

[简体中文](README.md) · English

![Version](https://img.shields.io/badge/version-1.4.0-2563eb)
![Platform](https://img.shields.io/badge/platform-Windows_x64-475569)
![Qt](https://img.shields.io/badge/Qt-5_%7C_6-41cd52)
[![License](https://img.shields.io/badge/license-LGPL--3.0--only-blue)](LICENSE)

Developed with assistance from OpenAI Codex.

[Quick start](#quick-start) · [User guide (Chinese)](docs/User-Guide.md) · [V1.4 release notes (Chinese)](docs/Release-1.4.md)

</div>

## New in V1.4

- Switch the application UI between **简中 / Eng** beside the main title. The selection is remembered. Database names, enum labels and user input retain their original text.
- Resize and drag columns in CAN message, LIN frame and signal tables. Layouts are saved per software channel in project configuration.
- Single-click a transmit raw/physical value to edit. Enum editors accept text, provide completion and accept it with Tab. Raw values outside the enum table remain valid; undefined enum labels display `-`.
- Arrange plots within the available height without a vertical scrollbar. Enum axes and grids adapt to zoom; tick labels appear only for defined enum values. The signal list’s y column displays matching enum labels; cursor differences remain numeric.
- Display rt in milliseconds with six decimal places, including fractional-microsecond input: `1200123.674 μs → 1200.123674 ms`. Recording starts at zero.

## Features

| Area | Capabilities |
| --- | --- |
| Channels | Multiple CAN / LIN software channels, simulation and hardware modes, independent tasks, project saving and unsaved-change prompts. |
| Signal transmission | Import DBC / LDF; edit raw, physical and enum values; create messages, schedules and frames without a database; repeated and periodic transmission. |
| Message monitor | t / rt / dt columns, filtering, follow mode, in-place updates by ID, nibble change colors and decoded signal expansion. Latest 10,000 records. |
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

V1.4 Release builds and all 15 CTest suites passed with both Qt 5.15.19 and Qt 6.8.4. Tests use simulation and mock SDKs; they do not replace real ECU acceptance.

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

Most detailed documentation is currently in Chinese: [user guide](docs/User-Guide.md), [trace and graphics](docs/Trace-and-Graphics.md), [workbench and replay](docs/Signal-Workbench-Replay.md), [CDD / UDS](docs/CDD-UDS.md), [V1.4 changes](docs/Release-1.4.md), [MVVM review](docs/MVVM-Audit.md), [hardware scope](docs/Tosun-Hardware.md).

## License

Project-owned code is licensed under **LGPL-3.0-only**. See [LICENSE](LICENSE) and the referenced GPL v3 terms in [LICENSE.GPL](LICENSE.GPL). Third-party components and vendor SDKs retain their own licenses; see [licensing and third-party components](docs/Licensing.md).
