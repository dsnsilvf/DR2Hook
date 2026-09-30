"""Testes sem os arquivos do jogo: tudo é gerado em memória."""

import struct
import unittest

from tools.egodata import bxml
from tools.egodata.aes import decrypt_ecb
from tools.egodata.nefs import MAGIC, VERSION_2, embedded_headers


class AesTest(unittest.TestCase):
    def test_fips197_aes256(self):
        key = bytes(range(32))
        cipher = bytes.fromhex("8ea2b7ca516745bfeafc49904b496089")
        self.assertEqual(decrypt_ecb(cipher, key), bytes.fromhex("00112233445566778899aabbccddeeff"))

    def test_trailing_bytes_pass_through(self):
        key = bytes(range(32))
        cipher = bytes.fromhex("8ea2b7ca516745bfeafc49904b496089") + b"abc"
        self.assertEqual(decrypt_ecb(cipher, key)[-3:], b"abc")


class BxmlTest(unittest.TestCase):
    TREE = [
        "screens", None, [],
        [
            ["Screen", None, [("id", "pause_menu"), ("object", "smart_hub")], [
                ["items", None, [], [
                    ["Item", None, [("id", "item_9")], [
                        ["BTextStatic", None, [("string", "lng_vr_reset_view"), ("glyph", "text_title")], []],
                    ]],
                ]],
                ["SBGridItemFlow", "\r\n item_0\r\n item_9\r\n", [("wrapV", "true")], []],
            ]],
            ["Screen", None, [("id", "options_ingame")], []],
        ],
    ]

    def test_roundtrip_is_stable(self):
        data = bxml.encode(self.TREE)
        self.assertEqual(bxml.decode(data), self.TREE)
        self.assertEqual(bxml.encode(bxml.decode(data)), data)

    def test_string_table_is_aligned(self):
        data = bxml.encode(self.TREE)
        kind, _ = struct.unpack_from("<II", data, 8)
        self.assertEqual(kind, bxml.STRING_TABLE)
        strings_kind, strings_len = struct.unpack_from("<II", data, 16)
        self.assertEqual(strings_kind, bxml.STRINGS)
        self.assertEqual(strings_len % 16, 0)

    def test_rejects_other_formats(self):
        with self.assertRaises(ValueError):
            bxml.decode(b"PSSG" + b"\0" * 12)

    def test_xml_shows_attributes_and_text(self):
        text = bxml.to_xml(self.TREE)
        self.assertIn('<Screen id="pause_menu" object="smart_hub">', text)
        self.assertIn("item_0 item_9", text)


class EmbeddedHeaderTest(unittest.TestCase):
    def _header(self, size=0x100):
        h = bytearray(size)
        h[0:4] = MAGIC
        h[0x24:0x64] = b"0" * 64
        struct.pack_into("<II", h, 0x64, size, VERSION_2)
        return bytes(h)

    def test_finds_only_valid_headers(self):
        junk = MAGIC + b"\xff" * 0x200
        exe = b"\x90" * 16 + junk + self._header() + b"\xcc" * 8
        found = list(embedded_headers(exe))
        self.assertEqual(len(found), 1)
        self.assertEqual(found[0][:4], MAGIC)


if __name__ == "__main__":
    unittest.main()
