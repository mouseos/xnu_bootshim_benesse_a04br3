# SPDX-License-Identifier: BSD-2-Clause-Patent
"""v5 shim-only acceptance: exactly two opaque raw assets, no RAMDisk."""
import ctypes,struct,unittest
import test_handoff as legacy
from test_gpu_payload import tree


def payload5():
    # Synthetic input only: no embedded vendor firmware bytes.
    wmt=bytes(33);gpu=bytes(8193);data=bytearray(832+256)
    struct.pack_into('<8sII8Q608s',data,0,b'A04XNU01',5,3,0xfffffff006000000,0x60000000,0x18000000,0xfffffff007004000,0x61010000,688,832,0,b'-v rd=emmcroot0')
    struct.pack_into('<4Q',data,688,0x61004000,0x4000,832,256)
    struct.pack_into('<4Q',data,720,0x61008000,0x4000,len(data),len(wmt));data+=wmt+bytes(-len(wmt)%8)
    struct.pack_into('<4Q',data,752,0x6100c000,0x4000,len(data),len(gpu));data+=gpu+bytes(-len(gpu)%8)
    struct.pack_into('<Q',data,72,len(data));return data


class WMTHandoff(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        legacy.HandoffTests.setUpClass();cls.addClassCleanup(legacy.HandoffTests.tearDownClass)
        cls.check=legacy.HandoffTests();cls.lib=legacy.HandoffTests.lib

    def test_v5_exact_tail_and_actual_lengths(self):
        b=payload5();error,plan=self.check.validate(bytes(b));self.assertEqual(error,0)
        self.assertEqual((plan.adt,plan.args,plan.top),(0x61010000,0x61014000,0x61018000))
        adt=ctypes.create_string_buffer(16384);n=self.lib.xnu_make_adt(adt,len(adt),ctypes.create_string_buffer(bytes(b)),13000000)
        self.assertGreater(n,0);t,end=tree(adt.raw[:n]);self.assertEqual(end,n)
        chosen=next(x for x in t[1] if x[0][b'name']==b'chosen\0')
        props=next(x[0] for x in chosen[1] if x[0][b'name']==b'memory-map\0')
        self.assertEqual(struct.unpack('<2Q',props[b'WMTFirmware-A04BR3']),(0x61008000,33))
        self.assertEqual(struct.unpack('<2Q',props[b'GPUFirmware-A04BR3']),(0x6100c000,8193));self.assertNotIn(b'RAMDisk',props)

    def test_no_asset_entry_or_source_alias_and_no_extra_tail(self):
        b=payload5()
        for at,fmt,value in [(12,'<I',2),(40,'<Q',0xfffffff007008000),(40,'<Q',0xfffffff00700c000),
                             (720,'<Q',0x61008008),(728,'<Q',0x8000),(736,'<Q',832),(744,'<Q',31),
                             (752,'<Q',0x61010000),(768,'<Q',1088),(776,'<Q',8192)]:
            bad=bytearray(b);struct.pack_into(fmt,bad,at,value)
            with self.subTest(at=at,value=value):self.assertNotEqual(self.check.validate(bytes(bad))[0],0)
        for root in [b'rd=md0',b'rd=emmcroot0 rd=emmcroot0',b'rd=emmcroot0\trd=md0']:
            bad=bytearray(b);bad[80:688]=root.ljust(608,b'\0');self.assertNotEqual(self.check.validate(bytes(bad))[0],0)
        bad=b+bytes(8);struct.pack_into('<Q',bad,72,len(bad));self.assertNotEqual(self.check.validate(bytes(bad))[0],0)

if __name__=='__main__':unittest.main()
