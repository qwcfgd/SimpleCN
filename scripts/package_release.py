"""Build a relocatable public release without private algorithms from an already deployed CMake build."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import zipfile

ROOT = Path(__file__).resolve().parents[1]
def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--qt-major", choices=["5", "6"], required=True)
    args = parser.parse_args()
    build, output = args.build.resolve(), args.output.resolve()
    archive = output.parent/(output.name+".zip")
    if output.exists() or archive.exists():
        parser.error("Output directory or archive already exists; choose a new output name.")
    required = [build/"QtBootloader.exe", build/"platforms/qwindows.dll",
                build/"dll/PLinApi.dll", build/"dll/PCANBasic.dll",
                ROOT/"docs/User-Guide.md", build/"libsignal_dbcppp.dll",
                ROOT/"LICENSE", ROOT/"LICENSE.GPL", ROOT/"docs/Licensing.md",
                ROOT/"docs/licenses/dbcppp-MIT.txt", ROOT/"docs/licenses/Boost-1.0.txt"]
    for path in required:
        if not path.is_file():
            parser.error(f"Missing required file: {path}")
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
    (output/"dll").mkdir()
    for name in ("PLinApi.dll", "PCANBasic.dll"):
        shutil.copy2(build/"dll"/name, output/"dll"/name)
    (output/"qt.conf").write_text("[Paths]\nPrefix=.\nPlugins=.\n", encoding="utf-8")
    (output/"docs").mkdir()
    for name in ("User-Guide.md", "Release-1.4.1.md", "Tosun-Hardware.md", "Trace-and-Graphics.md", "Signal-Workbench-Replay.md",
                 "CDD-UDS.md", "Default-Configuration.md", "Licensing.md", "Signal-Transmission-Implementation.md", "Logic-Review-Fixes.md"):
        shutil.copy2(ROOT/"docs"/name, output/"docs"/name)
    shutil.copytree(ROOT/"docs/licenses", output/"docs/licenses")
    (output/"README.txt").write_text(
        "Qt-GeneralController V1.4.1\n\n"
        "启动 QtBootloader.exe。首次包含 CAN01 和 LIN01，不自动连接。\n"
        "完整说明：docs/User-Guide.md\n"
        "发布包仅包含应用及运行依赖；测试、调试与模拟素材保存在 build/qttemp。\n"
        "公开包不含安全访问算法；实机使用需另行配置已授权 DLL 和桥接程序。\n"
        "请保留整个目录；不要只复制 exe。保存配置需要目录可写。\n",
        encoding="utf-8-sig")
    inventory = {
        "applicationLicense": "LGPL-3.0-only", "applicationVersion": "1.4.1", "uiVersion": "Qt-GeneralController V1.4.1",
        "qtMajor": int(args.qt_major), "architecture": "Windows x64",
        "buildType": "Release", "physicalDownloadEnabled": False, "physicalDownloadBuses": [], "physicalECUValidated": False,
        "components": ["Qt runtime and plugins", "MinGW runtime",
                       "PEAK PCANBasic and PLIN API x64", "TOSUN SDK adapter (SDK installed separately)",
                       "dbcppp 3.8.0 (MIT)", "Boost 1.84 headers (Boost Software License 1.0)"],
        "driverServicesIncluded": False,
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
