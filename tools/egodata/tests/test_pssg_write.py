"""Serializador do PSSG: regrava idêntico e aceita edições simples."""

import struct
import unittest
import zlib

from tools.egodata.pssg import PSSGFile


def attr(aid, raw):
    return struct.pack(">II", aid, len(raw)) + raw


def text(value):
    raw = value.encode()
    return struct.pack(">I", len(raw)) + raw


def node(nid, attrs=b"", payload=b""):
    return struct.pack(">III", nid, 4 + len(attrs) + len(payload), len(attrs)) + attrs + payload


def sample(blob=b"abcdefgh" * 4):
    schema = b""
    for nid, name, attrs in ((1, b"ROOT", {1: b"id"}), (2, b"LEAF", {1: b"id", 2: b"count"}), (3, b"DATA", {})):
        schema += struct.pack(">II", nid, len(name)) + name + struct.pack(">I", len(attrs))
        for aid, aname in attrs.items():
            schema += struct.pack(">II", aid, len(aname)) + aname
    leaf = node(2, attr(1, text("peca")) + attr(2, struct.pack(">I", 3)))
    data = node(3, b"", zlib.compress(blob))
    body = schema + node(1, attr(1, text("raiz")), leaf + data)
    return struct.pack(">4sIII", b"PSSG", len(body) + 8, 2, 3) + body


class PssgWriteTest(unittest.TestCase):
    def test_roundtrip_is_identical(self):
        raw = sample()
        self.assertEqual(PSSGFile(raw).serialize(), raw)

    def test_trailing_bytes_are_kept(self):
        raw = sample() + b"\x00\x00\x00\x00"
        self.assertEqual(PSSGFile(raw).serialize(), raw)

    def test_edit_attribute_text_changes_sizes(self):
        f = PSSGFile(sample())
        leaf = f.root.children[0]
        PSSGFile.set_attr(leaf, "id", "outra_peca_maior")
        again = PSSGFile(f.serialize())
        self.assertEqual(again.root.children[0].id, "outra_peca_maior")
        self.assertEqual(again.root.children[1].data, b"abcdefgh" * 4)

    def test_edit_compressed_payload_recompresses(self):
        f = PSSGFile(sample())
        f.root.children[1].data = b"novo conteudo " * 10
        again = PSSGFile(f.serialize())
        self.assertEqual(again.root.children[1].data, b"novo conteudo " * 10)


if __name__ == "__main__":
    unittest.main()
