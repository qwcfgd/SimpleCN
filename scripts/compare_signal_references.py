"""Compare native synthetic fixture output with the pinned Python reference parsers."""
import argparse
import importlib.metadata
import json
from pathlib import Path
import subprocess
import tempfile

import cantools
import ldfparser

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--probe", type=Path, required=True)
    parser.add_argument("--report", type=Path, required=True)
    args = parser.parse_args()
    versions = {name: importlib.metadata.version(name) for name in ("cantools", "ldfparser")}
    assert versions == {"cantools": "44.1.0", "ldfparser": "0.26.0"}, versions
    checked = []
    with tempfile.TemporaryDirectory() as directory:
        for ext in ("dbc", "ldf"):
            source = ROOT / f"tests/fixtures/signals-synthetic.{ext}"
            out = Path(directory) / f"{ext}.json"
            subprocess.run([str(args.probe.resolve()), str(source), str(out)], check=True)
            native = json.loads(out.read_text(encoding="utf-8"))
            reference = (cantools.database.load_file(str(source), encoding="utf-8") if ext == "dbc"
                         else ldfparser.parse_ldf(str(source), encoding="utf-8", pad_with_zero=False))
            expected_count = len(reference.messages if ext == "dbc" else reference.get_unconditional_frames())
            assert len(native["frames"]) == expected_count
            for frame in native["frames"]:
                ref = reference.get_message_by_name(frame["name"]) if ext == "dbc" else reference.get_frame(frame["name"])
                assert frame["id"] == ref.frame_id and frame["length"] == ref.length
                if ext == "dbc":
                    assert frame["extended"] == ref.is_extended_frame
                    fields = {s.name: (s.start, s.length) for s in ref.signals}
                else:
                    assert frame["publisher"] == ref.publisher.name
                    fields = {s.name: (offset, s.width) for offset, s in ref.signal_map}
                assert fields == {s["name"]: (s["start"], s["width"]) for s in frame["fields"]}
                for sample in frame["samples"]:
                    values = {s["name"]: (list(bytes.fromhex(sample["values"][s["name"]])) if s["array"]
                                         else int(sample["values"][s["name"]])) for s in frame["fields"]}
                    encoded = (ref.encode(values, scaling=False, strict=False, padding=False) if ext == "dbc"
                               else ref.encode_raw(values))
                    assert encoded.hex() == sample["payload"], (ext, frame["name"], encoded.hex(), sample)
                checked.append({"bus": ext, "frame": frame["name"], "layout": "matched",
                                "payloadVectors": len(frame["samples"]), "nativeLimitation": frame["issue"]})
    report = {"referenceVersions": versions, "scope": "synthetic fixtures only; no hardware", "comparisons": checked}
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(f"Matched {len(checked)} frame layouts and {sum(x['payloadVectors'] for x in checked)} payload vectors.")


if __name__ == "__main__":
    main()
