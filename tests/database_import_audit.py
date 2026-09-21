"""Audit private local databases without copying them into the repository.

Usage: python tests/database_import_audit.py --probe <signal_fixture_probe.exe>
       --source <repository> --output <../build/qttemp/database-import-audit/qt6>
"""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import re
import subprocess
import xml.etree.ElementTree as ET


def read_text(path):
    data = path.read_bytes()
    try:
        return data.decode("utf-8-sig")
    except UnicodeDecodeError:
        return data.decode("gb18030")


def block(text, name):
    match = re.search(r"\b" + re.escape(name) + r"\s*\{", text)
    if not match:
        return ""
    start = match.end()
    depth = 1
    for index in range(start, len(text)):
        depth += (text[index] == "{") - (text[index] == "}")
        if not depth:
            return text[start:index]
    raise ValueError("Unclosed block: " + name)


def inventory(path, result):
    """Independently compare names and membership, not just parser exit status."""
    if path.suffix.lower() == ".cdd":
        root = ET.parse(path).getroot()
        expected = []
        for ecu in root.findall("./ECUDOC/ECU"):
            for variant in ecu.iter("VAR"):
                if variant.get("baseref") or variant.get("basevarref"):
                    raise ValueError("Inventory comparator requires a non-inherited local CDD variant")
                for inst in variant.iter("DIAGINST"):
                    if inst.get("enabled") in ("0", "false", "no"):
                        continue
                    for service in inst.findall("SERVICE"):
                        if service.get("enabled") not in ("0", "false", "no"):
                            expected.append(inst.findtext("QUAL", "") + "/" + service.findtext("QUAL", ""))
        actual = [s["qualifier"] for e in result["ecus"] for v in e["variants"] for s in v["services"]]
        if Counter(expected) != Counter(actual):
            raise ValueError("CDD service inventory mismatch")
        return {"services": len(actual), "warnings": result["warnings"]}

    text = read_text(path)
    expected = {}
    if path.suffix.lower() == ".dbc":
        current = None
        for line in text.splitlines():
            match = re.match(r"\s*BO_\s+\d+\s+(\w+)\s*:", line)
            if match:
                current = match[1]
                expected[current] = []
            signal = re.match(r"\s*SG_\s+(\w+)\s", line)
            if signal and current:
                expected[current].append(signal[1])
    else:
        # Mask comments and strings before brace scanning; braces inside labels
        # must not change the source inventory.
        text = re.sub(r'"(?:\\.|[^"\\])*"|/\*[\s\S]*?\*/|//[^\n]*', lambda m: " " * len(m[0]), text)
        for section in ("Frames", "Diagnostic_frames"):
            contents = block(text, section)
            for match in re.finditer(r"(\w+)\s*:\s*(?:0[xX][\da-fA-F]+|\d+)[^{]*\{([^{}]*)\}", contents):
                expected[match[1]] = re.findall(r"\b(\w+)\s*,\s*(?:0[xX][\da-fA-F]+|\d+)\s*;", match[2])
        schedules = block(text, "Schedule_tables")
        for name in ("MasterReq", "SlaveResp"):
            if re.search(r"\b" + name + r"\s+delay\b", schedules):
                expected.setdefault(name, [])
        names = re.findall(r"\b(\w+)\s*\{", schedules)
        # The local corpus has ordinary slots, without nested command blocks.
        if Counter(names) != Counter(s["name"] for s in result["schedules"]):
            raise ValueError("LDF schedule inventory mismatch")
    actual = {f["name"]: [s["name"] for s in f["fields"]] for f in result["frames"]}
    if {k: Counter(v) for k, v in expected.items()} != {k: Counter(v) for k, v in actual.items()}:
        raise ValueError("Frame/signal inventory mismatch")
    return {"frames": len(actual), "signals": sum(map(len, actual.values())),
            "schedules": len(result["schedules"]),
            "restrictedFrames": [f["name"] for f in result["frames"] if f["issue"]],
            "diagnostics": result["diagnostics"],
            "scheduleDiagnostics": {s["name"]: s["issue"] for s in result["schedules"] if s["issue"]}}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--probe", required=True, type=Path)
    parser.add_argument("--source", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    rows = []
    for kind in ("dbc", "ldf", "cdd"):
        paths = sorted((args.source / kind).rglob("*." + kind))
        if not paths:
            raise RuntimeError("No local files found in " + kind)
        for index, path in enumerate(paths):
            before = hashlib.sha256(path.read_bytes()).hexdigest()
            output = args.output / f"{kind}-{index}.json"
            run = subprocess.run([str(args.probe.resolve()), str(path.resolve()), str(output.resolve())], capture_output=True)
            row = {"file": str(path.relative_to(args.source)), "sha256": before, "exitCode": run.returncode}
            try:
                if run.returncode:
                    raise ValueError(f"Probe failed ({run.returncode})")
                result = json.loads(output.read_text(encoding="utf-8"))
                if result.get("error"):
                    raise ValueError(result["error"])
                row.update(inventory(path, result))
                if before != hashlib.sha256(path.read_bytes()).hexdigest():
                    raise ValueError("Source database changed during audit")
                row["passed"] = True
            except (ValueError, KeyError, OSError) as error:
                row.update(passed=False, error=str(error))
            rows.append(row)
            print(json.dumps(row, ensure_ascii=True))
    (args.output / "summary.json").write_text(json.dumps(rows, ensure_ascii=False, indent=2), encoding="utf-8")
    return 0 if all(row["passed"] for row in rows) else 1


if __name__ == "__main__":
    raise SystemExit(main())
