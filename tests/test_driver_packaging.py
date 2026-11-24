"""Filesystem-only regression for the release's device runtime closure."""
import sys
sys.dont_write_bytecode = True
import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
spec=importlib.util.spec_from_file_location("package_release",Path(__file__).resolve().parents[1]/"scripts/package_release.py")
package=importlib.util.module_from_spec(spec)
spec.loader.exec_module(package)

class PackagingTest(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory()
        self.root=Path(self.temp.name)
        entries=[]
        for vendor,names in package.DRIVER_FILES.items():
            for name in names:
                relative=f"{vendor}/x64/{name}"
                path=self.root/"driver"/relative
                path.parent.mkdir(parents=True,exist_ok=True)
                path.write_bytes(name.encode())
                entries.append({"path":relative,"sha256":hashlib.sha256(path.read_bytes()).hexdigest()})
        (self.root/"driver/runtime-manifest.json").write_text(json.dumps({"libraries":entries}),encoding="utf-8")
    def tearDown(self):
        self.temp.cleanup()
    def test_complete_vendor_closure(self):
        self.assertEqual(len(package.validate_driver_runtime(self.root)["libraries"]),10)
    def test_missing_transitive_runtime(self):
        (self.root/"driver/tosun/x64/libTSH.dll").unlink()
        with self.assertRaisesRegex(ValueError,"Missing required device runtime"):
            package.validate_driver_runtime(self.root)
    def test_wrong_or_stale_dll(self):
        (self.root/"driver/vector/x64/vxlapi64.dll").write_bytes(b"changed")
        with self.assertRaisesRegex(ValueError,"provenance/hash mismatch"):
            package.validate_driver_runtime(self.root)
    def test_legacy_layout_does_not_satisfy_package(self):
        (self.root/"driver/runtime-manifest.json").unlink()
        with self.assertRaisesRegex(ValueError,"Missing runtime provenance manifest"):
            package.validate_driver_runtime(self.root)

if __name__=="__main__":unittest.main()
