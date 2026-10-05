import json
import os
import struct
import tempfile
import unittest

from tools.synthtrack import build as synth
from tools.uiview.mesh import unpack_geom
from tools.uiview.track import edit as track_edit
from tools.uiview.track import export as track


class SynthTrackTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        cls.out = cls.tmp.name
        cls.expected = synth.build(cls.out, seed=7, log=lambda *_: None)
        cls.dest = os.path.join(cls.out, "tracks", synth.TRACK_ID)
        with open(os.path.join(cls.dest, "track.json"), encoding="utf-8") as fh:
            cls.doc = json.load(fh)

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def read(self, *parts):
        with open(os.path.join(self.dest, *parts), "rb") as fh:
            return fh.read()

    def test_terrain_matches_track_json(self):
        meshes = unpack_geom(self.read("terrain_0.bin"))
        route = self.doc["routes"][0]
        self.assertEqual(len(meshes), route["terrain"]["meshes"])
        self.assertEqual(sum(len(m["positions"]) for m in meshes), route["terrain"]["verts"])
        self.assertEqual(self.expected["terrain"]["wide"], 1)  # o fundo passa de 65 535 vértices

    def test_instances_header_and_types(self):
        data = self.read("inst_route_0.bin")
        self.assertEqual(data[:4], b"DR2I")
        n = struct.unpack_from("<I", data, 4)[0]
        self.assertEqual(n, self.doc["routes"][0]["instances"])
        pad = (2 * n + 3) & ~3
        self.assertEqual(len(data), 8 + pad + 4 * n + 48 * n)
        types = struct.unpack_from(f"<{n}H", data, 8)
        self.assertLess(max(types), len(self.doc["type_order"]))
        objs = unpack_geom(self.read("objects.bin"))
        for name, info in self.doc["types"].items():
            self.assertLessEqual(info["first"] + info["count"], len(objs), name)
        self.assertTrue(any(v["count"] == 0 for v in self.doc["types"].values()))
        self.assertTrue(any("_dist_" in k for k in self.doc["type_order"]))

    def test_every_material_file_exists_and_is_power_of_two(self):
        from PIL import Image

        for mat, rel in self.doc["materials"].items():
            self.assertTrue(os.path.exists(os.path.join(self.dest, rel)), mat)
            png = os.path.join(self.dest, "source", "textures", os.path.basename(rel)[:-5] + ".png")
            with Image.open(png) as im:
                for side in im.size:
                    self.assertEqual(side & (side - 1), 0, png)

    def test_sources_follow_export_readers(self):
        ens = track.instances(self.read("source", "route_0", "objects.ens"))
        orn = track.ornaments_bin(self.read("source", "route_0", "ornaments.bin"))
        trees = track.trees_bin(self.read("source", "route_0", "trees.bin"))
        kinds = [self.doc["type_order"][t][0] for t in self._types()]
        self.assertEqual((len(ens), len(orn), len(trees)), (kinds.count("e"), kinds.count("o"), kinds.count("t")))
        self.assertEqual(len(self.doc["routes"][0]["ens_ids"]), len(ens))
        size = len(self.read("source", "route_0", "objects.ens"))
        self.assertGreater(-size % 65536, 2000, "espaço livre no último bloco de 64 KiB para cópias")

    def _types(self):
        data = self.read("inst_route_0.bin")
        n = struct.unpack_from("<I", data, 4)[0]
        return struct.unpack_from(f"<{n}H", data, 8)

    def test_edits_round_trip_through_track_edit(self):
        """Um edits.json no formato do viewer aplicado com as funções de track/edit.py."""
        ens_data = self.read("source", "route_0", "objects.ens")
        ens = track.instances(ens_data)
        m = list(ens[3]["m"])
        moved = [m[0], m[1], m[2], m[4], m[5], m[6], m[8], m[9], m[10], m[12] + 2.5, m[13], m[14] - 1.0]
        edits = [
            {"route": "route_0", "kind": "e", "type": "x", "index": 3, "deleted": False, "m": moved, "m0": moved},
            {"route": "route_0", "kind": "e", "type": "x", "index": 4, "deleted": True, "m": moved, "m0": moved},
            {"route": "route_0", "kind": "e", "type": "x", "added": True, "src": 5, "index": -1, "deleted": False, "m": moved, "m0": moved},
        ]
        out = track_edit.edit_ens(ens_data, edits)
        blocks = lambda n: (n + 65535) // 65536  # noqa: E731
        self.assertEqual(blocks(len(out)), blocks(len(ens_data)))  # o que check_blocks exige
        after = track.instances(out)
        self.assertEqual(len(after), len(ens))  # um apagado, uma cópia
        self.assertAlmostEqual(after[3]["m"][12], m[12] + 2.5, places=4)
        trees_data = self.read("source", "route_0", "trees.bin")
        t_edits = [{"route": "route_0", "kind": "t", "type": "y", "index": 0, "deleted": True, "m": moved, "m0": moved}]
        t_out = track_edit.edit_bin(trees_data, t_edits, track_edit.BIN_LAYOUT["t"])
        self.assertEqual(track.trees_bin(t_out)[0]["m"][13], track_edit.HIDDEN_Y)

    def test_second_route_shares_terrain_and_has_own_sources(self):
        routes = self.doc["routes"]
        self.assertEqual([r["name"] for r in routes], ["route_0", "route_1"])
        self.assertEqual(routes[0]["terrain"]["file"], routes[1]["terrain"]["file"])
        data = self.read("inst_route_1.bin")
        n = struct.unpack_from("<I", data, 4)[0]
        self.assertEqual(n, routes[1]["instances"])
        self.assertNotEqual(n, routes[0]["instances"])
        ens = track.instances(self.read("source", "route_1", "objects.ens"))
        self.assertEqual(len(ens), len(routes[1]["ens_ids"]))
        self.assertEqual(self.expected["instances_by_route"], {"route_0": routes[0]["instances"], "route_1": n})

    def test_same_seed_same_bytes(self):
        with tempfile.TemporaryDirectory() as other:
            synth.build(other, seed=7, log=lambda *_: None)
            for name in ("terrain_0.bin", "objects.bin", "inst_route_0.bin", "inst_route_1.bin", "track.json"):
                with open(os.path.join(other, "tracks", synth.TRACK_ID, name), "rb") as fh:
                    self.assertEqual(fh.read(), self.read(name), name)


if __name__ == "__main__":
    unittest.main()
