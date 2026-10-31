"""Regression test for corrupt NSIS release binaries.

Run: python3 -m unittest discover -s tests -p test_windows_packaging.py
"""
import importlib.util
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("package_windows_cross", REPO / "scripts" / "package_windows_cross.py")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


@unittest.skipUnless(shutil.which("makensis"), "NSIS compiler is not installed")
class InstallerCRCRegression(unittest.TestCase):
    def test_valid_and_modified_binary(self):
        with tempfile.TemporaryDirectory(prefix="oidm-crc-test-") as directory:
            root = Path(directory)
            installer = root / "smoke.exe"
            script = root / "smoke.nsi"
            script.write_text(
                f'Unicode true\nName "OIDM CRC smoke"\nOutFile "{installer}"\n'
                'CRCCheck force\nSetCompressor /SOLID zlib\n'
                'Section\nDetailPrint "CRC regression"\nSectionEnd\n',
                encoding="utf-8",
            )
            subprocess.run(["makensis", "-V2", str(script)], check=True)
            self.assertEqual(len(module.verify_nsis_crc(installer)), 8)
            modified = root / "corrupt.exe"
            binary = bytearray(installer.read_bytes())
            binary[-18] ^= 0x01
            modified.write_bytes(binary)
            with self.assertRaisesRegex(ValueError, "NSIS integrity failure"):
                module.verify_nsis_crc(modified)


if __name__ == "__main__":
    unittest.main()
