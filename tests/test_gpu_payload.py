# SPDX-License-Identifier: BSD-2-Clause-Patent
import ctypes
import os
from pathlib import Path
import struct
import unittest
import test_handoff as legacy
from test_handoff import payload


def firmware_payload(firmware=bytes(8192)):
    b=bytearray(payload())
    b+=firmware+bytes(-len(firmware)%8)
    struct.pack_into('<II', b, 8, 4, 2)
    struct.pack_into('<Q', b, 48, 0x61008000+((len(firmware)+0x3fff)&~0x3fff))
    struct.pack_into('<Q', b, 72, len(b))
    b[80:688]=b'-v rd=emmcroot0'.ljust(608,b'\0')
    struct.pack_into('<4Q', b, 720, 0x61008000,
                     (len(firmware)+0x3fff)&~0x3fff, 1024, len(firmware))
    return b


def tree(data, at=0):
    n,c=struct.unpack_from('<II', data, at);at+=8;props={};children=[]
    for _ in range(n):
        name=data[at:at+32].split(b'\0')[0]
        length=struct.unpack_from('<I',data,at+32)[0];at+=36
        props[name]=data[at:at+length];at+=(length+3)&~3
    for _ in range(c):
        child,at=tree(data,at);children.append(child)
    return (props,children),at


class GPUHandoffTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        legacy.HandoffTests.setUpClass()
        cls.addClassCleanup(legacy.HandoffTests.tearDownClass)
        cls.checker=legacy.HandoffTests()
        cls.lib=legacy.HandoffTests.lib

    def adt(self, blob):
        out=ctypes.create_string_buffer(16384)
        n=self.lib.xnu_make_adt(out,len(out),ctypes.create_string_buffer(bytes(blob)),13000000)
        self.assertGreater(n,0)
        result,end=tree(out.raw[:n]);self.assertEqual(end,n)
        chosen=next(node for node in result[1] if node[0][b'name']==b'chosen\0')
        return next(node[0] for node in chosen[1] if node[0][b'name']==b'memory-map\0')

    def test_v4_tail_reservation_and_exact_file_length_adt(self):
        b=firmware_payload(bytes(8193))
        result,plan=self.checker.validate(bytes(b))
        self.assertEqual(result,0)
        self.assertEqual((plan.adt,plan.args,plan.top), (0x6100c000,0x61010000,0x61014000))
        memory=self.adt(b)
        self.assertEqual(struct.unpack('<2Q',memory[b'GPUFirmware-A04BR3']), (0x61008000,8193))
        self.assertEqual(struct.unpack('<2Q',memory[b'Kernel-A04BR3']), (0x60000000,0x100c000))
        self.assertNotIn(b'RAMDisk',memory)

    def test_v4_entry_source_size_tail_and_root_bounds_rejected(self):
        base=firmware_payload()
        for offset,value,fmt in ((8,5,'<I'),(12,1,'<I'),(48,0x61008000,'<Q'),
                                (720,0x61008008,'<Q'),(720,0x61000000,'<Q'),
                                (728,0x8000,'<Q'),(736,768,'<Q'),
                                (744,0,'<Q'),(744,4096,'<Q'),(744,0x2000001,'<Q'),
                                (744,8184,'<Q'),
                                (40,0xfffffff007008000,'<Q')):
            bad=bytearray(base);struct.pack_into(fmt,bad,offset,value)
            with self.subTest(offset=offset,value=value):
                self.assertNotEqual(self.checker.validate(bytes(bad))[0],0)
        for command in (b'-v',b'rd=md0',b'rd=emmcroot00',b'rd=emmcroot0 rd=md0',
                        b'rd=emmcroot0 rd=emmcroot0',b'xrd=emmcroot0'):
            bad=bytearray(base);bad[80:688]=command.ljust(608,b'\0')
            self.assertNotEqual(self.checker.validate(bytes(bad))[0],0)
        for length in (0,687,1024,len(base)-1):
            self.assertNotEqual(self.checker.validate(bytes(base[:length]))[0],0)
        bad=bytearray(base)+bytes(8);struct.pack_into('<Q',bad,72,len(bad))
        self.assertNotEqual(self.checker.validate(bytes(bad))[0],0)

    def test_v1_v2_v3_stay_valid_and_only_ramdisks_for_v2_v3(self):
        for version in (1,2,3):
            b=bytearray(payload())
            if version!=1:
                struct.pack_into('<II',b,8,version,2)
                struct.pack_into('<Q',b,48,0x6100c000)
                struct.pack_into('<4Q',b,720,0x61008000,0x4000,1000,4 if version==3 else 24)
                if version==3: struct.pack_into('<I',b,1000,0x80004000)
            self.assertEqual(self.checker.validate(bytes(b))[0],0)
            memory=self.adt(b)
            self.assertNotIn(b'GPUFirmware-A04BR3',memory)
            self.assertEqual(b'RAMDisk' in memory,version in (2,3))

    def test_external_real_firmware_is_reserved_and_published_as_actual_size(self):
        root=Path(__file__).resolve().parents[4]
        path=Path(os.environ.get('A04_RGX_FIRMWARE',str(root/
            'port/gsi/vendor/benesse/a04br3/proprietary/vendor/firmware/rgx.fw.22.40.54.30')))
        if not path.is_file():
            if 'A04_RGX_FIRMWARE' in os.environ:self.fail('explicit firmware missing')
            self.skipTest('external firmware absent')
        raw=path.read_bytes();b=firmware_payload(raw)
        self.assertEqual(self.checker.validate(bytes(b))[0],0)
        self.assertEqual(struct.unpack('<2Q',self.adt(b)[b'GPUFirmware-A04BR3']),
                         (0x61008000,len(raw)))
