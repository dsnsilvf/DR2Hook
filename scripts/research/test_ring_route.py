"""Testes das paredes de reset do ring_route (cqtc com quadtree)."""
import os
import struct
import sys
import unittest

import numpy as np

sys.path.insert(0, os.path.dirname(__file__))

from ring_cameras import decode_cqtc  # noqa: E402
from ring_route import LEAF_MAX, curtain, encode_cqtc_tree, resetlines  # noqa: E402


def circle(r: float, n: int = 400) -> np.ndarray:
    a = np.linspace(0, 2 * np.pi, n + 1)
    return np.stack([r * np.cos(a), r * np.sin(a)], axis=1)


class CqtcTreeTest(unittest.TestCase):
    def setUp(self):
        # duas cortinas em volta de uma pista circular de raio 100: por fora (110) e por dentro (90)
        self.verts, self.tris = [], []
        for r in (110.0, 90.0):
            line = circle(r)
            v, t = curtain(line, np.zeros(len(line)), circle(100.0), len(self.verts))
            self.verts += v
            self.tris += t
        self.data = encode_cqtc_tree(self.verts, self.tris, b"REST")
        self.dec = decode_cqtc(self.data)

    def test_ida_e_volta(self):
        self.assertEqual(self.dec["tris"], self.tris)
        self.assertLess(np.abs(self.dec["verts"] - np.array(self.verts)).max(), 0.05)
        self.assertEqual(self.data[52:56], b"REST")

    def test_folhas(self):
        leaves = self.dec["leaves"]
        self.assertEqual({t for leaf in leaves for t in leaf}, set(range(len(self.tris))))
        self.assertLessEqual(max(len(leaf) for leaf in leaves), LEAF_MAX)
        self.assertTrue(all(leaf[0] == min(leaf) for leaf in leaves))

    def test_normais_para_a_pista(self):
        v = np.array(self.verts)
        for a, b, c in self.tris:
            self.assertLess(a, min(b, c))
            p = v[[a, b, c]]
            n = np.cross(p[1] - p[0], p[2] - p[0])[[0, 2]]
            cen = p.mean(0)[[0, 2]]
            r = np.linalg.norm(cen)
            # por fora a normal aponta para o centro; por dentro, para longe dele
            self.assertGreater(np.dot(n, -cen if r > 100 else cen), 0)

    def test_conteiner(self):
        data = resetlines({b"REST": self.data, b"SCRS": self.data})
        self.assertEqual(struct.unpack_from("<II", data, 0), (3, 2))
        off, size = struct.unpack_from("<QQ", data, 8 + 28)
        self.assertEqual(data[8 + 28 + 16:8 + 28 + 20], b"SCRS")
        self.assertEqual(data[off:off + size], self.data)


if __name__ == "__main__":
    unittest.main()
