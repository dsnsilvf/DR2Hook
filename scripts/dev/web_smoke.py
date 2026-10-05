#!/usr/bin/env python3
"""Teste de fumaça do visualizador web, num Edge/Chromium headless.

Monta uma pasta temporária com o `tools/uiview/web/` ATUAL e os dados de uma
exportação já feita, abre as abas Telas, Pistas e Carros, abre a primeira pista e
o primeiro carro e imprime um JSON com os erros de JavaScript e contagens.
Sai com código 1 se houver qualquer erro.

    python3 scripts/dev/web_smoke.py                       # usa build/uiview
    python3 scripts/dev/web_smoke.py --site build/uiview --tracks build/golden_antes

`--site` precisa ter `data/{ui,scenes,strings,assets,models}.js` (exportação
completa: `python -m tools.uiview --models 037`). `--tracks` é uma pasta com
`tracks/` e `data/tracks.js` (`python -m tools.uiview.track --tracks montalegre -o ...`);
sem ela, usa as pistas de `--site`.

Só lê as pastas de dados (por links simbólicos) e grava em `build/web_smoke/`.
O navegador é `$BROWSER` ou o primeiro de microsoft-edge-stable, chromium, google-chrome.
Sem GPU: usa o renderizador por software, que dá conta da Montalegre mas não da Polônia.
"""

from __future__ import annotations

import argparse
import functools
import html
import http.server
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import threading

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))

HOOK = """<script>
window.__errs = [];
window.addEventListener("error", (e) => window.__errs.push(String(e.message) + " @" + String(e.filename).split("/").slice(-2).join("/") + ":" + e.lineno));
window.addEventListener("unhandledrejection", (e) => window.__errs.push("promise: " + String((e.reason && e.reason.message) || e.reason)));
</script>
"""

DRIVER = """<script>
(async () => {
  const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
  const out = { errors: window.__errs, tabs: {} };
  const tab = async (mode) => { const b = document.querySelector('[data-mode="' + mode + '"]'); if (!b) throw new Error("sem aba " + mode); b.click(); await sleep(400); };
  const until = async (fn, ms) => { for (let t = 0; t < ms; t += 200) { if (fn()) return true; await sleep(200); } return false; };
  try {
    await sleep(500);
    out.tabs.screens = { items: document.querySelectorAll("#list > *").length };
    await tab("tracks");
    if (typeof TRACKS !== "undefined" && TRACKS.length) {
      const sel = document.getElementById("trk-open");
      if (sel) { sel.value = TRACKS[0].id; sel.dispatchEvent(new Event("change", { bubbles: true })); }
      const ok = await until(() => typeof tv !== "undefined" && tv.data && !tv.busy, 40000);
      out.tabs.tracks = { loaded: ok, id: TRACKS[0].id, types: tv.typeList && tv.typeList.length, instances: tv.inst && tv.inst.n, terrainMeshes: tv.terrain && tv.terrain.length, status: tv.status };
    } else out.tabs.tracks = { skipped: "sem pistas exportadas" };
    await tab("cars");
    if (typeof CAR_LIST !== "undefined" && CAR_LIST.length) {
      const sel = document.getElementById("car-open");
      if (sel) { sel.value = CAR_LIST[0].id; sel.dispatchEvent(new Event("change", { bubbles: true })); }
      const ok = await until(() => typeof cv !== "undefined" && cv.data, 30000);
      out.tabs.cars = { loaded: ok, id: CAR_LIST[0].id, nodes: cv.data && cv.data.tree ? 1 : 0 };
    } else out.tabs.cars = { skipped: "sem carros exportados" };
    await tab("screens");
  } catch (e) { out.errors.push("driver: " + String(e && e.stack || e)); }
  document.title = "SMOKE " + JSON.stringify(out);
})();
</script>
"""


def find_browser() -> str:
    names = [os.environ["BROWSER"]] if os.environ.get("BROWSER") else ["microsoft-edge-stable", "chromium", "google-chrome"]
    for name in names:
        path = shutil.which(name)
        if path:
            return path
    sys.exit("nenhum navegador encontrado (defina $BROWSER)")


def link_tree(src: str, dst: str, skip: set[str]) -> None:
    os.makedirs(dst, exist_ok=True)
    for name in os.listdir(src):
        if name not in skip:
            os.symlink(os.path.join(src, name), os.path.join(dst, name))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--site", default=os.path.join(ROOT, "build", "uiview"))
    parser.add_argument("--tracks", default=None)
    parser.add_argument("--budget", type=int, default=60000, help="tempo virtual máximo em ms")
    args = parser.parse_args()
    site = os.path.abspath(args.site)
    if not os.path.exists(os.path.join(site, "data", "ui.js")):
        sys.exit(f"{site} não tem uma exportação completa (data/ui.js). Rode: python -m tools.uiview --models 037 -o {args.site}")

    work = os.path.join(ROOT, "build", "web_smoke")
    shutil.rmtree(work, ignore_errors=True)
    os.makedirs(work)
    shutil.copytree(os.path.join(ROOT, "tools", "uiview", "web"), work, dirs_exist_ok=True)
    link_tree(site, work, {"index.html", "js", "css", "data", "tracks"})
    data = os.path.join(work, "data")
    link_tree(os.path.join(site, "data"), data, {"tracks.js"})
    track_src = os.path.abspath(args.tracks) if args.tracks else site
    for name in ("tracks", ):
        if os.path.isdir(os.path.join(track_src, name)):
            os.symlink(os.path.join(track_src, name), os.path.join(work, name))
    if os.path.exists(os.path.join(track_src, "data", "tracks.js")):
        os.symlink(os.path.join(track_src, "data", "tracks.js"), os.path.join(data, "tracks.js"))
    else:
        with open(os.path.join(data, "tracks.js"), "w", encoding="utf-8") as fh:
            fh.write("window.TRACK_DATA = [];\n")

    index = os.path.join(work, "index.html")
    with open(index, encoding="utf-8") as fh:
        page = fh.read()
    page = page.replace("<head>", "<head>\n" + HOOK, 1).replace("</body>", DRIVER + "</body>", 1)
    with open(index, "w", encoding="utf-8") as fh:
        fh.write(page)

    class Quiet(http.server.SimpleHTTPRequestHandler):
        def log_message(self, *args, **kwargs):
            pass

    handler = functools.partial(Quiet, directory=work)
    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    url = f"http://127.0.0.1:{server.server_address[1]}/index.html"

    profile = tempfile.mkdtemp(prefix="smoke_profile_")
    cmd = [find_browser(), "--headless=new", "--disable-gpu", "--no-sandbox", f"--user-data-dir={profile}",
           f"--virtual-time-budget={args.budget}", "--dump-dom", url]
    done = subprocess.run(cmd, capture_output=True, text=True, timeout=args.budget // 1000 + 120)
    server.shutdown()
    shutil.rmtree(profile, ignore_errors=True)
    match = re.search(r"<title>SMOKE (.*?)</title>", done.stdout, re.S)
    if not match:
        print(json.dumps({"errors": ["a página não terminou (sem título SMOKE)"], "stderr": done.stderr[-500:]}))
        return 1
    result = json.loads(html.unescape(match.group(1)))
    print(json.dumps(result, indent=2, ensure_ascii=False))
    return 1 if result["errors"] else 0


if __name__ == "__main__":
    raise SystemExit(main())
