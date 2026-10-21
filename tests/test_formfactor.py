# SPDX-License-Identifier: MIT
"""Verify the generated ADT keeps real hardware identity for the tablet UI."""
import ctypes
import struct
import unittest
import test_handoff as handoff


def read_tree(data, offset=0):
    count, children = struct.unpack_from("<II", data, offset)
    offset += 8
    properties = {}
    for _ in range(count):
        key = data[offset:offset + 32].split(b"\0")[0]
        size = struct.unpack_from("<I", data, offset + 32)[0]
        offset += 36
        properties[key] = data[offset:offset + size]
        offset += (size + 3) & ~3
    nested = []
    for _ in range(children):
        node, offset = read_tree(data, offset)
        nested.append(node)
    return (properties, nested), offset


class FormFactorTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        handoff.HandoffTests.setUpClass()
        cls.fn = handoff.HandoffTests.lib.xnu_make_adt

    @classmethod
    def tearDownClass(cls):
        handoff.HandoffTests.tearDownClass()

    def test_hardware_model_and_compatibles_survive_ui_product(self):
        for args in (b"-v", b"a04br3.full_root=1 a04br3.display_probe=1 cpus=4"):
            payload = bytearray(handoff.payload())
            payload[80:688] = args.ljust(608, b"\0")
            buffer = ctypes.create_string_buffer(16384)
            size = self.fn(buffer, len(buffer), ctypes.create_string_buffer(bytes(payload)), 13000000)
            self.assertGreater(size, 0)
            (root, _), end = read_tree(buffer.raw[:size])
            self.assertEqual(end, size)
            self.assertEqual(root[b"model"], b"Benesse TAB-A04-BR3\0")
            self.assertEqual(root[b"compatible"], b"benesse,a04br3\0mediatek,mt8167\0")
            self.assertEqual(root[b"device_type"], b"device-tree\0")
            # UI product is published by the expert, not by replacing the board.
            self.assertNotIn(b"iPad", buffer.raw[:size])
            self.assertNotIn(b"mt6630-wlan", buffer.raw[:size])


if __name__ == "__main__":
    unittest.main()
