"""Independent ASC/BLF interoperability checks; development-only dependencies.

Run test_trace_plot first, then:
  python -m pip install python-can==4.6.1 vblf==0.3.1
  python scripts/verify_trace_exports.py --artifacts <build>/tests/artifacts/trace
No application runtime dependency on Python is introduced.
"""
import argparse
import json
import re
import struct
from collections import Counter
from pathlib import Path

import can
from vblf.reader import BlfReader


def verify(folder: Path):
    expected = sorted(json.loads((folder / "sample.json").read_text()), key=lambda r: r["us"])
    can_expected = [row for row in expected if row["bus"] == "CAN"]
    lin_expected = [row for row in expected if row["bus"] == "LIN"]
    asc_text = (folder / "sample.asc").read_text(encoding="utf-8")
    mapping = {
        name: int(number)
        for number, name in re.findall(r"// channel (\d+) = (.+)", asc_text)
    }
    for suffix, reader_type in (("asc", can.ASCReader), ("blf", can.BLFReader)):
        with reader_type(str(folder / ("sample." + suffix))) as reader:
            actual = list(reader)
            origin = reader.start_timestamp if suffix == "blf" else 0
        assert len(actual) == len(can_expected), (suffix, len(actual), len(can_expected))
        for got, want in zip(actual, can_expected):
            assert got.arbitration_id == want["id"]
            assert got.is_fd == want["fd"]
            assert not got.is_extended_id and not got.is_remote_frame
            assert bytes(got.data).hex() == want["data"]
            assert got.dlc == len(bytes.fromhex(want["data"]))
            assert got.is_rx != want["tx"]
            assert got.channel + 1 == mapping["0:" + want["channel"]]
            assert abs(got.timestamp - origin - want["us"] / 1e6) < 0.000002
        print(f"{suffix}: {len(actual)} CAN/CAN FD frames, payload/ID/channel/direction/time matched")

    with BlfReader(folder / "sample.blf") as reader:
        objects = list(reader)
    actual = [obj for obj in objects if type(obj).__name__ == "LinMessage"]
    assert len(actual) == len(lin_expected)
    for got, want in zip(actual, lin_expected):
        assert got.id == want["id"]
        assert got.data[:got.dlc].hex() == want["data"]
        assert got.dir == int(want["tx"])
        assert got.channel == mapping["1:" + want["channel"]]
        assert got.header.object_time_stamp == want["us"] * 1000
    asc_lin = []
    for line in asc_text.splitlines():
        parts = line.split()
        if len(parts) >= 5 and re.fullmatch(r"L\d+", parts[1]):
            size = int(parts[4])
            asc_lin.append((round(float(parts[0]) * 1e6), int(parts[2], 16),
                            "".join(parts[5:5 + size]).lower()))
    assert asc_lin == [(r["us"], r["id"], r["data"]) for r in lin_expected]
    print(f"asc/blf: {len(actual)} LIN frames matched")

    with BlfReader(folder / "mixed.blf") as reader:
        mixed = list(reader)
    types = Counter(int(obj.header.base.object_type) for obj in mixed)
    assert types[15] == types[14] == types[11] == types[100] == 1
    for obj in mixed:
        kind = int(obj.header.base.object_type)
        if kind in (14, 15):
            channel, frame_id, dlc = struct.unpack_from("<HBB", obj.buffer, 32)
            assert (channel, frame_id, dlc) == (2, 0x12, 8)
            assert obj.header.base.object_size == (48 if kind == 14 else 40)
    for suffix, reader_type in (("asc", can.ASCReader), ("blf", can.BLFReader)):
        with reader_type(str(folder / ("mixed." + suffix))) as reader:
            messages = list(reader)
        assert len(messages) == 4
        assert messages[1].is_extended_id
        assert messages[2].is_fd and messages[2].bitrate_switch and messages[2].dlc == 32
        assert messages[3].is_remote_frame and messages[3].dlc == 8
    print("mixed: standard/extended CAN, CAN FD, RTR, LIN no-response and receive-error checked")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--artifacts", type=Path, required=True)
    parser.add_argument("--sample-blf", type=Path)
    args = parser.parse_args()
    verify(args.artifacts)
    if args.sample_blf:
        with BlfReader(args.sample_blf) as reader:
            objects = list(reader)
        print("Original BLF sample:", dict(Counter(type(obj).__name__ for obj in objects)))
        assert any(type(obj).__name__ == "LinMessage2" for obj in objects)
