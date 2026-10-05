"""Testes sem os arquivos do jogo: tudo é gerado em memória."""

import struct
import unittest

from tools.egodata import bxml, cfgxml, lng
from tools.egodata.aes import decrypt_ecb
from tools.egodata.nefs import MAGIC, VERSION_2, decrypt_nefs_intro, embedded_headers


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


class IntroTest(unittest.TestCase):
    # Primeiros 128 bytes de cars/037.nefs. Não é o pacote: só o intro embaralhado.
    CIPHER = bytes.fromhex(
        "0a2338c5b015a974c842c04822b3f59f"
        "a14ba15ede156a6c55e4eabd4356089d"
        "dd1ca5ad2f761cb8d25015252a0ff5cb"
        "2738f22f040ea3dc68b475f953cd8a4b"
        "98f93a908fcb1d6bf629ab7439a80e04"
        "f83529a74940695462cb4c09b5027260"
        "657b023d11d7d17bb69acb974aa89f6f"
        "300ca6db3289291554d8b1e0aaff9a5a"
    )

    def test_dirt_rally_2_intro_becomes_nefs(self):
        intro = decrypt_nefs_intro(self.CIPHER)
        self.assertEqual(intro[:4], MAGIC)
        size, version = struct.unpack_from("<II", intro, 0x64)
        self.assertEqual(version, VERSION_2)
        self.assertEqual(size, 24960)
        self.assertEqual(intro[0x24:0x64], b"928AB01E02903E19C13B61D856DD134D4845959D4DC31C55AD218A81394BAB65")

    def test_plain_intro_passes_through(self):
        raw = MAGIC + b"\x00" * 124
        self.assertEqual(decrypt_nefs_intro(raw), raw)

    def test_garbage_is_rejected(self):
        with self.assertRaises(ValueError):
            decrypt_nefs_intro(b"\x01" * 128)


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


class LngTest(unittest.TestCase):
    def test_decode(self):
        keys = b"lng_a\0lng_b\0"
        values = "Olá\0B\0".encode("utf-8")
        pairs = struct.pack(">I", 2) + struct.pack(">IIII", 0, 0, 6, 5)
        data = (b"LNGT" + struct.pack(">I", 0) + b"SIDA" + struct.pack(">I", len(pairs)) + pairs
                + b"SIDB" + struct.pack(">I", len(keys)) + keys
                + b"LNGB" + struct.pack(">I", len(values)) + values)
        self.assertEqual(lng.decode(data), {"lng_a": "Olá", "lng_b": "B"})


class CfgXmlTest(unittest.TestCase):
    @staticmethod
    def _element(name, attrs, children=b"", text=b""):
        head = bytes([len(attrs)]) + name + b"\0" + b"".join(k + b"\0" + v + b"\0" for k, v in attrs)
        return b"\0" + struct.pack("<I", 0) + head + children + text + b"\0\x04" + struct.pack("<I", 5)

    def test_decode(self):
        leaf = self._element(b"style", [(b"id", b"default"), (b"height", b"34")])
        texted = self._element(b"note", [], text=b"oi")
        data = b"\0BXML" + self._element(b"xml", [], leaf + texted)
        self.assertEqual(cfgxml.decode(data), [
            "xml", None, [], [
                ["style", None, [("id", "default"), ("height", "34")], []],
                ["note", "oi", [], []],
            ]])


    def test_decode_v1(self):
        def element(name, attrs, children=b""):
            head = struct.pack("<I", 0) + bytes([len(attrs), 0]) + name + b"\0"
            head += b"".join(k + b"\0" + v + b"\0" for k, v in attrs)
            return head + children + b"\x04\0" + struct.pack("<I", 5)
        title = element(b"title", [(b"value", b"lng_t")])
        data = b"\1BXML" + element(b"messages", [], element(b"message", [(b"id", b"m")], title))
        self.assertEqual(cfgxml.decode(data), [
            "messages", None, [], [
                ["message", None, [("id", "m")], [["title", None, [("value", "lng_t")], []]]],
            ]])


if __name__ == "__main__":
    unittest.main()
