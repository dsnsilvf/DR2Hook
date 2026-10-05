import struct
import unittest

from tools.uiview import track, track_edit

ENS = """<?xml version="1.0" encoding="utf-8" standalone="yes"?>
<PSSGFILE version="0.4.0.0beta">
	<PSSGDATABASE creator="CSSGXml">
		<TEMPLATEENTITYINSTANCE id="a1" instanceID="1" uri="route_objecttypes.pssg#barrier_a">
			<TEMPLATETRANSFORM>1 0 0 0 0 1 0 0 0 0 1 0 10 20 30 1 </TEMPLATETRANSFORM>
		</TEMPLATEENTITYINSTANCE>
		<TEMPLATEENTITYINSTANCE id="a2" instanceID="2" uri="route_objecttypes.pssg#barrier_b">
			<TEMPLATETRANSFORM>0 0 1 0 0 1 0 0 -1 0 0 0 40 50 60 1 </TEMPLATETRANSFORM>
		</TEMPLATEENTITYINSTANCE>
		<TEMPLATEENTITYINSTANCE id="skip" instanceID="3" uri="no_hash_here">
			<TEMPLATETRANSFORM>1 0 0 0 0 1 0 0 0 0 1 0 0 0 0 1 </TEMPLATETRANSFORM>
		</TEMPLATEENTITYINSTANCE>
		<TEMPLATEENTITYINSTANCE id="a3" instanceID="4" uri="route_objecttypes.pssg#barrier_a">
			<TEMPLATETRANSFORM>1 0 0 0 0 1 0 0 0 0 1 0 70 80 90 1 </TEMPLATETRANSFORM>
		</TEMPLATEENTITYINSTANCE>
	</PSSGDATABASE>
</PSSGFILE>
"""
ID12 = [1, 0, 0, 0, 1, 0, 0, 0, 1]


def record(h, ident, rot, pos, size, rot_at, pos_at):
    out = bytearray(size)
    struct.pack_into("<II", out, 0, h, ident)
    struct.pack_into("<9f", out, rot_at, *rot)
    struct.pack_into("<3f", out, pos_at, *pos)
    return bytes(out)


def packed(n, stride, start, start_at, count_at, rot_at, pos_at, positions):
    head = bytearray(start)
    struct.pack_into("<I", head, start_at, start)
    struct.pack_into("<I", head, count_at, n)
    body = b"".join(record(7 + i, i, ID12, positions[i], stride, rot_at, pos_at) for i in range(n))
    return bytes(head) + body


class TrackTests(unittest.TestCase):
    def test_instances_follow_document_order_and_skip_unusable(self):
        got = track.instances(ENS.encode())
        self.assertEqual([i["id"] for i in got], ["a1", "a2", "a3"])
        self.assertEqual(got[1]["type"], "barrier_b")
        self.assertEqual(got[1]["m"][12:15], [40, 50, 60])

    def test_spans_agree_with_instances(self):
        self.assertEqual(len(track_edit.ens_spans(ENS)), len(track.instances(ENS.encode())))

    def test_edit_ens_moves_and_deletes_without_changing_size(self):
        edits = [
            {"index": 0, "deleted": False, "m": ID12 + [11, 22, 33]},
            {"index": 1, "deleted": True, "m": ID12 + [0, 0, 0]},
        ]
        data = track_edit.edit_ens(ENS.encode(), edits)
        self.assertEqual(len(data), len(ENS.encode()))
        got = track.instances(data)
        self.assertEqual([i["id"] for i in got], ["a1", "a3"])
        self.assertEqual(got[0]["m"][12:15], [11, 22, 33])
        self.assertEqual(got[0]["m"][15], 1)

    def test_edit_ens_rejects_index_outside_file(self):
        with self.assertRaises(ValueError):
            track_edit.edit_ens(ENS.encode(), [{"index": 9, "deleted": False, "m": ID12 + [0, 0, 0]}])

    def test_trees_bin_roundtrip_and_edit(self):
        data = packed(3, 96, 72, 60, 48, 8, 44, [(1, 2, 3), (4, 5, 6), (7, 8, 9)])
        got = track.trees_bin(data)
        self.assertEqual(len(got), 3)
        self.assertEqual(got[2]["m"][12:15], [7, 8, 9])
        layout = track_edit.BIN_LAYOUT["t"]
        out = track_edit.edit_bin(data, [{"index": 1, "deleted": True, "m": ID12 + [4, 5, 6]}, {"index": 2, "deleted": False, "m": ID12 + [70, 80, 90]}], layout)
        again = track.trees_bin(out)
        self.assertEqual(again[0], got[0])
        self.assertEqual(again[1]["m"][13], track_edit.HIDDEN_Y)
        self.assertEqual(again[1]["m"][:3], [0, 0, 0])
        self.assertEqual(again[2]["m"][12:15], [70, 80, 90])

    def test_ornaments_bin_layout(self):
        data = packed(2, 212, 140, 80, 88, 16, 52, [(1, 2, 3), (4, 5, 6)])
        got = track.ornaments_bin(data)
        self.assertEqual([g["m"][12:15] for g in got], [[1, 2, 3], [4, 5, 6]])

    def test_pack_instances_layout(self):
        items = [{"type": "e:a", "idnum": 5, "m": ID12[:3] + [0] + ID12[3:6] + [0] + ID12[6:9] + [0] + [1, 2, 3, 1]},
                 {"type": "t:b", "idnum": 6, "m": ID12[:3] + [0] + ID12[3:6] + [0] + ID12[6:9] + [0] + [4, 5, 6, 1]}]
        data = track.pack_instances(items, {"e:a": 0, "t:b": 1})
        self.assertEqual(data[:4], b"DR2I")
        n = struct.unpack_from("<I", data, 4)[0]
        self.assertEqual(n, 2)
        types = struct.unpack_from("<2H", data, 8)
        self.assertEqual(types, (0, 1))
        ids = struct.unpack_from("<2I", data, 12)
        self.assertEqual(ids, (5, 6))
        floats = struct.unpack_from("<24f", data, 20)
        self.assertEqual(floats[9:12], (1, 2, 3))
        self.assertEqual(floats[21:24], (4, 5, 6))

    def test_pick_diffuse_prefers_color_map(self):
        self.assertEqual(track.pick_diffuse(["x_n.tga", "x_d.tga", "x_s.tga"]), "x_d.tga")
        self.assertEqual(track.pick_diffuse(["x_n.tga", "x_s.tga", "y_a.tga"]), "y_a.tga")
        self.assertIsNone(track.pick_diffuse(["x_n.tga", "x_s.tga"]))

    def test_near_route_keeps_background_meshes(self):
        tile = lambda x0, x1: {"positions": [(x0, 0, 0), (x1, 0, 10)]}
        far, near, back = tile(5000, 5050), tile(0, 50), tile(-9000, 9000)
        kept = track.near_route([far, near, back], [[10, 0, 5]], margin=100)
        self.assertEqual(kept, [near, back])


if __name__ == "__main__":
    unittest.main()
