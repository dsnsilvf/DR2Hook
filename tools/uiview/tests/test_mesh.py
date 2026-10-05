"""Malha e seleção de modelos, sem abrir o jogo."""

import struct
import unittest

from tools.egodata.pssg import PSSGAttribute, PSSGNode
from tools.uiview.mesh import decode_half, extract_meshes, pack_geom, summarize_geom, transform_point, unpack_geom
from tools.uiview.content import texture_name
from tools.uiview.car.models import HARD_CAP, SOFT_CAP, classify_path, parse_decision, surface_suffix


def node(type_name, children=None, data=None, **attrs):
    item = PSSGNode(0, type_name, 0, 0, 0)
    item.children = list(children or [])
    item.data = data
    for key, value in attrs.items():
        item.attributes[key] = PSSGAttribute(0, key, b"", value)
    return item


def matrix(tx, ty, tz):
    return struct.pack(">16f", 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, tx, ty, tz, 1)


class MeshTest(unittest.TestCase):
    def test_half_and_translation(self):
        self.assertAlmostEqual(decode_half(0x3C00), 1.0)
        moved = transform_point((1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 5, 0, 0, 1), 1, 2, 3)
        self.assertEqual(moved, (6, 2, 3))

    def test_skin_moves_the_triangle_and_lod1_is_left_out(self):
        verts = struct.pack(">9f", 0, 0, 0, 1, 0, 0, 0, 1, 0)
        skins = struct.pack(">3f", 0, 0, 0)
        block_v = node("DATABLOCK", id="bv", elementCount=3, children=[
            node("DATABLOCKSTREAM", renderType="Vertex", dataType="float3", offset=0, stride=12),
            node("DATABLOCKDATA", data=verts),
        ])
        block_s = node("DATABLOCK", id="bs", elementCount=3, children=[
            node("DATABLOCKSTREAM", renderType="SkinIndices", dataType="float", offset=0, stride=4),
            node("DATABLOCKDATA", data=skins),
        ])
        uv = struct.pack(">6H", 0, 0x3C00, 0x3C00, 0, 0, 0)
        block_uv = node("DATABLOCK", id="bu", elementCount=3, children=[
            node("DATABLOCKSTREAM", renderType="ST", dataType="half2", offset=0, stride=4),
            node("DATABLOCKDATA", data=uv),
        ])

        def source(name, shader, joint):
            rds = node("RENDERDATASOURCE", id=name, children=[
                node("RENDERINDEXSOURCE", primitive="triangles", format="ushort", children=[
                    node("INDEXSOURCEDATA", data=struct.pack(">3H", 0, 1, 2)),
                ]),
                node("RENDERSTREAM", dataBlock="#bv"),
                node("RENDERSTREAM", dataBlock="#bu"),
                node("RENDERSTREAM", dataBlock="#bs"),
            ])
            inst = node("MATRIXPALETTERENDERINSTANCE", shader=shader, indices="#" + name, children=[
                node("MATRIXPALETTESKINJOINT", joint="#" + joint),
            ])
            return rds, inst

        low_rds, low_inst = source("low", "#hidden", "jl")
        high_rds, high_inst = source("high", "#body", "jh")
        root = node("PSSGDATABASE", children=[
            block_v, block_s, block_uv,
            node("MATRIXPALETTEJOINTNODE", id="jl", nickname="jl", children=[node("TRANSFORM", data=matrix(9, 0, 0))]),
            node("MATRIXPALETTEJOINTNODE", id="jh", nickname="jh", children=[node("TRANSFORM", data=matrix(5, 0, 0))]),
            low_rds, high_rds,
            node("MATRIXPALETTEBUNDLENODE", nickname="LOD1_", children=[low_inst]),
            node("MATRIXPALETTEBUNDLENODE", nickname="LOD0_", children=[high_inst]),
        ])
        meshes = extract_meshes(root)
        self.assertEqual([m["material"] for m in meshes], ["body"])
        self.assertEqual(meshes[0]["positions"][0], (5, 0, 0))
        self.assertAlmostEqual(meshes[0]["uvs"][1][0], 1.0)
        back = unpack_geom(pack_geom(meshes))
        self.assertEqual(back[0]["name"], "body")
        self.assertEqual(back[0]["indices"], [0, 1, 2])
        self.assertAlmostEqual(back[0]["positions"][1][0], 6.0)

    def test_character_keeps_the_detailed_copy(self):
        verts = struct.pack(">9f", 0, 0, 0, 1, 0, 0, 0, 1, 0)
        block = node("DATABLOCK", id="bv", elementCount=3, children=[
            node("DATABLOCKSTREAM", renderType="SkinnableVertex", dataType="float3", offset=0, stride=12),
            node("DATABLOCKDATA", data=verts),
        ])

        def part(name, shader):
            return (
                node("RENDERDATASOURCE", id=name, children=[
                    node("RENDERINDEXSOURCE", primitive="triangles", format="ushort", children=[
                        node("INDEXSOURCEDATA", data=struct.pack(">3H", 0, 1, 2)),
                    ]),
                    node("RENDERSTREAM", dataBlock="#bv"),
                ]),
                node("MODIFIERNETWORKINSTANCE", id=name, shader=shader, indices="#" + name),
            )

        low_src, low = part("head_x3", "#head")
        high_src, high = part("head_x1", "#head")
        suit_src, suit = part("suit_x1", "#livery")
        root = node("PSSGDATABASE", children=[block, low_src, high_src, suit_src, low, high, suit])
        meshes = extract_meshes(root)
        self.assertEqual(sorted(m["material"] for m in meshes), ["head", "livery"])
        self.assertEqual(meshes[0]["positions"][1], (1, 0, 0))

    def test_summary_matches_the_packed_mesh(self):
        import os
        import tempfile
        mesh = {"name": "porta", "material": "bodywork", "positions": [(1, 2, 3), (4, 5, 6)],
                "uvs": [(0.25, 0.5), (0.75, 0.5)], "indices": [0, 1, 0]}
        with tempfile.TemporaryDirectory() as folder:
            path = os.path.join(folder, "a.bin")
            with open(path, "wb") as fh:
                fh.write(pack_geom([mesh]))
            summary = summarize_geom(path)
        self.assertEqual(summary["meshes"], 1)
        self.assertEqual(summary["mats"], ["bodywork"])
        self.assertEqual(summary["verts"], 2)
        self.assertEqual(summary["tris"], 1)
        self.assertIsNone(summarize_geom(os.path.join(folder, "ausente.bin")))

    def test_odd_name_stays_aligned(self):
        mesh = {"name": "a", "material": "b", "positions": [(1, 2, 3)], "uvs": [(0.5, 0.25)], "indices": [0, 0, 0]}
        back = unpack_geom(pack_geom([mesh, mesh]))
        self.assertEqual(len(back), 2)
        self.assertEqual(back[1]["positions"][0], (1, 2, 3))


class SelectionTest(unittest.TestCase):
    def test_classify(self):
        self.assertEqual(classify_path("cars/models/037/037_highLOD.pssg"), "carro")
        self.assertEqual(classify_path("cars/models/037/037_lowLOD.pssg"), "lod")
        self.assertEqual(classify_path("cars/models/037/interior/int_037.pssg"), "interior")
        self.assertIsNone(classify_path("cars/models/037/livery_00/textures_high/037_tex_high_00.pssg"))
        self.assertEqual(surface_suffix("cars/models/037/livery_00/textures_high/037_tex_tm_high_00.pssg"), "tm")
        self.assertEqual(surface_suffix("cars/models/037/livery_00/textures_high/037_tex_gr_high_00.pssg"), "gr")
        self.assertEqual(surface_suffix("cars/models/037/livery_00/textures_high/037_tex_high_00.pssg"), "")
        self.assertEqual(texture_name("037_wheel_d.tga", "tm"), "037_wheel_d_tm.tga")
        self.assertEqual(texture_name("037_main_d.tga", "tm"), "037_main_d.tga")
        self.assertEqual(classify_path("characters/models/codriver_male_phil_mills/codriver.pssg"), "personagem")
        self.assertEqual(classify_path("tracks/locations/usa/twin_peaks/objects.pssg"), "local")

    def test_filter_keeps_primary_and_can_force_a_file(self):
        src = "cars/037.nefs"
        high = "cars/models/037/037_highLOD.pssg"
        low = "cars/models/037/037_lowLOD.pssg"
        interior = "cars/models/037/interior/int_037.pssg"
        self.assertEqual(parse_decision("carro", 12_000_000, src, high, None), "parse")
        self.assertEqual(parse_decision("lod", 20_000_000, src, low, None), "index")
        self.assertEqual(parse_decision("carro", 12_000_000, src, high, ["037"]), "parse")
        self.assertEqual(parse_decision("lod", 20_000_000, src, low, ["037"]), "index")
        self.assertEqual(parse_decision("interior", 48_000_000, src, interior, ["037"]), "index")
        self.assertEqual(parse_decision("interior", 48_000_000, src, interior, ["int_037.pssg"]), "parse")
        self.assertEqual(parse_decision("local", HARD_CAP + 1, "locations/usa__twin_peaks.nefs",
                                        "tracks/locations/usa/twin_peaks/tracksplit.pssg", ["twin_peaks"]), "index")
        self.assertEqual(parse_decision("personagem", 5_000_000, "game/game.nefs",
                                        "characters/models/codriver_male_phil_mills/codriver.pssg", ["phil_mills"]), "parse")
        self.assertEqual(parse_decision("lod", 20_000_000, src, low, None, all_models=True), "parse")
        self.assertEqual(parse_decision("interior", 48_000_000, src, interior, None, all_models=True), "parse")
        self.assertEqual(parse_decision("local", HARD_CAP + 1, "locations/usa__twin_peaks.nefs",
                                        "tracks/locations/usa/twin_peaks/tracksplit.pssg", None, all_models=True), "index")
        self.assertEqual(parse_decision("carro", SOFT_CAP + 1, src, high, None, all_models=True), "parse")


if __name__ == "__main__":
    unittest.main()
