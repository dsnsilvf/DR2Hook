"""Testes do porte do editor ao jogo (ring_deploy.py) e das edições no ring_objects.py.

    python3 -m unittest scripts/research/test_ring_deploy.py
"""
import json
import os
import sys
import tempfile
import threading
import time
import unittest

import numpy as np

sys.path.insert(0, os.path.dirname(__file__))

import ring_deploy  # noqa: E402
from ring_cameras import decode_cqtc, encode_cqtc  # noqa: E402
from ring_objects import apply_edits, matrix12  # noqa: E402


def m12(x):
    return [1, 0, 0, 0, 1, 0, 0, 0, 1, x, 0, 0]


class ApplyEditsTest(unittest.TestCase):
    def setUp(self):
        self.insts = [("e:cone", matrix12(np.array(m12(k), float)), k) for k in range(3)]
        self.insts.append(("o:tent", matrix12(np.array(m12(10), float)), 0))

    def edits(self, *items):
        return {"format": "dr2-track-edits", "version": 1, "edits": list(items)}

    def test_move_delete_copy(self):
        e = self.edits(
            {"route": "route_0", "kind": "e", "type": "cone", "index": 1, "deleted": False, "m": m12(5), "m0": m12(1)},
            {"route": "route_0", "kind": "e", "type": "cone", "index": 2, "deleted": True, "m": m12(2), "m0": m12(2)},
            {"route": "route_0", "kind": "o", "type": "tent", "added": True, "src": 0, "index": -1, "deleted": False,
             "m": m12(42), "m0": m12(42)},
            {"route": "route_1", "kind": "e", "type": "cone", "index": 0, "deleted": True, "m": m12(0), "m0": m12(0)},
        )
        out, skipped = apply_edits(self.insts, e, "route_0")
        self.assertEqual(skipped, [])
        self.assertEqual([(t, m[3, 0]) for t, m in out], [("e:cone", 0), ("e:cone", 5), ("o:tent", 10), ("o:tent", 42)])

    def test_unknown_instance_is_reported(self):
        e = self.edits({"route": "route_0", "kind": "e", "type": "cone", "index": 9, "deleted": True,
                        "m": m12(0), "m0": m12(0)})
        out, skipped = apply_edits(self.insts, e, "route_0")
        self.assertEqual(len(out), 4)
        self.assertEqual(len(skipped), 1)

    def test_wrong_format(self):
        with self.assertRaises(SystemExit):
            apply_edits(self.insts, {"format": "x"}, "route_0")


class CqtcTest(unittest.TestCase):
    def test_round_trip_reads_every_triangle(self):
        sq = np.array([[0, 0], [2, 0], [2, 2], [0, 2]], float)
        data = encode_cqtc([(sq, 0.0, 100.0), (sq + [50, 30], 5.0, 105.0)])
        d = decode_cqtc(data)
        self.assertEqual(d["leaves"], [list(range(16))])  # refs alinhadas: 1º em 24 bits, depois u16
        self.assertTrue(np.allclose(d["verts"][:2], [[0, 100, 0], [0, 0, 0]], atol=1e-3))


class DeployTest(unittest.TestCase):
    def test_set_ini_keeps_comments_and_adds_missing(self):
        with tempfile.TemporaryDirectory() as d:
            p = os.path.join(d, "a.ini")
            with open(p, "w") as fh:
                fh.write("; comentario\n[autostage]\nenabled = 0\ntrack = montalegre\n")
            ring_deploy.set_ini(p, {"enabled": "1", "track": "dr2hook_ring", "once": "1"})
            self.assertEqual(open(p).read(), "; comentario\n[autostage]\nenabled = 1\ntrack = dr2hook_ring\nonce = 1\n")

    def test_signature_follows_edits_content_and_upstream(self):
        with tempfile.TemporaryDirectory() as d:
            src, edits = os.path.join(d, "src.bin"), os.path.join(d, "e.json")
            open(src, "wb").write(b"a")
            json.dump({"edits": []}, open(edits, "w"))
            st = ring_deploy.Step("objects", "x", None, inputs=[src, edits], after=["terrain"])
            base = ring_deploy.signature(st, {"terrain": "t1"}, edits)
            self.assertEqual(base, ring_deploy.signature(st, {"terrain": "t1"}, edits))
            self.assertNotEqual(base, ring_deploy.signature(st, {"terrain": "t2"}, edits))
            json.dump({"edits": [1]}, open(edits, "w"))
            self.assertNotEqual(base, ring_deploy.signature(st, {"terrain": "t1"}, edits))


class GameLogTest(unittest.TestCase):
    def _deploy(self, game):
        d = ring_deploy.Deploy.__new__(ring_deploy.Deploy)
        d.a = type("A", (), {"game": game})()
        d.rep = type("R", (), {"log": lambda self, m: None, "line": lambda self, m: None})()
        d.t0, d.log_pos, d.reached, d.opens, d.last_open = 0, 0, -1, 0, 0.0
        return d

    def _feed(self, d, game, data: bytes):
        with open(os.path.join(game, "dr2hook.log"), "ab") as fh:
            for k in range(0, len(data), 7):  # pedaços que cortam as linhas
                fh.write(data[k:k + 7])
                fh.flush()
                d.poll_log()

    def test_phases_in_order_with_crlf(self):
        log = ("[0] [INFO] AutoStage: Fast-path configurado\r\n"
               "[1] [INFO] LoadTrace: open a\r\n"
               "[2] [INFO] LoadTrace: IO 300 MB em 9 leituras\r\n"  # o do boot não fecha a pista
               "[3] [INFO] RaceEvent: carregando 'x'.\r\n"
               + "[4] [INFO] LoadTrace: open b\r\n" * 5
               + "[5] [INFO] RaceEvent: largada em 'x'.\r\n")
        with tempfile.TemporaryDirectory() as g:
            d = self._deploy(g)
            self._feed(d, g, log.encode()[:120])
            self.assertEqual(d.reached, 0)
            self._feed(d, g, log.encode()[120:])
            self.assertEqual((d.reached, d.opens), (3, 5))

    def test_crash_line_fails(self):
        with tempfile.TemporaryDirectory() as g:
            d = self._deploy(g)
            with self.assertRaisesRegex(RuntimeError, "travou"):
                self._feed(d, g, b"[0] [ERROR] GhostLab[crash]: excecao 0xc0000005 rip=1\r\n")


class CommandChannelTest(unittest.TestCase):
    """Pausar/continuar do viewer: um lote pelo dr2hook_cmd.txt e a resposta no dr2hook_cmd.out."""

    def test_answer_is_the_last_entry_of_the_command(self):
        out = "# lote 7\n> status\ncorrida\n> pause\nok: pausa pedida\n"
        self.assertEqual(ring_deploy.cmd_answer(out, "pause"), "ok: pausa pedida")
        self.assertEqual(ring_deploy.cmd_answer(out, "status"), "corrida")
        self.assertEqual(ring_deploy.cmd_answer(out, "unpause"), "")

    def test_round_trip_with_a_fake_core(self):
        with tempfile.TemporaryDirectory() as g:
            with open(os.path.join(g, "dr2hook_cmd.out"), "w") as fh:
                fh.write("# lote 1\n> pause\nresposta velha\n")
            old = time.time() - 60  # o .out do lote anterior
            os.utime(os.path.join(g, "dr2hook_cmd.out"), (old, old))

            def core():  # lê e apaga, e só depois grava a resposta (como o remote_commands.cpp)
                cmd = os.path.join(g, "dr2hook_cmd.txt")
                while not os.path.exists(cmd):
                    time.sleep(0.01)
                line = open(cmd).read().strip()
                os.remove(cmd)
                time.sleep(0.1)
                with open(os.path.join(g, "dr2hook_cmd.out"), "w") as fh:
                    fh.write(f"# lote 2\n> {line}\nok: pausa pedida\n")

            th = threading.Thread(target=core)
            th.start()
            try:
                self.assertEqual(ring_deploy.send_command(g, "pause"), "ok: pausa pedida")
            finally:
                th.join()

    def test_unread_command_is_removed(self):
        with tempfile.TemporaryDirectory() as g:
            with self.assertRaisesRegex(RuntimeError, "não leu"):
                ring_deploy.send_command(g, "pause", timeout=0.2)
            self.assertFalse(os.path.exists(os.path.join(g, "dr2hook_cmd.txt")))  # não roda no próximo boot


if __name__ == "__main__":
    unittest.main()
