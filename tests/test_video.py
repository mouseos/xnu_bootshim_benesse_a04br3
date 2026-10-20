# SPDX-License-Identifier: MIT
import ctypes
import unittest
import test_handoff as handoff
from test_fdt import Info

class Video(ctypes.Structure):
    _fields_ = [(n,ctypes.c_uint64) for n in ('base','display','rowbytes','width','height','depth')]

class VideoTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        handoff.HandoffTests.setUpClass()
        cls.fn = handoff.HandoffTests.lib.handoff_video_setup
        cls.fn.argtypes = [ctypes.POINTER(Video),ctypes.POINTER(Info),ctypes.c_void_p]

    @classmethod
    def tearDownClass(cls):
        handoff.HandoffTests.tearDownClass()

    def setup(self, flag, reserved=True, size=0x1000000):
        blob=bytearray(handoff.payload());blob[80:688]=flag.encode().ljust(608,b'\0')
        f=Info();f.reserved_count=int(reserved);f.reserved[0].base=0xbe9c0000;f.reserved[0].size=size
        v=Video();v.base=0xdeadbeef
        r=self.fn(ctypes.byref(v),ctypes.byref(f),ctypes.create_string_buffer(bytes(blob)))
        return r,v

    def test_reserved_geometry(self):
        r,v=self.setup('-v a04br3.display_probe=1 cpus=1')
        self.assertEqual(r,1)
        self.assertEqual((v.base,v.rowbytes,v.width,v.height,v.depth),(0xbe9c0000,3200,800,1280,32))
        self.assertLessEqual(v.rowbytes*v.height*3,0x1000000)

    def test_opt_in_and_protected_extent(self):
        for flag,reserved,size in [('-v',True,0x1000000),('a04br3.display_probe=10',True,0x1000000),('xa04br3.display_probe=1',True,0x1000000),('a04br3.display_probe=1',False,0),('a04br3.display_probe=1',True,800*1280*4)]:
            with self.subTest(flag=flag,reserved=reserved,size=size):
                r,v=self.setup(flag,reserved,size)
                self.assertEqual(r,0);self.assertEqual(v.base,0)
