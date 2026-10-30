"""Core discovery must follow the reference interpreter on mixed-ABI hosts."""

import os
from pathlib import Path
import sys
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parent / "fixtures" / "space"))
import libretro


class CoreDiscoveryTests(unittest.TestCase):
    def test_mixed_architectures(self):
        for native, foreign in [("x86_64-linux-gnu", "aarch64-linux-gnu"),
                                ("aarch64-linux-gnu", "x86_64-linux-gnu")]:
            with self.subTest(native=native):
                installed = {
                    Path(root) / abi / "libretro" / filename
                    for root in ["/usr/lib", "/usr/local/lib"]
                    for abi in [native, foreign]
                    for filename in libretro.CORE_NAMES.values()
                }
                # Present foreign directories first, as an unrestricted glob may.
                def glob(root, pattern):
                    return iter(Path(root) / abi / "libretro" for abi in [foreign, native])

                with patch.dict(os.environ, {}, clear=True), \
                     patch.object(libretro.sysconfig, "get_config_var", return_value=native), \
                     patch.object(Path, "glob", glob), \
                     patch.object(Path, "is_file", lambda path: path in installed):
                    expected = {system: Path("/usr/lib") / native / "libretro" / filename
                                for system, filename in libretro.CORE_NAMES.items()}
                    self.assertEqual(libretro.reference_cores(), expected)
                    installed.difference_update(expected.values())
                    self.assertEqual(libretro.reference_cores(), {
                        system: Path("/usr/local/lib") / native / "libretro" / filename
                        for system, filename in libretro.CORE_NAMES.items()
                    })

    def test_override_and_standard_directory_precedence(self):
        with patch.dict(os.environ, {"PROTO_TIME_LIBRETRO_DIR": "/custom/cores"}), \
             patch.object(Path, "is_file", return_value=True):
            self.assertEqual(libretro.reference_cores(), {
                system: Path("/custom/cores") / filename
                for system, filename in libretro.CORE_NAMES.items()
            })
        with patch.dict(os.environ, {}, clear=True), \
             patch.object(Path, "is_file", return_value=True):
            self.assertEqual(libretro.reference_cores(), {
                system: Path("/usr/lib/libretro") / filename
                for system, filename in libretro.CORE_NAMES.items()
            })

    def test_unknown_abi_does_not_search_foreign_directories(self):
        with patch.dict(os.environ, {}, clear=True), \
             patch.object(libretro.sysconfig, "get_config_var", return_value=None), \
             patch.object(Path, "glob", return_value=iter([Path("/usr/lib/foreign/libretro")])), \
             patch.object(Path, "is_file", lambda path: "foreign" in path.parts):
            self.assertEqual(libretro.reference_cores(), {"gb": None, "gg": None})


if __name__ == "__main__":
    unittest.main()
