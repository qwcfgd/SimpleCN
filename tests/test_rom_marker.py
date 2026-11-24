"""Offline packer and actual portable ECU verifier tests. Never opens hardware."""
import ctypes as C
import importlib.util
import os
from pathlib import Path
import struct
import unittest

spec = importlib.util.spec_from_file_location("rom_marker", Path(__file__).parents[1] / "scripts/rom_marker.py")
marker = importlib.util.module_from_spec(spec)
spec.loader.exec_module(marker)


class Marker(C.Structure):
    _fields_ = [("project", C.c_uint8 * 8), ("version", C.c_uint8 * 8),
                ("date", C.c_uint8 * 8), ("kind", C.c_uint32),
                ("imageBytes", C.c_uint32), ("bodyCrc", C.c_uint64)]


class Verify(C.Structure):
    _fields_ = [("marker", Marker), ("crc", C.c_uint64)] + [
        (x, C.c_uint32) for x in ("base", "capacity", "kind", "cursor", "skip", "skipBytes", "started")]


class Descriptor(C.Structure):
    _fields_ = [("codeBytes", C.c_uint32), ("transferBytes", C.c_uint32),
                ("binding", C.c_uint8 * 16), ("appVersion", C.c_uint8 * 9)]


def image(kind, stage="ST1", size=None):
    size = size or {"driver": 512, "app": 0x3000, "boot": 0xF000, "calib": 4096}[kind]
    data = bytearray((i * 17 + 3) & 255 for i in range(size))
    if kind == "app":
        data[:4096] = b"\xff" * 4096
        data[:8] = b"T008APP2"
        struct.pack_into("<I", data, 8, size - 4096)
        data[46:55] = b"T008A0001"
    return marker.stamp(bytes(data), kind, hardware="A", stage=stage, date="20261010")


class PackerTests(unittest.TestCase):
    def test_ecma_and_date(self):
        self.assertEqual(marker.crc64(b"123456789"), 0x6C40DF5F0B497347)
        self.assertEqual(marker.validate(image("app"), "app")["version"], "AST17CFF")

    def test_four_roles_and_mutations(self):
        for kind in marker.TYPES:
            with self.subTest(kind=kind):
                data = image(kind)
                self.assertEqual(marker.validate(data, kind)["image_bytes"], len(data))
                at, skip, count = marker.ranges(kind, len(data))
                bad = bytearray(data)
                body_at = 64 if kind == "driver" else 4096 if kind == "app" else 0
                bad[body_at] ^= 1
                with self.assertRaises(ValueError): marker.validate(bytes(bad), kind)
                bad = bytearray(data);bad[at + 63] ^= 1
                with self.assertRaises(ValueError): marker.validate(bytes(bad), kind)

    def test_actual_container_length(self):
        data = image("app")
        larger = marker.stamp(data + b"\xff" * 4096, "app", date="20261010")
        self.assertNotEqual(marker.validate(data, "app")["body_crc64"], marker.validate(larger, "app")["body_crc64"])
        with self.assertRaises(ValueError): marker.validate(data + b"\xff" * 4096, "app")

    def test_no_implicit_padding(self):
        for kind in ("app", "boot", "calib"):
            with self.assertRaises(ValueError): marker.stamp(bytes(4095), kind)


@unittest.skipUnless(os.environ.get("T008_NATIVE_MARKER_DLL"), "native ECU DLL not configured")
class NativeEcuTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.lib = C.CDLL(os.environ["T008_NATIVE_MARKER_DLL"])
        cls.read_type = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_uint32, C.POINTER(C.c_uint8), C.c_uint32)
        cls.lib.RomMarker_Decode.argtypes = [C.c_void_p, C.POINTER(Marker)]
        cls.lib.T008_RomVerify_Init.argtypes = [C.POINTER(Verify), C.c_uint32, C.c_uint32, C.c_uint32]
        cls.lib.T008_RomVerify_Step.argtypes = [C.POINTER(Verify), cls.read_type, C.c_void_p]
        cls.lib.T008_Dependencies_Check.argtypes = [C.POINTER(Marker)] * 3
        cls.lib.T008_DescriptorV2_Parse.argtypes = [C.c_void_p, C.c_void_p, C.POINTER(Descriptor)]
        cls.lib.T008_DriverImageDecode.argtypes = [C.c_void_p, C.c_uint32, C.c_void_p]

    def verify(self, data, kind, io_failure=False):
        base = {"app": 0x15000, "boot": 0x5000, "calib": 0x7E000}[kind]
        state = Verify();reads = []
        @self.read_type
        def read(_, address, out, count):
            offset = address - base
            if io_failure or offset < 0 or offset + count > len(data): return 1
            self.assertLessEqual(count, 256)
            reads.append((offset, count));C.memmove(out, data[offset:offset + count], count);return 0
        self.lib.T008_RomVerify_Init(C.byref(state), base, len(data), {"app": 2, "boot": 3, "calib": 4}[kind])
        for _ in range(len(data) // 256 + 8):
            result = self.lib.T008_RomVerify_Step(C.byref(state), read, None)
            if result != 1: return result, state.marker, reads
        self.fail("incremental verifier did not terminate")

    def test_actual_flash_reads_and_corruption(self):
        for kind in ("boot", "calib", "app"):
            data = image(kind)
            result, decoded, reads = self.verify(data, kind)
            self.assertEqual(result, 0);self.assertEqual(decoded.imageBytes, len(data))
            bad = bytearray(data);bad[4096 if kind == "app" else 0] ^= 1
            self.assertEqual(self.verify(bytes(bad), kind)[0], 2)
            self.assertEqual(self.verify(data, kind, True)[0], 3)

    def test_boot_crc_covers_bytes_before_final_marker(self):
        data = bytearray(image("boot"));data[0xEF00] ^= 1
        self.assertEqual(self.verify(bytes(data), "boot")[0], 2)

    def test_marker_length_bound(self):
        data = bytearray(image("app"));at = 0xFC0
        data[at + 32:at + 48] = b"0000000000004000"
        data[at + 56:at + 64] = marker.crc64(data[at:at + 56]).to_bytes(8, "big")
        self.assertEqual(self.verify(bytes(data), "app")[0], 2)

    def test_dependencies_use_hardware_stage(self):
        decoded = [self.verify(image(k), k)[1] for k in ("app", "calib", "boot")]
        self.assertEqual(self.lib.T008_Dependencies_Check(*[C.byref(x) for x in decoded]), 0)
        decoded[1] = self.verify(image("calib", "ST2"), "calib")[1]
        self.assertEqual(self.lib.T008_Dependencies_Check(*[C.byref(x) for x in decoded]), 4)
        decoded[1] = self.verify(image("calib"), "calib")[1]
        decoded[0] = self.verify(image("app", "ST2"), "app")[1]
        self.assertEqual(self.lib.T008_Dependencies_Check(*[C.byref(x) for x in decoded]), 1)

    def test_legacy_minimum_boot_config_fields_have_no_effect(self):
        data = image("app");header = bytearray(data[:88]);header[12:46] = b"X" * 34;header[55:87] = b"X" * 32
        out = Descriptor()
        self.assertEqual(self.lib.T008_DescriptorV2_Parse(bytes(header), data[0xFC0:0x1000], C.byref(out)), 0)
        self.assertEqual(out.transferBytes, len(data));self.assertEqual(bytes(out.appVersion), b"T008A0001")

    def test_did_version_independent_of_marker(self):
        data = image("app"); header = data[:88]; at = 0xFC0; out = Descriptor()
        other = marker.stamp(data, "app", date="20261011")
        self.assertNotEqual(data[at + 16:at + 24], other[at + 16:at + 24])
        for raw in (data[at:at + 64], other[at:at + 64]):
            self.assertEqual(self.lib.T008_DescriptorV2_Parse(header, raw, C.byref(out)), 0)
            self.assertEqual(bytes(out.appVersion), b"T008A0001")
        header = bytearray(header);header[46:55] = b"T008A0002"
        self.assertEqual(self.lib.T008_DescriptorV2_Parse(bytes(header), other[at:at + 64], C.byref(out)), 0)
        self.assertEqual(bytes(out.appVersion), b"T008A0002")

    def test_driver_gate_rejects_corruption_and_ignores_version(self):
        data = bytearray(512)
        data[64:72] = b"T008DRV1"
        struct.pack_into("<4H", data, 72, 192, 0xFFFF, 0xFFFF, 0)
        struct.pack_into("<14I", data, 80, 512, 0x20001400, 512, 256,
                         192, 194, 196, 198, 200, 192, 4096, 1024, 0, 20000000)
        data[136:152] = b"0123456789ABCDEF"
        struct.pack_into("<I", data, 152, 202)
        data = marker.stamp(bytes(data), "driver", date="20261010")
        out = C.create_string_buffer(128)
        self.assertEqual(self.lib.T008_DriverImageDecode(data, len(data), out), 0)
        self.assertEqual(int.from_bytes(out.raw[:2], "little"), 192)
        bad = bytearray(data);bad[300] ^= 1
        self.assertEqual(self.lib.T008_DriverImageDecode(bytes(bad), len(bad), out), 1)
        self.assertEqual(out.raw[:80], bytes(80))
        bad = bytearray(data);struct.pack_into("<I", bad, 96, 512)
        bad = marker.stamp(bytes(bad), "driver", date="20261010")
        self.assertEqual(self.lib.T008_DriverImageDecode(bad, len(bad), out), 1)


@unittest.skipUnless(os.environ.get("T008_NATIVE_MARKER_DLL"), "native ECU DLL not configured")
class NativeDownloadCoreTests(unittest.TestCase):
    """Actual ECU core and verifier; in-memory adapter, no ARM execution/hardware."""
    class Reply(C.Structure):
        _fields_ = [("bytes", C.c_uint32), ("data", C.c_uint8 * 3), ("nrc", C.c_uint8)]

    @classmethod
    def setUpClass(cls):
        cls.lib = C.CDLL(os.environ["T008_NATIVE_MARKER_DLL"])
        cls.lib.MarkerCore_Request.argtypes = [C.c_uint8, C.c_void_p, C.c_uint32, C.POINTER(cls.Reply)]
        for name in ("MarkerCore_Erase", "MarkerCore_Security"):
            getattr(cls.lib, name).argtypes = [C.c_void_p, C.c_uint32, C.POINTER(cls.Reply)]
        for name in ("MarkerCore_Check", "MarkerCore_ErasePoll"):
            getattr(cls.lib, name).argtypes = [C.POINTER(cls.Reply)]
        cls.lib.MarkerCore_SetFlash.argtypes = [C.c_uint32, C.c_void_p, C.c_uint32]
        cls.lib.MarkerCore_SetRam.argtypes = [C.c_uint32, C.c_uint8]
        cls.lib.MarkerCore_Get.argtypes = [C.c_uint32]
        cls.lib.MarkerCore_StartupRomSet.argtypes = [C.c_uint32]
        cls.lib.MarkerCore_FlashCrc.restype = C.c_uint64
        cls.lib.MarkerCore_SetSelection.argtypes = [C.c_uint32]
        cls.lib.MarkerCore_SetRunning.argtypes = [C.c_uint32]

    def setUp(self):
        self.assertEqual(self.lib.MarkerCore_Open(), 0)
        self.images = {"boot": image("boot"), "calib": image("calib"), "app": image("app")}
        for address, kind in ((0x5000, "boot"), (0x15000, "app"), (0x7E000, "calib")):
            self.set_flash(address, self.images[kind])

    def set_flash(self, address, data):
        self.assertEqual(self.lib.MarkerCore_SetFlash(address, bytes(data), len(data)), 0)

    def test_startup_accepts_marker_and_rejects_reserved_bytes(self):
        data = bytearray(self.images["app"])
        struct.pack_into("<II", data, 4096, 0x20001400, 0x16401)
        data = marker.stamp(bytes(data), "app", date="20261010")
        self.set_flash(0x15000, data)
        self.assertEqual(self.lib.MarkerCore_StartupApp(), 0)
        # The App body CRC excludes this descriptor page. Reserved bytes must
        # still be rejected independently of the valid marker at its end.
        self.set_flash(0x15FBF, b"\0")
        self.assertEqual(self.lib.MarkerCore_StartupApp(), 3)

    def test_startup_rejects_bad_marker_vectors_and_read_failure(self):
        data = bytearray(self.images["app"])
        struct.pack_into("<II", data, 4096, 0x20001400, 0x16401)
        data = bytearray(marker.stamp(bytes(data), "app", date="20261010"))
        data[0xFFF] ^= 1
        self.set_flash(0x15000, data)
        self.assertEqual(self.lib.MarkerCore_StartupApp(), 3)
        struct.pack_into("<II", data, 4096, 0x20001400, 0x16400)
        data = marker.stamp(bytes(data), "app", date="20261010")
        self.set_flash(0x15000, data)
        self.assertEqual(self.lib.MarkerCore_StartupApp(), 6)
        self.lib.MarkerCore_SetFault(1)
        self.assertNotEqual(self.lib.MarkerCore_StartupApp(), 0)

    def test_startup_rom_set_works_without_driver_or_download_job(self):
        self.assertEqual(self.lib.MarkerCore_StartupRomSet(0x5000), 0)
        self.assertEqual(self.lib.MarkerCore_Get(3), 0, "startup invoked Driver")
        self.assertEqual(self.lib.MarkerCore_Get(1), 0, "startup selected Boot")
        self.assertEqual(self.lib.MarkerCore_Get(2), 0, "startup committed App")

    def test_startup_rom_set_rejects_corrupt_bodies_and_mismatch(self):
        for base, kind, offset, expected in ((0x5000, "boot", 0xE000, 2),
                                           (0x15000, "app", 4096, 1),
                                           (0x7E000, "calib", 0, 4)):
            with self.subTest(kind=kind):
                self.setUp()
                data = bytearray(self.images[kind]);data[offset] ^= 1
                self.set_flash(base, data)
                self.assertEqual(self.lib.MarkerCore_StartupRomSet(0x5000), expected)
        self.setUp();self.set_flash(0x15000, image("app", stage="ST2"))
        self.assertEqual(self.lib.MarkerCore_StartupRomSet(0x5000), 1)

    def test_startup_rom_set_rejects_read_failure_and_invalid_boot(self):
        self.assertEqual(self.lib.MarkerCore_StartupRomSet(0x14000), 2)
        self.lib.MarkerCore_SetFault(1)
        self.assertEqual(self.lib.MarkerCore_StartupRomSet(0x5000), 5)

    def selector_images(self):
        for base in (0x5000, 0x6D000):
            data = bytearray(self.images["boot"])
            struct.pack_into("<II", data, 0, 0x20001400, base + 0x401)
            self.set_flash(base, marker.stamp(bytes(data), "boot", date="20261010"))

    def test_selector_preserves_valid_selection_without_writes(self):
        self.selector_images()
        for base in (0x5000, 0x6D000):
            self.lib.MarkerCore_SetSelection(base)
            before = self.lib.MarkerCore_FlashCrc()
            self.assertEqual(self.lib.MarkerCore_ChooseBoot(), base)
            self.assertEqual(self.lib.MarkerCore_FlashCrc(), before)
            for field in (1, 2, 3, 11):
                self.assertEqual(self.lib.MarkerCore_Get(field), 0)

    def test_selector_invalid_and_torn_records_choose_valid_a_first(self):
        self.selector_images()
        target = struct.pack("<II", 0x6D000, 0xFFFFFFFF)
        records = [bytes(8), b"\xff" * 8, struct.pack("<II", 0x14000, 0xFFFFFFFF),
                   struct.pack("<II", 0x6D000, 0)]
        records += [target[:n] + b"\xff" * (8 - n) for n in range(9)]
        for record in records:
            with self.subTest(record=record.hex()):
                self.set_flash(0x4000, record)
                selected = self.lib.MarkerCore_SelectionRecord()
                expected = 0x6D000 if selected == 0x6D000 else 0x5000
                self.assertEqual(self.lib.MarkerCore_ChooseBoot(), expected)

    def test_selector_corrupt_selected_boot_falls_back(self):
        for selected, other in ((0x5000, 0x6D000), (0x6D000, 0x5000)):
            self.selector_images()
            self.lib.MarkerCore_SetSelection(selected)
            self.set_flash(selected + 0xEF00, b"\0")
            self.assertEqual(self.lib.MarkerCore_ChooseBoot(), other)
        self.set_flash(0x5000 + 0xEF00, b"\0")
        self.assertEqual(self.lib.MarkerCore_ChooseBoot(), 0)

    def test_selector_rejects_vectors_even_with_valid_crc(self):
        for msp, reset in ((0x1FFF9000, 0x5401), (0x20001401, 0x5401),
                           (0x20001400, 0x5400), (0x20001400, 0x5001),
                           (0x20001400, 0x13FC1)):
            self.selector_images()
            bad = bytearray(self.images["boot"])
            struct.pack_into("<II", bad, 0, msp, reset)
            self.set_flash(0x5000, marker.stamp(bytes(bad), "boot", date="20261010"))
            self.lib.MarkerCore_SetSelection(0x5000)
            self.assertEqual(self.lib.MarkerCore_ChooseBoot(), 0x6D000)

    def test_selector_read_failures_are_not_valid_data(self):
        self.selector_images()
        self.lib.MarkerCore_SetSelection(0x6D000)
        for fault, expected in ((7, 0x5000), (10, 0x6D000), (11, 0x5000), (1, 0)):
            self.lib.MarkerCore_SetFault(fault)
            self.assertEqual(self.lib.MarkerCore_ChooseBoot(), expected)

    def request(self, sid, data=b"", expected=0):
        reply = self.Reply()
        self.assertEqual(self.lib.MarkerCore_Request(sid, data, len(data), C.byref(reply)), expected)
        return reply

    def transfer(self, address, data, finish=True):
        self.request(0x34, b"\0\x44" + struct.pack(">II", address, len(data)))
        for n, at in enumerate(range(0, len(data), 1024), 1):
            self.request(0x36, bytes([n & 255]) + data[at:at + 1024])
        if finish: self.request(0x37)

    def driver(self, finish=True):
        data = bytearray(512);data[64:72] = b"T008DRV1"
        struct.pack_into("<4H", data, 72, 192, 0xFFFF, 0xFFFF, 0)
        struct.pack_into("<14I", data, 80, 512, 0x20001400, 512, 256,
                         192, 194, 196, 198, 200, 192, 4096, 1024, 0, 20000000)
        data[136:152] = b"0123456789ABCDEF";struct.pack_into("<I", data, 152, 202)
        self.driver_data = marker.stamp(bytes(data), "driver", date="20261010")
        self.transfer(0x20001400, self.driver_data, finish)

    def download(self):
        self.driver()
        for address, kind, capacity in ((0x6D000, "boot", 0xF000),
                                        (0x7E000, "calib", 4096), (0x15000, "app", 0x57000)):
            reply = self.Reply();option = b"\x44" + struct.pack(">II", address, capacity)
            result = self.lib.MarkerCore_Erase(option, len(option), C.byref(reply))
            self.assertEqual(result, 1)
            for _ in range(400):
                result = self.lib.MarkerCore_ErasePoll(C.byref(reply))
                if result != 1: break
            self.assertEqual(result, 0)
            self.transfer(address, self.images[kind])
            option = struct.pack(">III", address, len(self.images[kind]), 0)
            self.assertEqual(self.lib.MarkerCore_Security(option, len(option), C.byref(reply)), 0)
            self.assertEqual(reply.data[0], 0)

    def final_check(self):
        reply = self.Reply();pending = 0
        prior_selects = self.lib.MarkerCore_Get(1)
        prior_completes = self.lib.MarkerCore_Get(2)
        for _ in range(3000):
            result = self.lib.MarkerCore_Check(C.byref(reply))
            if result != 1: return result, reply, pending
            pending += 1
            self.assertEqual(self.lib.MarkerCore_Get(1), prior_selects, "selected before all validation completed")
            self.assertEqual(self.lib.MarkerCore_Get(2), prior_completes, "committed before all validation completed")
        self.fail("FF01 did not terminate")

    def erase_start(self, address, capacity):
        reply = self.Reply();option = b"\x44" + struct.pack(">II", address, capacity)
        self.assertEqual(self.lib.MarkerCore_Erase(option, len(option), C.byref(reply)), 1)
        return reply

    def erase_finish(self):
        reply = self.Reply()
        for _ in range(400):
            result = self.lib.MarkerCore_ErasePoll(C.byref(reply))
            if result != 1: return result, reply
        self.fail("erase preparation did not terminate")

    def test_unsupported_31_has_no_flash_permission_or_driver_side_effect(self):
        self.driver();self.lib.MarkerCore_SetSelection(0x6D000)
        before = self.lib.MarkerCore_FlashCrc()
        for option in (b"\x44" + struct.pack(">II", 0x5000, 0xF000),
                       b"\x44" + struct.pack(">II", 0x6D001, 0xF000),
                       b"\x44" + struct.pack(">II", 0x6D000, 0xE000),
                       b"\x44" + struct.pack(">II", 0x15000, 0x56000),
                       b"\x44" + struct.pack(">II", 0x7F000, 0x1000),
                       b"\x45" + struct.pack(">II", 0x6D000, 0xF000)):
            with self.subTest(option=option.hex()):
                reply = self.Reply()
                self.assertEqual(self.lib.MarkerCore_Erase(option, len(option), C.byref(reply)), 2)
                self.assertEqual(reply.nrc, 0x31)
                self.assertEqual(self.lib.MarkerCore_FlashCrc(), before)
                self.assertEqual([self.lib.MarkerCore_Get(x) for x in (1, 4, 9, 11)], [0, 0, 0, 0])

    def test_app_permission_is_revoked_before_first_actual_erase(self):
        self.driver();self.erase_start(0x15000, 0x57000)
        self.assertEqual(self.lib.MarkerCore_Get(10), 1)
        self.assertEqual(self.lib.MarkerCore_Get(11), 0)
        reply = self.Reply()
        self.assertEqual(self.lib.MarkerCore_ErasePoll(C.byref(reply)), 1)
        self.assertEqual(self.lib.MarkerCore_Get(10), 0)
        self.assertEqual(self.lib.MarkerCore_Get(11), 1)
        self.assertEqual(self.lib.MarkerCore_StartupRomSet(0x5000), 1)

    def test_failed_app_permission_change_does_not_erase_rom(self):
        self.driver();self.erase_start(0x15000, 0x57000)
        before = self.lib.MarkerCore_FlashCrc();self.lib.MarkerCore_SetFault(8)
        result, reply = self.erase_finish()
        self.assertEqual((result, reply.nrc), (2, 0x72))
        self.assertEqual(self.lib.MarkerCore_FlashCrc(), before)
        self.assertEqual(self.lib.MarkerCore_Get(10), 1)
        self.assertEqual(self.lib.MarkerCore_Get(11), 0)

    def test_boot_erase_restores_verified_untouched_backup_first(self):
        self.driver();self.lib.MarkerCore_SetSelection(0x6D000)
        self.erase_start(0x6D000, 0xF000)
        self.assertEqual(self.lib.MarkerCore_Get(0), 0x6D000)
        self.assertEqual(self.lib.MarkerCore_Get(1), 0)
        result, reply = self.erase_finish()
        self.assertEqual((result, reply.data[0]), (0, 0))
        self.assertEqual(self.lib.MarkerCore_Get(0), 0x5000)
        self.assertEqual(self.lib.MarkerCore_Get(1), 1)
        self.assertEqual(self.lib.MarkerCore_Get(11), 15)
        self.assertEqual(self.lib.MarkerCore_Get(10), 1)

    def test_bad_backup_or_failed_selection_prevents_erase(self):
        for fault in (1, 2, 5, 7):
            with self.subTest(fault=fault):
                self.setUp();self.driver();self.lib.MarkerCore_SetSelection(0x6D000)
                self.erase_start(0x6D000, 0xF000);self.lib.MarkerCore_SetFault(fault)
                result, reply = self.erase_finish()
                self.assertEqual((result, reply.nrc), (2, 0x72))
                self.assertEqual(self.lib.MarkerCore_Get(11), 0)
        self.setUp();self.driver();self.set_flash(0x5000, b"\0")
        self.erase_start(0x6D000, 0xF000)
        result, reply = self.erase_finish()
        self.assertEqual((result, reply.nrc), (2, 0x72))
        self.assertEqual(self.lib.MarkerCore_Get(11), 0)

    def test_retry_after_permission_commit_failure_and_ram_loss(self):
        for interrupt_after_erase in (False, True):
            with self.subTest(after_erase=interrupt_after_erase):
                self.setUp();self.driver();self.lib.MarkerCore_SetSelection(0x6D000)
                self.erase_start(0x6D000, 0xF000)
                if not interrupt_after_erase:self.lib.MarkerCore_SetFault(9)
                if interrupt_after_erase:
                    reply = self.Reply()
                    for _ in range(400):
                        self.lib.MarkerCore_ErasePoll(C.byref(reply))
                        if self.lib.MarkerCore_Get(11): break
                    self.assertEqual(self.lib.MarkerCore_Get(11), 1)
                else:
                    result, reply = self.erase_finish()
                    self.assertEqual((result, reply.nrc), (2, 0x72))
                    self.assertEqual(self.lib.MarkerCore_Get(11), 0)
                self.assertEqual(self.lib.MarkerCore_Get(0), 0x5000)
                self.lib.MarkerCore_SetFault(0)
                self.assertEqual(self.lib.MarkerCore_ResetJob(), 0)
                self.driver();self.erase_start(0x6D000, 0xF000)
                self.assertEqual(self.erase_finish()[0], 0)
                self.transfer(0x6D000, self.images["boot"])
                result, reply, _ = self.final_check()
                self.assertEqual((result, reply.data[0]), (0, 0))
                self.assertEqual(self.lib.MarkerCore_Get(0), 0x6D000)

    def test_app_retry_after_ram_loss_before_and_after_first_erase(self):
        for after_erase in (False, True):
            with self.subTest(after_erase=after_erase):
                self.setUp();self.driver();self.erase_start(0x15000, 0x57000)
                if after_erase:
                    reply = self.Reply()
                    self.assertEqual(self.lib.MarkerCore_ErasePoll(C.byref(reply)), 1)
                else:
                    self.lib.MarkerCore_SetFault(9)
                    self.assertEqual(self.erase_finish()[0], 2)
                self.assertEqual(self.lib.MarkerCore_Get(10), 0)
                self.assertEqual(self.lib.MarkerCore_Get(11), int(after_erase))
                self.lib.MarkerCore_SetFault(0)
                self.assertEqual(self.lib.MarkerCore_ResetJob(), 0)
                data = bytearray(self.images["app"])
                struct.pack_into("<II", data, 4096, 0x20001400, 0x16401)
                data = marker.stamp(bytes(data), "app", date="20261010")
                self.driver();self.erase_start(0x15000, 0x57000)
                self.assertEqual(self.erase_finish()[0], 0)
                self.transfer(0x15000, data)
                result, reply, _ = self.final_check()
                self.assertEqual((result, reply.data[0]), (0, 0))
                self.assertEqual(self.lib.MarkerCore_Get(10), 1)
                self.assertEqual(self.lib.MarkerCore_StartupApp(), 0)
                self.assertEqual(self.lib.MarkerCore_StartupRomSet(0x5000), 0)

    def test_wrong_stage_app_can_be_replaced_without_resetting_boot(self):
        self.driver();self.erase_start(0x15000, 0x57000)
        self.assertEqual(self.erase_finish()[0], 0)
        self.transfer(0x15000, image("app", stage="ST2"))
        result, reply, _ = self.final_check()
        self.assertEqual((result, reply.data[0]), (0, 1))
        self.assertEqual(self.lib.MarkerCore_Get(10), 0)
        self.assertEqual(self.lib.MarkerCore_Get(0), 0x5000)
        self.driver();self.erase_start(0x15000, 0x57000)
        self.assertEqual(self.erase_finish()[0], 0)
        self.transfer(0x15000, self.images["app"])
        result, reply, _ = self.final_check()
        self.assertEqual((result, reply.data[0]), (0, 0))
        self.assertEqual(self.lib.MarkerCore_Get(10), 1)

    def test_running_boot_b_is_preserved_before_erasing_a(self):
        self.lib.MarkerCore_SetRunning(0x6D000)
        self.set_flash(0x6D000, self.images["boot"])
        self.driver();before = self.lib.MarkerCore_FlashCrc()
        option = b"\x44" + struct.pack(">II", 0x6D000, 0xF000);reply = self.Reply()
        self.assertEqual(self.lib.MarkerCore_Erase(option, len(option), C.byref(reply)), 2)
        self.assertEqual(reply.nrc, 0x31)
        self.assertEqual(self.lib.MarkerCore_FlashCrc(), before)
        self.erase_start(0x5000, 0xF000)
        self.assertEqual(self.erase_finish()[0], 0)
        self.assertEqual(self.lib.MarkerCore_Get(0), 0x6D000)
        self.assertEqual(self.lib.MarkerCore_StartupRomSet(0x6D000), 0)

    def test_actual_core_success_and_independent_erase_length(self):
        self.download();result, reply, pending = self.final_check()
        self.assertEqual((result, reply.data[0]), (0, 0));self.assertGreater(pending, 100)
        self.assertEqual(self.lib.MarkerCore_Get(0), 0x6D000)
        self.assertEqual(self.lib.MarkerCore_Get(1), 1);self.assertEqual(self.lib.MarkerCore_Get(2), 1)
        self.assertEqual(self.lib.MarkerCore_Get(6), 0x57000)
        self.assertEqual(self.lib.MarkerCore_Get(7), len(self.images["app"]))
        self.assertEqual(self.lib.MarkerCore_Get(10), 1)

    def test_integrity_failure_never_selects_or_commits(self):
        for kind, address, offset, status in (("boot", 0x6D000, 0xEF00, 2),
                ("calib", 0x7E000, 0, 4), ("app", 0x15000, 4096, 1)):
            with self.subTest(kind=kind):
                self.setUp();self.download();bad = bytearray(self.images[kind]);bad[offset] ^= 1
                self.set_flash(address, bytes(bad));result, reply, _ = self.final_check()
                self.assertEqual((result, reply.data[0]), (0, status))
                self.assertEqual(self.lib.MarkerCore_Get(0), 0x5000)
                self.assertEqual(self.lib.MarkerCore_Get(1), 0);self.assertEqual(self.lib.MarkerCore_Get(2), 0)

    def test_stage_mismatch_never_selects_or_commits(self):
        self.download();self.set_flash(0x7E000, image("calib", "ST2"))
        result, reply, _ = self.final_check();self.assertEqual((result, reply.data[0]), (0, 4))
        self.assertEqual(self.lib.MarkerCore_Get(1), 0);self.assertEqual(self.lib.MarkerCore_Get(2), 0)

    def test_flash_read_failure_never_selects_or_commits(self):
        self.download();self.lib.MarkerCore_SetFault(1)
        result, reply, _ = self.final_check();self.assertEqual((result, reply.nrc), (2, 0x72))
        self.assertEqual(self.lib.MarkerCore_Get(1), 0);self.assertEqual(self.lib.MarkerCore_Get(2), 0)

    def test_corrupt_driver_ram_rejected_before_simulated_entry(self):
        self.driver(finish=False);self.lib.MarkerCore_SetRam(300, self.driver_data[300] ^ 1)
        reply = self.request(0x37, expected=2);self.assertEqual(reply.nrc, 0x72)
        self.assertEqual(self.lib.MarkerCore_Get(3), 0)

    def test_dd02_driver_uses_existing_placeholder(self):
        self.driver();reply = self.Reply();option = struct.pack(">III", 0x20001400, len(self.driver_data), 0)
        self.assertEqual(self.lib.MarkerCore_Security(option, len(option), C.byref(reply)), 0)
        self.assertEqual(reply.data[0], 0)

    def test_selection_io_failure_never_commits_app(self):
        self.download();self.lib.MarkerCore_SetFault(2)
        result, reply, _ = self.final_check();self.assertEqual((result, reply.nrc), (2, 0x72))
        self.assertEqual(self.lib.MarkerCore_Get(0), 0x5000);self.assertEqual(self.lib.MarkerCore_Get(2), 0)

    def test_post_selection_io_failure_is_reported_without_claiming_rollback(self):
        # Existing production order selects Boot, deinitializes Driver, then
        # commits App. A later IO failure is observable and is not atomic rollback.
        for fault in (3, 4, 5):
            with self.subTest(fault=fault):
                self.setUp();self.download();self.lib.MarkerCore_SetFault(fault)
                result, reply, _ = self.final_check();self.assertEqual((result, reply.nrc), (2, 0x72))
                self.assertEqual(self.lib.MarkerCore_Get(0), 0x6D000)
                self.assertEqual(self.lib.MarkerCore_Get(1), 1)


@unittest.skipUnless(os.environ.get("T008_NATIVE_MARKER_DLL"), "native ECU DLL not configured")
class NativeNonceStorageTests(unittest.TestCase):
    """Secret-free, synthetic flash tests of the actual persistent counter code."""
    @classmethod
    def setUpClass(cls):
        cls.lib = C.CDLL(os.environ["T008_NATIVE_MARKER_DLL"])
        cls.lib.NonceFixture_Reserve.argtypes = [C.c_uint32, C.c_uint32, C.POINTER(C.c_uint64)]
        cls.lib.NonceFixture_CutAfter.argtypes = [C.c_int]

    def setUp(self):
        self.lib.NonceFixture_Reset()

    def test_cross_role_resets_keep_reservations_monotonic(self):
        previous = 1024
        for n in range(100):
            first = C.c_uint64()
            self.assertEqual(self.lib.NonceFixture_Reserve(n & 1, 256, C.byref(first)), 0)
            self.assertEqual(first.value, previous + 1)
            previous = first.value + 255
        self.assertEqual(self.lib.NonceFixture_Outside(), 0)

    def test_torn_or_failed_commit_never_reuses_an_issued_range(self):
        for limit in (0, 40, 104, 111, 112):
            with self.subTest(written_before_failure=limit):
                self.setUp();first = C.c_uint64()
                self.assertEqual(self.lib.NonceFixture_Reserve(0, 256, C.byref(first)), 0)
                last_issued = first.value + 255
                self.lib.NonceFixture_CutAfter(limit)
                self.assertNotEqual(self.lib.NonceFixture_Reserve(1, 256, C.byref(first)), 0)
                self.assertEqual(first.value, 0)
                self.lib.NonceFixture_CutAfter(-1)
                self.assertEqual(self.lib.NonceFixture_Reserve(0, 256, C.byref(first)), 0)
                self.assertGreater(first.value, last_issued)
                self.assertEqual(self.lib.NonceFixture_Outside(), 0)


if __name__ == "__main__":
    unittest.main()
