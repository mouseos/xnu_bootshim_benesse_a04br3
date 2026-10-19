# SPDX-License-Identifier: MIT
import ctypes
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class Range(ctypes.Structure):
    _fields_ = [('base', ctypes.c_uint64), ('size', ctypes.c_uint64)]


class Info(ctypes.Structure):
    _fields_ = [('bytes', ctypes.c_uint32), ('memory_count', ctypes.c_uint),
                ('reserved_count', ctypes.c_uint), ('memory', Range * 8),
                ('reserved', Range * 64), ('initrd_start', ctypes.c_uint64),
                ('initrd_end', ctypes.c_uint64)]


class FdtTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix='a04br3-fdt-test-')
        out = Path(cls.temp.name)
        subprocess.run(['cc', '-shared', '-fPIC', '-O2', '-Wall', '-Wextra', '-Werror',
                        str(ROOT / 'fdt.c'), '-o', str(out / 'fdt.so')], check=True)
        subprocess.run(['dtc', '-q', '-I', 'dts', '-O', 'dtb', '-o', str(out / 'fixture.dtb'),
                        str(ROOT / 'tests/fdt-fixture.dts')], check=True)
        cls.blob = (out / 'fixture.dtb').read_bytes()
        cls.lib = ctypes.CDLL(str(out / 'fdt.so'))
        cls.lib.fdt_inspect.argtypes = [ctypes.c_void_p, ctypes.c_size_t, ctypes.POINTER(Info)]
        cls.lib.fdt_inspect.restype = ctypes.c_int

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def inspect(self, blob):
        info = Info()
        result = self.lib.fdt_inspect(ctypes.create_string_buffer(blob), len(blob), ctypes.byref(info))
        return result, info

    def test_ranges_and_initrd(self):
        result, info = self.inspect(self.blob)
        self.assertEqual(result, 0)
        self.assertEqual([(r.base, r.size) for r in info.memory[:info.memory_count]],
                         [(0x40000000, 0x40000000), (0x80000000, 0x3f9c0000)])
        self.assertEqual([(r.base, r.size) for r in info.reserved[:info.reserved_count]],
                         [(0x43000000, 0x30000), (0x44410000, 0xe0000)])
        self.assertEqual((info.initrd_start, info.initrd_end), (0x47000000, 0x47001000))

    def test_every_truncation_is_rejected(self):
        for length in range(len(self.blob)):
            self.assertNotEqual(self.inspect(self.blob[:length])[0], 0)

    def test_header_bounds_and_version(self):
        for offset, value in ((0, 0), (4, 0xffffffff), (8, 0xfffffff0),
                              (12, 0xffffffff), (16, 0xffffffff), (20, 16),
                              (32, 0xffffffff), (36, 0xffffffff)):
            bad = bytearray(self.blob)
            struct.pack_into('>I', bad, offset, value)
            with self.subTest(offset=offset):
                self.assertNotEqual(self.inspect(bytes(bad))[0], 0)

    def test_unsupported_memory_cells_are_rejected(self):
        bad = self.blob.replace(struct.pack('>4I', 0, 0x40000000, 0, 0x40000000),
                                struct.pack('>4I', 0xffffffff, 0xffffffff, 0, 0x40000000), 1)
        self.assertNotEqual(self.inspect(bad)[0], 0)  # wrapping physical range


if __name__ == '__main__':
    unittest.main()
