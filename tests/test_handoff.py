# SPDX-License-Identifier: MIT
import ctypes
import hashlib
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

    def test_opt_in_cpu_nodes_preserve_flat_affinity_and_default(self):
        for args, count in ((b'-v',1),(b'cpus=1',1),(b'cpus=2',2),(b'cpus=4',4),
                            (b'cpus=20',1),(b'xcpus=4',1)):
            blob=bytearray(payload());blob[80:688]=args.ljust(608,b'\0')
            buf=ctypes.create_string_buffer(16384)
            size=self.lib.xnu_make_adt(buf,len(buf),ctypes.create_string_buffer(bytes(blob)),13000000)
            data=buf.raw[:size]
            def read_node(offset):
                n,c=struct.unpack_from('<II',data,offset);offset+=8;props={}
                for _ in range(n):
                    key=data[offset:offset+32].split(b'\0')[0]
                    length=struct.unpack_from('<I',data,offset+32)[0];offset+=36
                    props[key]=data[offset:offset+length];offset+=(length+3)&~3
                children=[]
                for _ in range(c):
                    child,offset=read_node(offset);children.append(child)
                return (props,children),offset
            tree,end=read_node(0);self.assertEqual(end,size)
            cpus=next(children for props,children in tree[1] if props[b'name']==b'cpus\0')
            self.assertEqual(len(cpus),count)
            for i,(props,children) in enumerate(cpus):
                self.assertEqual(children,[])
                self.assertEqual(struct.unpack('<I',props[b'reg'])[0],i)
                self.assertEqual(struct.unpack('<I',props[b'cpu-id'])[0],i)
                self.assertEqual(props[b'state'],b'running\0' if i==0 else b'stopped\0')

    def test_development_preboot_namespace_is_in_chosen(self):
        buf = ctypes.create_string_buffer(16384)
        size = self.lib.xnu_make_adt(buf, len(buf), ctypes.create_string_buffer(payload()), 13000000)
        data = buf.raw[:size]
        def read_node(offset):
            properties, children = struct.unpack_from('<II', data, offset); offset += 8
            props = {}
            for _ in range(properties):
                name = data[offset:offset+32].split(b'\0')[0]
                length = struct.unpack_from('<I', data, offset+32)[0]; offset += 36
                props[name] = data[offset:offset+length]; offset += (length+3)&~3
            nested = []
            for _ in range(children):
                child, offset = read_node(offset); nested.append(child)
            return (props,nested),offset
        tree, end = read_node(0)
        self.assertEqual(end, size)
        chosen = next(props for props,_ in tree[1] if props[b'name'] == b'chosen\0')
        self.assertEqual(chosen[b'boot-manifest-hash'], hashlib.sha384(b'A04BR3 development boot namespace v1').digest())

    def test_native_display_tree_is_opt_in(self):
        for args, expected in ((b'-v',False),(b'a04br3.display_probe=1',True),
                               (b'a04br3.display_probe=10',False),
                               (b'xa04br3.display_probe=1',False)):
            blob=bytearray(payload());blob[80:688]=args.ljust(608,b'\0')
            buf=ctypes.create_string_buffer(16384)
            size=self.lib.xnu_make_adt(buf,len(buf),ctypes.create_string_buffer(bytes(blob)),13000000)
            data=buf.raw[:size]
            def read_node(offset):
                n,c=struct.unpack_from('<II',data,offset);offset+=8;props={}
                for _ in range(n):
                    key=data[offset:offset+32].split(b'\0')[0]
                    length=struct.unpack_from('<I',data,offset+32)[0];offset+=36
                    props[key]=data[offset:offset+length];offset+=(length+3)&~3
                children=[]
                for _ in range(c):
                    child,offset=read_node(offset);children.append(child)
                return (props,children),offset
            tree,end=read_node(0);self.assertEqual(end,size)
            chosen=next(n[0] for n in tree[1] if n[0][b'name']==b'chosen\0')
            self.assertEqual(b'display-scale' in chosen,expected)
            if expected:
                self.assertEqual(struct.unpack('<I',chosen[b'display-scale'])[0],1)
                self.assertEqual(struct.unpack('<I',chosen[b'display-rotation'])[0],0)
            arm=next(n for n in tree[1] if n[0][b'name']==b'arm-io\0')
            displays=[n for n in arm[1] if n[0].get(b'device_type')==b'display-subsystem\0']
            self.assertEqual(len(displays),int(expected))
            if expected:
                props=displays[0][0]
                self.assertEqual(props[b'name'],b'disp0\0')
                self.assertEqual(struct.unpack('<I',props[b'canvas-width'])[0],800)
                self.assertEqual(struct.unpack('<I',props[b'canvas-height'])[0],1280)

    def test_full_donor_filesystem_relaxation_is_explicit(self):
        for args, expected in ((b'-v',False),(b'-v a04br3.full_root=1',True),(b'a04br3.full_root=10',False)):
            blob=bytearray(payload());blob[80:688]=args.ljust(608,b'\0')
            buf=ctypes.create_string_buffer(16384)
            size=self.lib.xnu_make_adt(buf,len(buf),ctypes.create_string_buffer(bytes(blob)),13000000)
            data=buf.raw[:size]
            self.assertEqual(b'lp-sip0' in data,expected)
            self.assertEqual(b'unique-chip-id' in data,expected)
            self.assertEqual(b'crypto-hash-method' in data,expected)
            if expected:
                at=data.index(b'unique-chip-id')
                self.assertEqual(struct.unpack_from('<IQ',data,at+32),(8,0x4130344252330001))
                at=data.index(b'chip-id'.ljust(32,b'\0'))
                self.assertEqual(struct.unpack_from('<II',data,at+32),(4,0x8167))

            if expected:
                at=data.index(b'lp-sip0');self.assertEqual(struct.unpack_from('<IQ',data,at+32),(8,2))
                at=data.index(b'lp-sip1');self.assertEqual(struct.unpack_from('<II',data,at+32),(4,1))

    def test_full_root_memory_spans_adjacent_real_fdt_banks_only(self):
        blob=bytearray(payload());struct.pack_into('<Q',blob,32,0x5e000000)
        f=self.fdt();f.memory_count=2
        f.memory[0].base=0x40000000;f.memory[0].size=0x40000000
        f.memory[1].base=0x80000000;f.memory[1].size=0x3e9c0000
        f.reserved_count=2;f.reserved[1].base=0xbe9c0000;f.reserved[1].size=0x1000000
        self.assertEqual(self.validate(bytes(blob),f)[0],0)
        f.memory[1].base+=0x4000;f.memory[1].size-=0x4000
        self.assertNotEqual(self.validate(bytes(blob),f)[0],0)
        f.memory[1].base=0x80000000;f.memory[1].size=0x3e9c0000
        f.reserved[1].base=0xa0000000
        self.assertNotEqual(self.validate(bytes(blob),f)[0],0)

    def test_plan_and_boot_args_reservation(self):
        result, plan = self.validate(payload())
        self.assertEqual(result, 0)
        self.assertEqual((plan.entry, plan.adt, plan.args, plan.top),
                         (0x61004000, 0x61008000, 0x6100c000, 0x61010000))

    def test_sparse_decoder_roundtrip_and_rejection(self):
        source = struct.pack('<I', 3) + b'ABC' + struct.pack('<I', 0x80000020) + struct.pack('<I', 2) + b'DE'
        f = self.lib.handoff_sparse
        f.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_uint64, ctypes.c_uint64]
        target = ctypes.create_string_buffer(37)
        self.assertEqual(f(target, source, len(source), 37), 0)
        self.assertEqual(target.raw, b'ABC' + bytes(32) + b'DE')
        for bad, size in ((source[:-1], 37), (source, 36), (source, 38), (bytes(4), 0), (b'X', 1)):
            self.assertNotEqual(f(None, bad, len(bad), size), 0)

    def test_v2_root_ramdisk_plan_and_adt(self):
        blob = bytearray(payload())
        struct.pack_into('<II', blob, 8, 2, 2)
        struct.pack_into('<Q', blob, 48, 0x6100c000)
        struct.pack_into('<4Q', blob, 720, 0x61008000, 0x4000, 1000, 24)
        result, plan = self.validate(bytes(blob))
        self.assertEqual(result, 0)
        self.assertEqual(plan.adt, 0x6100c000)
        buf = ctypes.create_string_buffer(16384)
        n = self.lib.xnu_make_adt(buf, len(buf), ctypes.create_string_buffer(bytes(blob)), 13000000)
        self.assertGreater(n, 0)
        pos = buf.raw.index(b'RAMDisk')
        self.assertEqual(struct.unpack_from('<2Q', buf.raw, pos + 36), (0x61008000, 0x4000))
        struct.pack_into('<Q', blob, 720, 0x61008008)
        self.assertNotEqual(self.validate(bytes(blob))[0], 0)

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
        chosen, cpus, arm_io, defaults = tree['children']
        self.assertEqual(defaults['name'], b'defaults\0')
        self.assertNotIn('pmap-max-asids', defaults)
        child, parent, span = struct.unpack('<3Q', arm_io['ranges'])
        reg, length = struct.unpack('<2Q', arm_io['children'][0]['reg'])
        self.assertEqual(parent + reg - child, 0x11005000)
        self.assertLessEqual(reg + length, child + span)
        gic = arm_io['children'][1]
        dist, dist_size, cpu_if, cpu_size = struct.unpack('<4Q', gic['reg'])
        self.assertEqual((parent + dist, parent + cpu_if), (0x10310000, 0x1032f000))
        self.assertEqual((dist_size, cpu_size), (0x1000, 0x2000))
        self.assertEqual(struct.unpack('<I', gic['virtual-timer-intid'])[0], 27)
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
