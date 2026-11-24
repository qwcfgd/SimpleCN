"""Build a relocatable public release without private algorithms from an already deployed CMake build."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import zipfile

ROOT = Path(__file__).resolve().parents[1]

DRIVER_FILES = {
    "peak": ("PCANBasic.dll", "PLinApi.dll"),
    "tosun": ("libTSCAN.dll", "libTSH.dll", "binlog.dll", "libLog.dll",
              "msvcp140.dll", "vcruntime140.dll", "vcruntime140_1.dll"),
    "vector": ("vxlapi64.dll",),
}

def validate_driver_runtime(build):
    driver = Path(build)/"driver"
    manifest_path = driver/"runtime-manifest.json"
    if not manifest_path.is_file():
        raise ValueError(f"Missing runtime provenance manifest: {manifest_path}")
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    recorded = {item["path"]: item for item in manifest["libraries"]}
    for vendor, names in DRIVER_FILES.items():
        for name in names:
            relative = f"{vendor}/x64/{name}"
            path = driver/relative
            if not path.is_file():
                raise ValueError(f"Missing required device runtime: {path}")
            entry = recorded.get(relative)
            if not entry or entry.get("sha256") != hashlib.sha256(path.read_bytes()).hexdigest():
                raise ValueError(f"Runtime provenance/hash mismatch: {relative}; review the supplied DLL and update driver/runtime-manifest.json")
    return manifest

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--qt-major", choices=["5", "6"], required=True)
    permission = parser.add_mutually_exclusive_group()
    permission.add_argument("--local-use-only", action="store_true",
        help="Package the user's installed runtimes for local use; no redistribution rights are asserted")
    permission.add_argument("--redistribution-rights-reviewed", action="store_true",
        help="Distributor confirms vendor and Microsoft runtime redistribution rights have been independently reviewed")
    args = parser.parse_args()
    if not (args.local_use_only or args.redistribution_rights_reviewed):
        parser.error("Vendor/Microsoft runtime redistribution rights are not verified. Use --local-use-only for the local build, or review all applicable rights before --redistribution-rights-reviewed.")
    build, output = args.build.resolve(), args.output.resolve()
    archive = output.parent/(output.name+".zip")
    if output.exists() or archive.exists():
        parser.error("Output directory or archive already exists; choose a new output name.")
    required = [build/"SimpleCN.exe", build/"platforms/qwindows.dll",
                ROOT/"docs/User-Guide.md", build/"libsignal_dbcppp.dll",
                ROOT/"LICENSE", ROOT/"LICENSE.GPL", ROOT/"docs/Licensing.md",
                ROOT/"docs/licenses/dbcppp-MIT.txt", ROOT/"docs/licenses/Boost-1.0.txt"]
    for path in required:
        if not path.is_file():
            parser.error(f"Missing required file: {path}")
    try:
        driver_manifest = validate_driver_runtime(build)
    except (ValueError, KeyError, TypeError) as error:
        parser.error(str(error))
    output.mkdir(parents=True)
    shutil.copy2(required[0], output)
    for name in ("LICENSE", "LICENSE.GPL"):
        shutil.copy2(ROOT/name, output/name)
    for path in build.glob("*.dll"):
        if path.name != f"Qt{args.qt_major}Test.dll" and (path.name.startswith((f"Qt{args.qt_major}", "libgcc_", "libstdc++", "libwinpthread", "libsignal_dbcppp")) or path.name.lower() in {"d3dcompiler_47.dll", "opengl32sw.dll", "dxcompiler.dll", "dxil.dll"}):
            shutil.copy2(path, output)
    for name in ("generic", "iconengines", "imageformats",
                 "networkinformation", "platforms", "styles", "tls"):
        if (build/name).is_dir():
            shutil.copytree(build/name, output/name)
    shutil.copytree(build/"driver", output/"driver")
    bridge=build/"seedkey/SeedkeyBridge32.exe"
    if bridge.is_file():
        (output/"seedkey").mkdir()
        shutil.copy2(bridge, output/"seedkey/SeedkeyBridge32.exe")
    (output/"qt.conf").write_text("[Paths]\nPrefix=.\nPlugins=.\n", encoding="utf-8")
    (output/"docs").mkdir()
    for name in ("Download-Slots.md", "Release-1.50.md", "User-Guide.md", "Driver-Packaging.md", "Vector-Hardware.md", "Release-1.45.md", "Release-1.44.md", "Tosun-Hardware.md", "Trace-and-Graphics.md", "Signal-Workbench-Replay.md",
                 "CDD-UDS.md", "Default-Configuration.md", "Licensing.md", "Signal-Transmission-Implementation.md", "Logic-Review-Fixes.md",
                 "Online-SeedKey-1.44.md", "Startup-Review-1.44.md", "MVVM-Audit.md"):
        shutil.copy2(ROOT/"docs"/name, output/"docs"/name)
    shutil.copytree(ROOT/"docs/licenses", output/"docs/licenses")
    (output/"README.txt").write_text(
        ("LOCAL USE ONLY: vendor/Microsoft runtime redistribution rights have not been verified.\n\n" if args.local_use_only else "") +
        "SimpleCN V1.50\n\n"
        "启动 SimpleCN.exe。首次包含 CAN01 和 LIN01，不自动连接。\n"
        "完整说明：docs/User-Guide.md\n"
        "发布包仅包含应用及运行依赖；测试、调试与模拟素材保存在 build/qttemp。\n"
        "未配置 27 DLL 时，在线 LIN 将收到的 seed 原样作为 key；配置 DLL 时使用外部算法及 32 位桥接程序。\n"
        "公开包不包含私有安全访问算法。\n"
        "请保留整个目录；不要只复制 exe。保存配置需要目录可写。\n",
        encoding="utf-8-sig")
    inventory = {
        "releaseStatus": "candidate-awaiting-manual-validation",
        "romAlignmentBytes": 4096, "slotFallback": "erase NRC31 with validated alternate image only",
        "applicationLicense": "LGPL-3.0-only", "applicationVersion": "1.50", "uiVersion": "SimpleCN V1.50",
        "qtMajor": int(args.qt_major), "architecture": "Windows x64",
        "buildType": "Release", "physicalDownloadEnabled": True, "physicalDownloadBuses": ["CAN", "LIN"], "physicalECUValidated": False,
        "defaultOnlineKeyProvider": "seed-as-key", "externalKeyBridgeIncluded": bridge.is_file(),
        "components": ["Qt runtime and plugins", "MinGW runtime",
                       "PEAK PCANBasic and PLIN API x64", "TOSUN libTSCAN runtime and supporting DLLs",
                       "Vector XL API x64 (classic CAN/LIN backend)",
                       "dbcppp 3.8.0 (MIT)", "Boost 1.84 headers (Boost Software License 1.0)"],
        "driverServicesIncluded": False,
        "driverRuntimeDirectory": "driver", "driverRuntimeManifest": driver_manifest,
        "distributionScope": "local-use-only" if args.local_use_only else "redistribution-rights-reviewed-by-distributor",
        "vendorRedistributionRightsVerifiedByProject": False,
    }
    (output/"release-info.json").write_text(
        json.dumps(inventory, ensure_ascii=False, indent=2)+"\n", encoding="utf-8")
    manifest = {}
    for path in sorted(output.rglob("*")):
        if path.is_file():
            manifest[path.relative_to(output).as_posix()] = hashlib.sha256(path.read_bytes()).hexdigest()
    (output/"manifest-sha256.json").write_text(
        json.dumps(manifest, ensure_ascii=False, indent=2)+"\n", encoding="utf-8")
    with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED) as z:
        for path in sorted(output.rglob("*")):
            if path.is_file():
                z.write(path, path.relative_to(output.parent).as_posix())
    digest = hashlib.sha256(archive.read_bytes()).hexdigest()
    archive.with_suffix(".zip.sha256").write_text(f"{digest}  {archive.name}\n", encoding="ascii")
    print(json.dumps({"directory": str(output), "archive": str(archive),
                      "files": len(manifest), "unpackedBytes": sum(p.stat().st_size for p in output.rglob("*") if p.is_file()),
                      "archiveBytes": archive.stat().st_size, "sha256": digest}, ensure_ascii=False))

if __name__ == "__main__":
    main()
