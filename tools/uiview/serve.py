"""Servidor local do visualizador: serve `build/uiview` e grava edições de pista em `.nefs` novos.

    python -m tools.uiview.serve [--port 8790] [--root build/uiview] [--game PASTA]

`POST /api/save` recebe o JSON `dr2-track-edits` do Track Explorer e grava `<root>/saves/<pista>.nefs`
com `track_edit.apply`. Só escuta em 127.0.0.1 e nunca grava dentro da pasta do jogo.
"""

from __future__ import annotations

import functools
import http.server
import json
import os
import re
import sys

from tools.egodata.cli import DEFAULT_GAME
from tools.uiview import track_edit


def save(game: str, root: str, doc: dict) -> str:
    name = re.sub(r"[^A-Za-z0-9_.-]", "_", str(doc.get("track") or "pista"))
    out = os.path.join(root, "saves", name + ".nefs")
    os.makedirs(os.path.dirname(out), exist_ok=True)
    track_edit.apply(game, doc, out, log=lambda *_: None)
    return out


class Handler(http.server.SimpleHTTPRequestHandler):
    game = DEFAULT_GAME
    root = "build/uiview"

    def do_POST(self) -> None:
        if self.path != "/api/save":
            self.send_error(404)
            return
        try:
            doc = json.loads(self.rfile.read(int(self.headers.get("Content-Length", 0))))
            body, code = {"path": save(self.game, self.root, doc)}, 200
        except Exception as err:  # o viewer mostra a mensagem
            body, code = {"error": str(err)}, 400
        data = json.dumps(body).encode()
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)


def main(argv: list[str] | None = None) -> int:
    import argparse

    parser = argparse.ArgumentParser(prog="python -m tools.uiview.serve")
    parser.add_argument("--port", type=int, default=8790)
    parser.add_argument("--root", default="build/uiview")
    parser.add_argument("--game", default=DEFAULT_GAME)
    args = parser.parse_args(argv)
    root = os.path.abspath(args.root)
    if args.game and root.startswith(args.game.rstrip("/") + "/"):
        parser.error("a pasta de saída não pode ficar dentro da pasta do jogo")
    Handler.game, Handler.root = args.game, root
    handler = functools.partial(Handler, directory=root)
    with http.server.ThreadingHTTPServer(("127.0.0.1", args.port), handler) as srv:
        print(f"http://127.0.0.1:{args.port}/ (gravações em {root}/saves)")
        srv.serve_forever()
    return 0


if __name__ == "__main__":
    sys.exit(main())
