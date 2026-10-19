# SPDX-License-Identifier: MIT
import ctypes
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest
from test_fdt import Info

ROOT = Path(__file__).resolve().parents[1]


class Plan(ctypes.Structure):
    _fields_ = [('adt', ctypes.c_uint64), ('args', ctypes.c_uint64),
                ('top', ctypes.c_uint64), ('entry', ctypes.c_uint64)]


def payload():
    data = bytearray(1024)
    struct.pack_into('<8sII8Q608s', data, 0, b'A04XNU01', 1, 1,
                     0xfffffff006000000, 0x60000000, 0x18000000,
                     0xfffffff007004000, 0x61008000, 688, 768, 1024, b'-v cpus=1')
    struct.pack_into('<4Q', data, 688, 0x61004000, 0x4000, 768, 256)
    return bytes(data)


class HandoffTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix='a04br3-handoff-test-')
        lib = Path(cls.temp.name) / 'handoff.so'
        subprocess.run(['cc', '-shared', '-fPIC', '-O2', '-Wall', '-Wextra', '-Werror',
                        '-DHANDOFF_HOST_TEST', str(ROOT / 'handoff.c'), str(ROOT / 'adt.c'),
                        '-o', str(lib)], check=True)
        cls.lib = ctypes.CDLL(str(lib))
        cls.lib.handoff_validate.argtypes = [ctypes.c_void_p, ctypes.c_size_t, ctypes.c_uint64,
                                            ctypes.POINTER(Info), ctypes.c_uint64, ctypes.c_uint64,
                                            ctypes.c_uint64, ctypes.POINTER(Plan)]
        cls.lib.xnu_make_adt.argtypes = [ctypes.c_void_p, ctypes.c_size_t, ctypes.c_void_p, ctypes.c_uint32]
        cls.lib.xnu_make_adt.restype = ctypes.c_size_t

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def fdt(self):
        f = Info(); f.bytes = 0x10000; f.memory_count = 1
        f.memory[0].base = 0x40000000; f.memory[0].size = 0x40000000
        f.reserved_count = 1; f.reserved[0].base = 0x43000000; f.reserved[0].size = 0x30000
        return f

    def validate(self, blob, fdt=None):
        plan = Plan(); fdt = fdt or self.fdt()
        result = self.lib.handoff_validate(ctypes.create_string_buffer(blob), len(blob), 0x47000000,
                                           ctypes.byref(fdt), 0x4e000000, 0x40080000, 0x400a0000,
                                           ctypes.byref(plan))
        return result, plan

    def test_plan_and_boot_args_reservation(self):
        result, plan = self.validate(payload())
        self.assertEqual(result, 0)
        self.assertEqual((plan.entry, plan.adt, plan.args, plan.top),
                         (0x61004000, 0x61008000, 0x6100c000, 0x61010000))

    def test_kernel_outside_39_bit_bootstrap_range_rejected(self):
        bad = bytearray(payload())
        struct.pack_into('<Q', bad, 16, 0xfffffe0006000000)
        struct.pack_into('<Q', bad, 40, 0xfffffe0007004000)
        self.assertNotEqual(self.validate(bytes(bad))[0], 0)

    def test_reserved_and_unknown_memory_rejected(self):
        f = self.fdt(); f.reserved[0].base = 0x61000000
        self.assertNotEqual(self.validate(payload(), f)[0], 0)
        f = self.fdt(); f.memory[0].size = 0x20000000
        self.assertNotEqual(self.validate(payload(), f)[0], 0)

    def test_corrupt_payload_and_destination_rejected(self):
        for offset, value in ((24, 0x40000000), (32, 0x1000), (40, 0),
                              (48, 0x61004000), (56, 0xffffffffffffffff),
                              (64, 700), (72, 1023), (688, 0x43000000),
                              (704, 0xfffffffffffffff8), (712, 1024)):
            bad = bytearray(payload()); struct.pack_into('<Q', bad, offset, value)
            with self.subTest(offset=offset):
                self.assertNotEqual(self.validate(bytes(bad))[0], 0)
        for length in (0, 687, 1023):
            self.assertNotEqual(self.validate(payload()[:length])[0], 0)

    def test_adt_tree_and_output_bounds(self):
        source = ctypes.create_string_buffer(payload())
        output = ctypes.create_string_buffer(16384)
        size = self.lib.xnu_make_adt(output, 16384, source, 13000000)
        self.assertGreater(size, 0)
        def parse(blob, at=0):
            props, children = struct.unpack_from('<II', blob, at); at += 8; node = {}
            for _ in range(props):
                name = blob[at:at + 32].split(b'\0')[0].decode(); length = struct.unpack_from('<I', blob, at + 32)[0]
                at += 36; node[name] = blob[at:at + length]; at += (length + 3) & ~3
            node['children'] = []
            for _ in range(children):
                child, at = parse(blob, at); node['children'].append(child)
            return node, at
        tree, end = parse(output.raw[:size]); self.assertEqual(end, size)
        self.assertEqual(tree['model'], b'Benesse TAB-A04-BR3\0')
        chosen, cpus, arm_io = tree['children']
        child, parent, span = struct.unpack('<3Q', arm_io['ranges'])
        reg, length = struct.unpack('<2Q', arm_io['children'][0]['reg'])
        self.assertEqual(parent + reg - child, 0x11005000)
        self.assertLessEqual(reg + length, child + span)
        self.assertEqual(struct.unpack('<Q', chosen['dram-base'])[0], 0x60000000)
        self.assertEqual(struct.unpack('<I', cpus['children'][0]['timebase-frequency'])[0], 13000000)
        # Synthetic host fixture only: no such seed is built into boot images.
        seeded = self.lib.xnu_make_adt_seeded
        seeded.argtypes = [ctypes.c_void_p, ctypes.c_size_t, ctypes.c_void_p,
                           ctypes.c_uint32, ctypes.c_void_p, ctypes.c_size_t]
        seeded.restype = ctypes.c_size_t
        seed = ctypes.create_string_buffer(bytes(range(64)))
        seeded_size = seeded(output, 16384, source, 13000000, seed, 64)
        seeded_tree, seeded_end = parse(output.raw[:seeded_size])
        self.assertEqual(seeded_end, seeded_size)
        self.assertEqual(seeded_tree['children'][0]['random-seed'], bytes(range(64)))
        self.assertEqual(seeded(output, 16384, source, 13000000, seed, 63), 0)
        for capacity in (0, 1, size - 1):
            canary = ctypes.create_string_buffer(b'\xa5' * 16384)
            self.assertEqual(self.lib.xnu_make_adt(canary, capacity, source, 13000000), 0)
            self.assertEqual(canary.raw[capacity:16384], b'\xa5' * (16384 - capacity))


if __name__ == '__main__':
    unittest.main()
