# Device API runtimes

This directory is copied into the executable directory as `driver/` by CMake on every application/test build, even when only a DLL changed. Keep the folder beside SimpleCN.exe. Runtime selection follows the application architecture, not the operating system architecture.

- `peak/x64`: optional local copies of PCANBasic.dll and PLinApi.dll. If absent, the build copies the matching PEAK DLLs already tracked under `resource/dll` into the same runtime directory.
- `tosun/x64`: libTSCAN.dll, libTSH.dll, binlog.dll, libLog.dll, msvcp140.dll, vcruntime140.dll, vcruntime140_1.dll. The vendor-documented support libraries are retained even when not present in the main PE import table.
- `vector/x64`: vxlapi64.dll from the installed Vector Platform Manager. 32-bit builds use `vector/x86/vxlapi.dll`.
- `*/x86`: placeholders only. Supply official matching 32-bit DLLs before a 32-bit build; never rename a 64-bit DLL to impersonate a 32-bit one.

The loaders resolve absolute paths below the application directory and do not depend on the current working directory, PATH, TSMaster installation directory or the obsolete `dll/` layout. TOSUN_SDK_DIR is an explicit developer fallback only. Optional backends fail with a useful message if their runtime is missing; this does not prevent other vendors from being used.

## Windows device drivers

These files are user-mode API libraries, not Windows PnP/kernel drivers. Install the matching official PEAK, TOSUN or Vector device-driver package normally on each target PC. Copying DLLs does not install a driver/service, grant hardware access or verify real CAN/LIN operation. No installer, driver registration or security setting is changed by this project.

## Provenance and redistribution

`runtime-manifest.json` records the local file source and SHA-256 for local packaging. Keep local SDK binaries and their provenance manifest outside published source changes. Installation or free download does not grant a right to redistribute. The project LGPL does not relicense these files.

- PEAK conditional EULA: https://www.peak-system.com/support/eula/ . Preserve terms/notices, distribute the PEAK software component without a charge, and use only PEAK/authorized OEM hardware. Retain the included EULA with any permitted redistribution.
- TOSUN official SDK deployment guide: https://www.tosunai.com/wp-content/uploads/2024/07/libTSCAN_SDK_CPP_cn.pdf . It documents copying the four vendor libraries. An external redistribution grant has not been verified here.
- Vector XL API and Microsoft VC runtime redistribution rights must be checked against the licenses supplied to the distributor. They have not been verified here.

Public/customer distribution is blocked by the packaging script until the distributor supplies reviewed rights. The CMake package_release target creates a clearly labelled local-use package, not an assertion of redistribution permission. The copied Microsoft runtimes still require Windows UCRT; never copy Windows system DLLs into this tree.
