"""Check TracePlotTest interop fixtures with Vector's original read-only BLF API.

No runtime dependency is added to the application. Run test_trace_plot's
blfInteroperabilityFixtures slot first, then pass --artifacts and one or more
--native-lib paths to the installed 64-bit Vector binlog.dll. --report is optional.
"""
import argparse
import ctypes as c
import hashlib
import json
import struct
import zlib
from collections import Counter
from pathlib import Path


class Base(c.Structure):
    _fields_ = [("signature", c.c_uint32), ("header_size", c.c_uint16),
                ("version", c.c_uint16), ("size", c.c_uint32), ("type", c.c_uint32)]


class Header(c.Structure):
    _fields_ = [("base", Base), ("flags", c.c_uint32), ("client", c.c_uint16),
                ("version", c.c_uint16), ("stamp", c.c_uint64)]


class Text(c.Structure):
    _fields_ = [("header", Header), ("source", c.c_uint32),
                ("reserved", c.c_uint32), ("length", c.c_uint32), ("text", c.c_void_p)]


class Stats(c.Structure):
    _fields_ = [("size", c.c_uint32), ("application", c.c_uint8 * 4),
                ("file_size", c.c_uint64), ("raw_size", c.c_uint64),
                ("count", c.c_uint32), ("read", c.c_uint32),
                ("start", c.c_uint16 * 8), ("end", c.c_uint16 * 8),
                ("reserved", c.c_uint32 * 18)]


def layout(path):
    """Strict framing check, including objects split across compressed containers."""
    data = path.read_bytes()
    assert data[:4] == b"LOGG"
    pos = struct.unpack_from("<I", data, 4)[0]
    raw = bytearray()
    boundaries = []
    while pos < len(data):
        sig, header, _, size, kind = struct.unpack_from("<4sHHII", data, pos)
        assert (sig, header, kind) == (b"LOBJ", 16, 10)
        method = struct.unpack_from("<H", data, pos + 16)[0]
        expected = struct.unpack_from("<I", data, pos + 24)[0]
        payload = data[pos + 32:pos + size]
        block = zlib.decompress(payload) if method == 2 else payload
        assert len(block) == expected
        raw.extend(block)
        boundaries.append(len(raw))
        assert data[pos + size:pos + size + size % 4] == bytes(size % 4)
        pos += size + size % 4
    assert pos == len(data)
    pos = 0
    remainders = Counter()
    objects = 0
    crossing = 0
    while pos < len(raw):
        assert raw[pos:pos + 4] == b"LOBJ", (path.name, "signature", pos)
        size = struct.unpack_from("<I", raw, pos + 8)[0]
        assert size >= 32
        end = pos + size
        padding = size % 4
        assert raw[end:end + padding] == bytes(padding), (path.name, "padding", end)
        crossing += sum(pos < b < end for b in boundaries[:-1])
        pos = end + padding
        remainders[size % 4] += 1
        objects += 1
    assert pos == len(raw)
    assert objects == struct.unpack_from("<I", data, 32)[0]
    assert set(remainders) == {0, 1, 2, 3}, remainders
    assert len(boundaries) > 1 and crossing > 0
    return {"containers": len(boundaries), "crossing_objects": crossing,
            "size_remainders": dict(remainders), "objects": objects}


def decode_static(kind, data):
    _, _, _, _, _, flags, _, _, stamp = struct.unpack_from("<4sHHIIIHHQ", data)
    event = {"bus": "LIN" if kind in (11, 14, 15, 57) else "CAN",
             "channel": struct.unpack_from("<H", data, 32)[0],
             "us": stamp * 10 if flags & 1 else stamp // 1000}
    if kind == 57:
        length = data[70]
        event.update(channel=struct.unpack_from("<H", data, 44)[0], kind="frame",
                     id=data[69], dlc=length, data=data[144:144 + length].hex(),
                     tx=bool(data[154]), checksum=struct.unpack_from("<H", data, 152)[0])
    elif kind == 101:
        wire_flags = struct.unpack_from("<I", data, 44)[0]
        identifier = struct.unpack_from("<I", data, 36)[0]
        length = (0, 1, 2, 3, 4, 5, 6, 7, 8, 12, 16, 20, 24, 32, 48, 64)[data[33]]
        rtr = bool(wire_flags & 16)
        event.update(channel=data[32], kind="frame", id=identifier & 0x1FFFFFFF,
                     dlc=length, data="" if rtr else data[72:72 + length].hex(),
                     tx=bool(data[66]), extended=bool(identifier & 0x80000000),
                     rtr=rtr, fd=bool(wire_flags & 0x1000),
                     brs=bool(wire_flags & 0x2000), esi=bool(wire_flags & 0x4000))
    elif kind == 2:
        event["kind"] = "error"
    elif kind in (14, 15):
        event.update(kind="error" if kind == 14 else "no-response", id=data[34], dlc=data[35])
    elif kind == 11:
        length = data[35]
        event.update(kind="frame", id=data[34], dlc=length, data=data[36:36 + length].hex(),
                     tx=bool(data[50]), checksum=struct.unpack_from("<H", data, 48)[0])
    else:
        wire_flags, dlc, identifier = struct.unpack_from("<BBI", data, 34)
        fd = kind == 100
        length = (0, 1, 2, 3, 4, 5, 6, 7, 8, 12, 16, 20, 24, 32, 48, 64)[dlc] if fd else dlc
        rtr = bool(wire_flags & 128)
        start = 52 if fd else 40
        event.update(kind="frame", id=identifier & 0x1FFFFFFF, dlc=length,
                     data="" if rtr else data[start:start + length].hex(), tx=bool(wire_flags & 1),
                     extended=bool(identifier & 0x80000000), rtr=rtr, fd=fd,
                     brs=bool(data[45] & 2) if fd else False,
                     esi=bool(data[45] & 4) if fd else False)
    return event


def native_read(path, library, converted=False):
    dll = c.CDLL(str(library))
    signatures = {
        "BLCreateFile": (c.c_void_p, [c.c_char_p, c.c_uint32]),
        "BLPeekObject": (c.c_int, [c.c_void_p, c.POINTER(Base)]),
        "BLSkipObject": (c.c_int, [c.c_void_p, c.POINTER(Base)]),
        "BLReadObjectSecure": (c.c_int, [c.c_void_p, c.POINTER(Base), c.c_size_t]),
        "BLFreeObject": (c.c_int, [c.c_void_p, c.POINTER(Base)]),
        "BLGetFileStatisticsEx": (c.c_int, [c.c_void_p, c.POINTER(Stats)]),
        "BLCloseHandle": (c.c_int, [c.c_void_p]),
    }
    for name, (restype, argtypes) in signatures.items():
        function = getattr(dll, name)
        function.restype, function.argtypes = restype, argtypes
    handle = dll.BLCreateFile(str(path).encode("mbcs"), 0x80000000)
    assert handle not in (None, c.c_void_p(-1).value), (path, "open failed")
    counts = Counter()
    events = []
    try:
        statistics = Stats()
        statistics.size = c.sizeof(statistics)
        assert dll.BLGetFileStatisticsEx(handle, c.byref(statistics))
        base = Base()
        while dll.BLPeekObject(handle, c.byref(base)):
            kind = base.type
            if kind == 115 and converted:
                # Vector's file index objects are excluded from its statistics count.
                assert dll.BLSkipObject(handle, c.byref(base))
                counts[kind] += 1
                continue
            if kind == 65:
                value = Text()
                value.header.base = base
                pointer = c.byref(value.header.base)
                assert dll.BLReadObjectSecure(handle, pointer, c.sizeof(value)), (path, kind)
                assert c.string_at(value.text, value.length)
            else:
                assert kind in (1, 2, 11, 14, 15, 57, 86, 100, 101), (path, kind)
                value = c.create_string_buffer(256)
                c.memmove(value, c.byref(base), c.sizeof(base))
                pointer = c.cast(value, c.POINTER(Base))
                assert dll.BLReadObjectSecure(handle, pointer, c.sizeof(value)), (path, kind)
                events.append(decode_static(kind, value.raw))
            assert dll.BLFreeObject(handle, pointer)
            counts[kind] += 1
        assert dll.BLGetFileStatisticsEx(handle, c.byref(statistics))
        assert sum(counts.values()) - counts[115] == statistics.count == statistics.read, (
            path, "premature EOF", sum(counts.values()), statistics.count, statistics.read)
        return events, dict(counts)
    finally:
        assert dll.BLCloseHandle(handle)


def verify(folder, libraries):
    results = []
    for mode in ("can", "lin", "mixed"):
        path = folder / f"interop-{mode}.blf"
        expected = json.loads(path.with_suffix(".json").read_text(encoding="utf-8"))
        structure = layout(path)
        assert structure["objects"] == expected["objects"]
        for library in libraries:
            events, counts = native_read(path, library)
            assert len(events) == len(expected["events"]), (path, len(events), len(expected["events"]))
            for index, (got, want) in enumerate(zip(events, expected["events"])):
                assert got == want, (path, index, got, want)
            result = dict(file=str(path), library=str(library), events=len(events),
                          types=counts, sha256=hashlib.sha256(path.read_bytes()).hexdigest(), **structure)
            results.append(result)
            print(json.dumps(result))
    return results


def verify_conversions(folder, conversions, libraries):
    results = []
    for mode in ("can", "lin", "mixed"):
        manifest = json.loads((folder / f"interop-{mode}.json").read_text(encoding="utf-8"))
        # Existing ASC exports LIN errors/no-response as comments, not replay events.
        expected = [event for event in manifest["events"]
                    if event["bus"] == "CAN" or event["kind"] == "frame"]
        path = conversions / f"canoe-{mode}-from-asc.blf"
        for library in libraries:
            events, counts = native_read(path, library, converted=True)
            assert len(events) == len(expected), (path, "converted event count", len(events), len(expected))
            for index, (got, want) in enumerate(zip(events, expected)):
                assert got == want, (path, index, got, want)
            result = dict(file=str(path), library=str(library), events=len(events), types=counts)
            results.append(result)
            print(json.dumps(result))
    return results


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--artifacts", required=True, type=Path)
    parser.add_argument("--native-lib", required=True, action="append", type=Path)
    parser.add_argument("--report", type=Path)
    parser.add_argument("--canoe-conversions", type=Path,
                        help="Also verify CANoe-converted ASC files against the source manifests")
    args = parser.parse_args()
    results = verify(args.artifacts, args.native_lib)
    if args.canoe_conversions:
        results.extend(verify_conversions(args.artifacts, args.canoe_conversions, args.native_lib))
    if args.report:
        args.report.write_text(json.dumps(results, indent=2), encoding="utf-8")
