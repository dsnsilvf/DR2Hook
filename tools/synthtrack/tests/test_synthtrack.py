import io
import json
import math
import os
import re
import struct
import tempfile
import unittest

from tools.synthtrack import build as synth
from tools.uiview.mesh import unpack_geom
from tools.uiview.track import edit as track_edit
from tools.uiview.track import export as track


# Gravado pelo scripts/research/ring_deploy.py (giro e deslocamento do porte para o jogo), não pelo gerador.
EXAMPLE_EXTRAS = {"game_transform.json"}
_NUMBER = re.compile(rb"-?\d+(?:\.\d+)?(?:[eE][-+]?\d+)?")


def _close(a: float, b: float) -> bool:
    return abs(a - b) <= 1e-5 + 1e-6 * max(abs(a), abs(b))


def same_content(rel: str, a: bytes, b: bytes) -> bool:
    """Igual, a menos de ruído de float: PNG por pixels; texto número a número; binário palavra a palavra (float32)."""
    if a == b:
        return True
    if rel.endswith(".png"):
        from PIL import Image
        with Image.open(io.BytesIO(a)) as ia, Image.open(io.BytesIO(b)) as ib:
            return ia.mode == ib.mode and ia.size == ib.size and ia.tobytes() == ib.tobytes()
    if rel.endswith((".json", ".ens", ".xml")):
        if _NUMBER.sub(b"#", a) != _NUMBER.sub(b"#", b):
            return False
        na, nb = _NUMBER.findall(a), _NUMBER.findall(b)
        return len(na) == len(nb) and all(_close(float(x), float(y)) for x, y in zip(na, nb))
    if len(a) != len(b):
        return False
    words = {i - i % 4 for i in range(len(a)) if a[i] != b[i]}  # floats alinhados em 4 nos .bin do formato
    for o in words:
        if o + 4 > len(a):
            return False
        x, y = struct.unpack_from("<f", a, o)[0], struct.unpack_from("<f", b, o)[0]
        # inteiros pequenos (tipo, id) lidos como float viram subnormais: esses precisam bater exatos
        if any(v != 0.0 and abs(v) < 1.2e-38 for v in (x, y)):
            return False
        if not (math.isfinite(x) and math.isfinite(y) and _close(x, y)):
            return False
    return True


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

    def test_example_matches_generator(self):
        """A cópia versionada em examples/ é a saída atual do gerador (regere com `python -m tools.synthtrack -o examples`).

        Compara o conteúdo, não os bytes: a libm e o zlib de outra máquina mudam o último bit de alguns floats e a
        compressão dos PNGs, sem mudar a pista."""
        example = os.path.join(os.path.dirname(__file__), "..", "..", "..", "examples", "tracks", synth.TRACK_ID)
        if not os.path.isdir(example):
            self.skipTest("examples/ ausente")
        generated = {os.path.relpath(os.path.join(r, f), self.dest) for r, _, fs in os.walk(self.dest) for f in fs}
        versioned = {os.path.relpath(os.path.join(r, f), example) for r, _, fs in os.walk(example) for f in fs}
        self.assertEqual(versioned - EXAMPLE_EXTRAS, generated)
        for rel in sorted(generated):
            with open(os.path.join(example, rel), "rb") as fh:
                self.assertTrue(same_content(rel, self.read(rel), fh.read()), f"examples/ desatualizado: {rel}")

    def test_replay_cameras(self):
        rep = self.doc["routes"][0]["replay"]
        cams = {c["name"]: c for c in rep["cameras"]}
        # nomes que o jogo procura no replay_camera_config.xml
        for name in ("initial_camera_r0", "Grid_Start_Cam_00", "finish_line_camera_001", "first_corner_001",
                     "pre_race_intro_001", "camera_r0_firstlap_001", *(f"pre_race_{k:03d}" for k in range(1, 8))):
            self.assertIn(name, cams)
        self.assertGreaterEqual(sum(c["kind"] == "trackside" for c in rep["cameras"]), 10)
        for c in rep["cameras"]:
            self.assertEqual(len(c["pos"]), 3, c["name"])
            self.assertGreater(sum((a - b) ** 2 for a, b in zip(c["pos"], c["aim"])), 1.0, c["name"])
            for key in ("path", "target"):
                pts = c.get(key, [])
                self.assertEqual(len(pts) % 4, 0, c["name"])
                for k in range(4, len(pts), 4):
                    self.assertEqual(pts[k], pts[k - 1], c["name"])   # trechos de Bézier encadeados
        for z in rep["zones"]:
            self.assertAlmostEqual(sum(sw["p"] for sw in z["switch"]), 1.0, places=6, msg=z["name"])
            for sw in z["switch"]:
                self.assertTrue(sw["camera"] in cams or sw["camera"].startswith(("onboard_", "external_")), sw)
            self.assertGreater(math.dist(z["l"], z["r"]), 2 * 5.0)
        for b in rep["bounds"]:
            self.assertEqual(len(b["corners"]), 4)
            self.assertLess(b["y0"], b["y1"])
        self.assertEqual(rep, self.doc["routes"][1]["replay"])

    def test_start_grids(self):
        from tools.synthtrack import layout

        grids = {g["name"]: g for g in self.doc["routes"][0]["grids"]}
        # grades e quantidade de vagas que o grids.pssg da Montalegre tem
        counts = {"grid_time_trial_0": 1, "grid_near_reset_01": 10, "grid_start_standing_01": 10,
                  "grid_start_staggered_01": 12, "grid_compound_5#5": 4}
        self.assertEqual({k: len(g["slots"]) for k, g in grids.items()}, counts)
        self.assertEqual(grids["grid_time_trial_0"]["slots"][0]["name"], "slot_0")
        for g in grids.values():
            names = [s["name"] for s in g["slots"]]
            self.assertEqual(len(set(names)), len(names), g["name"])
            for s in g["slots"] + g["markers"]:
                self.assertAlmostEqual(math.hypot(*s["fwd"]), 1.0, places=3, msg=s["name"])
            for s in g["slots"]:
                if g["name"] != "grid_compound_5#5":   # o paddock fica fora da pista
                    self.assertLessEqual(abs(s["lat"]) + s["size"][0] / 2, layout.ROAD_HALF + 0.05, s["name"])
            for a in g["slots"]:   # caixas não se sobrepõem
                for b in g["slots"]:
                    if a is not b:
                        apart = math.dist(a["pos"], b["pos"])
                        self.assertGreater(apart, min(a["size"][0], a["size"][1]) - 1e-6, (a["name"], b["name"]))
        self.assertEqual(self.doc["routes"][0]["grids"], self.doc["routes"][1]["grids"])


if __name__ == "__main__":
    unittest.main()
