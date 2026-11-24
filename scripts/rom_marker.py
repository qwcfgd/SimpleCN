"""Shared 64-byte ROM marker encoder/validator; no hardware access or host padding."""
from datetime import datetime, timezone

POLY = 0x42F0E1EBA9EA3693
MASK = (1 << 64) - 1
TYPES = {"driver": b"DRIVER\xff\xff", "app": b"APP" + b"\xff" * 5,
         "boot": b"BOOT" + b"\xff" * 4, "calib": b"CAL" + b"\xff" * 5}


def crc64(data, crc=0):
    for value in data:
        crc ^= value << 56
        for _ in range(8):
            crc = ((crc << 1) ^ (POLY if crc >> 63 else 0)) & MASK
    return crc


def make_marker(kind, image_bytes, body, date=None, hardware="A", stage="ST1"):
    date = date or datetime.now(timezone.utc).strftime("%Y%m%d")
    if len(date) != 8 or datetime.strptime(date, "%Y%m%d").strftime("%Y%m%d") != date:
        raise ValueError("date must be an eight-character YYYYMMDD")
    if kind not in TYPES or not 64 <= image_bytes <= 0xFFFFFFFF:
        raise ValueError("invalid role or actual UDS image length")
    if len(hardware) != 1 or len(stage) != 3 or not all(c in "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789" for c in hardware + stage):
        raise ValueError("hardware/stage must be uppercase alphanumeric XYYY")
    version = f"{hardware}{stage}{crc64(date.encode('ascii')) >> 48:04X}"
    prefix = (b"S32K144T" + TYPES[kind] + version.encode("ascii") + date.encode("ascii")
              + f"{image_bytes:016X}".encode("ascii") + crc64(body).to_bytes(8, "big"))
    assert len(prefix) == 56
    return prefix + crc64(prefix).to_bytes(8, "big")


def ranges(kind, image_bytes):
    if kind == "driver":
        return 0, 0, 64
    if kind == "app":
        if image_bytes < 8192 or image_bytes % 4096:
            raise ValueError("App package must already be 4 KiB aligned")
        return 0xFC0, 0, 4096
    if kind == "boot":
        if image_bytes not in (0xF000, 0x14000):
            raise ValueError("Boot package must cover its configured complete slot")
        return image_bytes - 64, image_bytes - 64, 64
    if kind == "calib":
        if image_bytes != 4096:
            raise ValueError("Calib must already cover one complete 4 KiB page")
        return 0xFC0, 0xFC0, 64
    raise ValueError("unknown image role")


def stamp(data, kind, **identity):
    """Replace the reserved marker only; preserve every other supplied byte."""
    at, skip, count = ranges(kind, len(data))
    body = data[:skip] + data[skip + count:]
    result = bytearray(data)
    result[at:at + 64] = make_marker(kind, len(data), body, **identity)
    validate(bytes(result), kind)
    return bytes(result)


def decode(raw):
    if len(raw) != 64 or raw[:8] != b"S32K144T" or raw[8:16] not in TYPES.values():
        raise ValueError("marker project/type")
    kind = next(k for k, value in TYPES.items() if raw[8:16] == value)
    date = raw[24:32].decode("ascii")
    if len(date) != 8 or datetime.strptime(date, "%Y%m%d").strftime("%Y%m%d") != date:
        raise ValueError("marker date")
    if any(c not in b"0123456789ABCDEF" for c in raw[32:48]):
        raise ValueError("length must be sixteen uppercase ASCII hexadecimal digits")
    length = int(raw[32:48], 16)
    if not 64 <= length <= 0xFFFFFFFF:
        raise ValueError("marker UDS length range")
    version = raw[16:24].decode("ascii")
    if any(c not in "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789" for c in version[:4]) or version[4:] != f"{crc64(raw[24:32]) >> 48:04X}":
        raise ValueError("marker version/date fingerprint")
    if crc64(raw[:56]) != int.from_bytes(raw[56:64], "big"):
        raise ValueError("marker CRC64")
    return {"kind": kind, "project": "S32K144T", "version": version, "date": date,
            "image_bytes": length, "body_crc64": int.from_bytes(raw[48:56], "big")}


def validate(data, kind):
    at, skip, count = ranges(kind, len(data))
    result = decode(data[at:at + 64])
    if result["kind"] != kind or result["image_bytes"] != len(data):
        raise ValueError("marker role/actual image length mismatch")
    if crc64(data[:skip] + data[skip + count:]) != result["body_crc64"]:
        raise ValueError("body CRC64")
    return result
