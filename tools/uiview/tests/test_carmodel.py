"""Árvore estrutural do carro (carmodel), sem abrir o jogo."""

import struct
import unittest

from tools.pssg import PSSGAttribute, PSSGNode
from tools.uiview.carmodel import build_car_model, lod_label, pack_resources, unpack_resources


def node(type_name, children=None, data=None, **attrs):
    item = PSSGNode(0, type_name, 0, 0, 0)
    item.children = list(children or [])
    item.data = data
    for key, value in attrs.items():
        item.attributes[key] = PSSGAttribute(0, key, b"", value)
    return item


def transform(tx, ty, tz):
    return node("TRANSFORM", data=struct.pack(">16f", 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, tx, ty, tz, 1))


def rds(name):
    """Quadrado de 4 vértices e 2 triângulos (6 índices)."""
    verts = struct.pack(">12f", 0, 0, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0)
    return node("RENDERDATASOURCE", id=name, children=[
        node("RENDERINDEXSOURCE", primitive="triangles", format="ushort", children=[
            node("INDEXSOURCEDATA", data=struct.pack(">6H", 0, 1, 2, 0, 2, 3)),
        ]),
        node("RENDERSTREAM", dataBlock="#" + name + "_v"),
    ]), node("DATABLOCK", id=name + "_v", elementCount=4, children=[
        node("DATABLOCKSTREAM", renderType="Vertex", dataType="float3", offset=0, stride=12),
        node("DATABLOCKDATA", data=verts),
    ])


def slice_(ident, shader, source, io=0, ic=6):
    return node("MATRIXPALETTEJOINTRENDERINSTANCE", id=ident, shader="#" + shader, indices="#" + source,
                streamOffset=0, elementCountFromOffset=4, indexOffset=io, indicesCountFromOffset=ic, jointID=2, extraField=7)


def sample(slice_ic=6):
    source, block = rds("x0_RDS1")
    group = node("SHADERGROUP", id="carglass.fx", children=[
        node("SHADERINPUTDEFINITION", name="FresnelMin", type="constant", format="float"),
        node("SHADERINPUTDEFINITION", name="TDiffuseAlphaMap", type="texture"),
    ])
    material = node("SHADERINSTANCE", id="glass", shaderGroup="#carglass.fx", parameterCount=2, parameterSavedCount=1, children=[
        node("SHADERINPUT", parameterID=0, type="constant", format="float", data=struct.pack(">f", 0.5)),
    ])
    wheel = node("MATRIXPALETTEJOINTNODE", id="x0_wheel_fl", nickname="x0_wheel_fl", children=[
        transform(1, 2, 3),
        slice_("s1", "glass", "x0_RDS1", 0, slice_ic),
        slice_("s2", "glass", "x0_RDS1", 3, 3),
    ])
    lod0 = node("MATRIXPALETTEBUNDLENODE", id="LOD0_", nickname="LOD0_", children=[wheel])
    top = node("ROOTNODE", id="Scene Root", children=[lod0])
    return node("PSSG", children=[
        node("LIBRARY", type="SHADERINSTANCE", children=[material]),
        node("LIBRARY", type="SHADERGROUP", children=[group]),
        node("LIBRARY", type="RENDERDATASOURCE", children=[source]),
        node("LIBRARY", type="RENDERINTERFACEBOUND", children=[block]),
        node("LIBRARY", type="NODE", children=[top]),
    ])


class CarModelTest(unittest.TestCase):
    def test_lod_label(self):
        self.assertEqual(lod_label("LOD0_"), "LOD0")
        self.assertEqual(lod_label("x1_lod1"), "LOD1")
        self.assertIsNone(lod_label("x0_wheel_fl"))

    def test_tree_keeps_names_slices_and_unknown_fields(self):
        model, resources = build_car_model(sample(), "037")
        self.assertEqual(model["notes"], [])
        lod = model["tree"]["children"][0]
        wheel = lod["children"][0]
        self.assertEqual(wheel["id"], "x0_wheel_fl")
        self.assertEqual(wheel["world"][12:15], [1.0, 2.0, 3.0])
        self.assertEqual([s["tris"] for s in wheel["slices"]], [2, 1])
        self.assertEqual(wheel["slices"][0]["extra"], {"extraField": 7})
        self.assertEqual(model["lods"], [{"name": "LOD0", "uid": lod["uid"], "nodes": 2, "slices": 2, "tris": 3}])
        self.assertEqual(list(resources), ["x0_RDS1"])
        self.assertEqual(model["resources"]["x0_RDS1"], {"verts": 4, "tris": 2})

    def test_material_reads_group_and_named_params(self):
        model, _ = build_car_model(sample(), "037")
        glass = model["materials"]["glass"]
        self.assertEqual(glass["group"], "carglass.fx")
        self.assertEqual(glass["params"], {"FresnelMin": 0.5})
        self.assertEqual(glass["savedCount"], 1)

    def test_slice_outside_buffer_is_reported_not_exported(self):
        model, _ = build_car_model(sample(slice_ic=60), "037")
        wheel = model["tree"]["children"][0]["children"][0]
        self.assertFalse(wheel["slices"][0]["ok"])
        self.assertTrue(any("fora do buffer" in n for n in model["notes"]))
        self.assertEqual(model["lods"][0]["tris"], 1)

    def test_resources_roundtrip(self):
        _, resources = build_car_model(sample(), "037")
        back = unpack_resources(pack_resources(resources))
        self.assertEqual(back["x0_RDS1"]["indices"], [0, 1, 2, 0, 2, 3])
        self.assertEqual(back["x0_RDS1"]["positions"][2], (1.0, 1.0, 0.0))


if __name__ == "__main__":
    unittest.main()
