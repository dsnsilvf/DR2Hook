#!/usr/bin/env python3
"""Do editor ao jogo num comando: porta o Ring para a overlay e abre o jogo direto na pista (AutoStage).

As etapas são as do docs/reverse_engineering/track_loading.md §12 (terreno, colisão, objetos, câmeras, vagas, tela
de carregamento). Cada saída fica em `build/re/ring_deploy/` e só é refeita quando o que ela lê mudou (assinatura
em `cache.json`: tamanho e data dos arquivos de entrada, conteúdo do edits.json, os scripts e os argumentos). Depois
copia para a overlay o que mudou e, se pedido, reabre o jogo:

- fecha o jogo aberto, apaga um `dr2hook_cmd.txt` velho (ele rodaria no boot), liga o AutoStage
  (`dr2hook_autostage.ini`: enabled = 1, once = 1, portugal / dr2hook_ring / route_0) e o SplashSkip
  (`dr2hook_intro.ini`: pular_splash=1; a `dxgi.dll` do jogo precisa tê-lo, ver boot_intro.md §4);
- abre pela Steam e acompanha o `dr2hook.log` fase por fase (MILESTONES): o jogo iniciou (AutoStage), os dados
  base, a pista (andamento pela contagem de arquivos abertos, comparada com a da última vez) e a largada. As linhas
  do jogo que interessam vão para o log; um crash (`[crash]` no log) para na hora, mesmo com a janela de erro do
  jogo ainda aberta;
- `--mode freecam`: na largada manda `key f9` pelo canal de comandos (câmera livre);
- `--mode bot`: o AutoStage é o benchmark do jogo, o carro anda sozinho.
  "Eu dirijo" ainda não existe: falta descobrir como o benchmark passa o controle ao jogador.

`--quick` pula a tela de carregamento (a foto aérea renderizada pelo viewer3d e o traçado): fica a da última vez. E
grava `dr2hook_quickload.ini` na pasta do jogo: o core deixa a tela preta com o log ao vivo do boot até a largada (e apaga o arquivo); com `--cover`, a foto da última tela de carregamento (`captures/overlay/dr2hook/loadcover_*.ppm`) fica atrás do log.

Progresso para o viewer3d (F5), uma linha por evento no stdout; o resto é log:

    @step <k> <n> <texto>     etapa k de n começou
    @progress <0..1>          andamento total (estimado pela duração da última vez)
    @done <texto>             terminou bem
    @fail <texto>             parou com erro (código de saída 1)

    python3 scripts/research/ring_deploy.py [--quick] [--mode bot|freecam] [--edits <edits.json>] [--no-game]
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import shutil
import signal
import subprocess
import sys
import threading
import time

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
GAME = os.environ.get("DR2_GAME_DIR", "/mnt/Jogos/SteamLibrary/steamapps/common/DiRT Rally 2.0")
TRACK_DIR = "examples/tracks/synthetic__dr2hook_ring"
OUT = "build/re/ring_deploy"
OVERLAY = "captures/overlay"
LOCATION, TRACK, ROUTE = "portugal", "dr2hook_ring", "route_0"
DEST = f"tracks/locations/{LOCATION}/{TRACK}"
EXE = "dirtrally2.exe"
CRASH_EXE = "CrashSender1405"  # a janela de erro do jogo (o nome do processo no Linux corta em 15 letras)
APP_ID = 690790
STEAM_URL = f"steam://rungameid/{APP_ID}"
# moldes da Montalegre que as etapas leem (extraídos antes; ver track_loading.md)
HOSTS = ("build/re/montalegre/tracksplit.pssg", "build/re/montalegre/route_0__track.vis",
         "build/re/montalegre_objects/objects.pssg", "build/re/montalegre_route0_orig/grids.pssg")
# código que as etapas usam: mudou, refaz
CODE = ("scripts/research", "tools/egodata", "tools/uiview/mesh.py")
# duração típica (s) quando ainda não há medida: só pesa a barra
GUESS = {"terrain": 2.0, "collision": 7.5, "objects": 1.0, "cameras": 0.3, "grids": 0.2, "loading": 20.0,
         "copy": 0.3, "close": 1.0, "open": 3.0, "boot": 15.0, "base": 4.0, "load": 4.0, "start": 2.0,
         "freecam": 0.5}
# fases do jogo no dr2hook.log, em ordem: (etapa, texto, marca que fecha a fase). Uma marca só vale depois da
# anterior (o "LoadTrace: IO" do boot não fecha nada); a da largada fecha todas.
MILESTONES = (
    ("boot", "Iniciando o jogo", "AutoStage: Fast-path"),
    ("base", "Carregando os dados do jogo", "RaceEvent: carregando"),
    ("load", "Carregando a pista", None),  # sem marca: pela contagem de arquivos abertos (wait_milestone)
    ("start", "Preparando a largada", "RaceEvent: largada"),
)
OPEN_MARK = "LoadTrace: open"  # um por arquivo aberto; contados a partir do "RaceEvent: carregando"
LOAD_OPENS_GUESS = 1600        # a carga do Ring de 2026-10-07 abriu 1708 e 1564
LOAD_QUIET = 1.5               # s sem abrir arquivo (com metade aberta) fecham a fase "load"
# linhas do jogo que vão para o log do viewer
GAME_LINE = re.compile(r"AutoStage: Fast-path|LoadProbe: pacote|RaceEvent|LoadCover|LoadTrace: IO|\[crash\]|\[ERROR\]")
CRASH = re.compile(r"\[crash\]: (excecao [^\n]*)")


class Report:
    """Linhas @ para o viewer; se o leitor sumir (viewer fechado), segue em silêncio."""

    def __init__(self, out=sys.stdout):
        self.out = out
        self.lock = threading.Lock()

    def line(self, text: str) -> None:
        with self.lock:
            if self.out is None:
                return
            try:
                self.out.write(text + "\n")
                self.out.flush()
            except (BrokenPipeError, OSError):
                self.out = None

    def log(self, text: str) -> None:
        for part in text.rstrip("\n").split("\n"):
            self.line(part[1:] if part.startswith("@") else part)  # log nunca vira comando


class Step:
    def __init__(self, key: str, label: str, run, inputs=(), args=(), after=(), outputs=(), cached=True):
        self.key, self.label, self.run = key, label, run
        self.inputs, self.args, self.after, self.outputs, self.cached = inputs, list(args), after, outputs, cached
        self.sig = ""
        self.skip = False


def files_under(path: str):
    if os.path.isfile(path):
        yield path
        return
    for root, dirs, names in os.walk(path):
        dirs[:] = sorted(d for d in dirs if d != "__pycache__")
        for n in sorted(names):
            if not n.endswith(".pyc") and not n.startswith(("ring_deploy", "test_")):
                yield os.path.join(root, n)


def signature(step: Step, sigs: dict[str, str], edits: str | None) -> str:
    h = hashlib.sha1()
    h.update(json.dumps(step.args).encode())
    for dep in step.after:
        h.update(sigs[dep].encode())
    for p in step.inputs:
        if p == edits:
            h.update(open(p, "rb").read() if p and os.path.exists(p) else b"-")
            continue
        for f in files_under(p):
            st = os.stat(f)
            h.update(f"{f}|{st.st_size}|{st.st_mtime_ns}\n".encode())
    return h.hexdigest()


def run_script(rep: Report, argv: list[str]) -> None:
    """Roda um script do porte repassando a saída como log; erro vira exceção com o fim da saída."""
    p = subprocess.Popen([sys.executable, "-u", *argv], cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                         text=True, errors="replace")
    tail = []
    for line in p.stdout:
        rep.log("  " + line.rstrip())
        tail = (tail + [line.rstrip()])[-6:]
    if p.wait():
        raise RuntimeError(f"{os.path.basename(argv[0])} falhou (código {p.returncode}): " + " | ".join(tail[-3:]))


def game_pids(names: tuple[str, ...] = (EXE,)) -> list[int]:
    pids = []
    for name in names:
        r = subprocess.run(["pgrep", "-x", name], capture_output=True, text=True)
        pids += [int(x) for x in r.stdout.split()]
    return pids


def prefix_servers() -> list[int]:
    """wineservers do prefixo do jogo (SteamAppId no ambiente): o jogo só fechou de vez quando eles saem."""
    pids = []
    for pid in subprocess.run(["pgrep", "-x", "wineserver"], capture_output=True, text=True).stdout.split():
        try:
            env = open(f"/proc/{pid}/environ", "rb").read().split(b"\0")
        except OSError:
            continue
        if f"SteamAppId={APP_ID}".encode() in env:
            pids.append(int(pid))
    return pids


def set_ini(path: str, values: dict[str, str]) -> None:
    text = open(path, encoding="utf-8", errors="replace").read()
    for k, v in values.items():
        text, n = re.subn(rf"(?m)^{k}\s*=.*$", f"{k} = {v}", text)
        if not n:
            text = text.rstrip("\n") + f"\n{k} = {v}\n"
    with open(path, "w", encoding="utf-8", newline="") as fh:
        fh.write(text)


def force_splash_skip(path: str) -> None:
    """O editor abre o jogo sempre sem o splash do logo: pular_splash=1 no dr2hook_intro.ini (a dxgi lê `chave=valor`, sem espaços)."""
    lines = open(path, encoding="utf-8").read().splitlines() if os.path.exists(path) else []
    lines = [l for l in lines if not l.startswith("pular_splash=")] + ["pular_splash=1"]
    with open(path, "w", encoding="utf-8") as fh:
        fh.write("\n".join(lines) + "\n")


class Deploy:
    def __init__(self, a, rep: Report):
        self.a, self.rep = a, rep
        self.out = os.path.join(ROOT, OUT)
        self.cache_path = os.path.join(self.out, "cache.json")
        try:
            self.cache = json.load(open(self.cache_path))
        except (OSError, ValueError):
            self.cache = {}
        self.t0 = 0.0
        self.log_pos, self.reached, self.opens, self.last_open = 0, -1, 0, 0.0
        self.sub: float | None = None

    # ---- etapas do porte

    def steps(self) -> list[Step]:
        a, o = self.a, OUT
        track = [TRACK_DIR]
        code = list(CODE)
        edits = a.edits
        s = [
            Step("terrain", "Terreno (tracksplit.pssg)",
                 lambda: run_script(self.rep, ["scripts/research/ring_tracksplit.py", f"{o}/terrain/tracksplit.pssg",
                                               "--host", HOSTS[0]]),
                 inputs=track + code + [HOSTS[0]], outputs=[f"{o}/terrain/tracksplit.pssg"]),
            Step("collision", "Colisão (track.jpk)",
                 lambda: run_script(self.rep, ["scripts/research/track_jpk.py", "ring", f"{o}/terrain/{ROUTE}/track.jpk",
                                               "--bg-margin", "200"]),
                 inputs=track + code, outputs=[f"{o}/terrain/{ROUTE}/track.jpk"]),
            Step("objects", "Objetos e decoração" + (" (com as edições)" if edits else ""),
                 lambda: run_script(self.rep, ["scripts/research/ring_objects.py", f"{o}/objects", "--tracksplit",
                                               f"{o}/terrain/tracksplit.pssg", "--kinds", "eot"]
                                    + (["--edits", edits] if edits else [])),
                 inputs=track + code + [p for p in HOSTS[1:3]] + ([edits] if edits else []), args=[edits or ""],
                 after=["terrain"], outputs=[f"{o}/objects/objects.pssg", f"{o}/objects/{ROUTE}/track.vis"]),
            Step("cameras", "Câmeras do replay",
                 lambda: run_script(self.rep, ["scripts/research/ring_cameras.py", f"{o}/cameras"]),
                 inputs=track + code, outputs=[f"{o}/cameras/replay_camera_config.xml"]),
            Step("grids", "Vagas de largada",
                 lambda: run_script(self.rep, ["scripts/research/ring_grids.py", f"{o}/grids"]),
                 inputs=track + code + [HOSTS[3]], outputs=[f"{o}/grids/grids.pssg"]),
        ]
        if not a.quick:
            s.append(Step("loading", "Tela de carregamento (foto aérea)",
                          lambda: run_script(self.rep, ["scripts/research/loading_screen.py", "--overlay", OVERLAY,
                                                        "--name", TRACK, "--layout", f"{TRACK_DIR}/source/layout.json",
                                                        "--track-dir", TRACK_DIR]),
                          inputs=track + code + ["build/viewer3d/viewer3d"],
                          outputs=[f"{OVERLAY}/frontend", f"{OVERLAY}/dr2hook/loadcover_{TRACK}_{ROUTE}.ppm"]))
        s.append(Step("copy", "Copiando para a overlay", self.copy, cached=False))
        if not a.no_game:
            s.append(Step("close", "Fechando o jogo aberto", self.close_game, cached=False))
            s.append(Step("open", "Abrindo o jogo (Steam)", self.open_game, cached=False))
            for i, (key, label, _mark) in enumerate(MILESTONES):
                s.append(Step(key, label, lambda i=i: self.wait_milestone(i), cached=False))
            if a.mode == "freecam":
                s.append(Step("freecam", "Câmera livre (F9)", self.freecam, cached=False))
        return s

    def plan(self, steps: list[Step]) -> None:
        sigs: dict[str, str] = {}
        for st in steps:
            if not st.cached:
                continue
            st.sig = signature(st, sigs, self.a.edits)
            sigs[st.key] = st.sig
            have = all(os.path.exists(os.path.join(ROOT, p)) for p in st.outputs)
            st.skip = have and self.cache.get(st.key, {}).get("sig") == st.sig and not self.a.force

    def copy(self) -> None:
        o = OUT
        files = {
            "tracksplit.pssg": f"{o}/terrain/tracksplit.pssg",
            f"{ROUTE}/track.jpk": f"{o}/terrain/{ROUTE}/track.jpk",
            "objects.pssg": f"{o}/objects/objects.pssg",
            "objectstextures.pssg": f"{o}/objects/objectstextures.pssg",
            "ornaments_references.xml": f"{o}/objects/ornaments_references.xml",
            f"{ROUTE}/ornaments.bin": f"{o}/objects/{ROUTE}/ornaments.bin",
            f"{ROUTE}/track.vis": f"{o}/objects/{ROUTE}/track.vis",
            f"{ROUTE}/replay_camera_config.xml": f"{o}/cameras/replay_camera_config.xml",
            f"{ROUTE}/cameralines.cqtc": f"{o}/cameras/cameralines.cqtc",
            f"{ROUTE}/grids.pssg": f"{o}/grids/grids.pssg",
        }
        dest = os.path.join(ROOT, OVERLAY, DEST)
        changed = 0
        for rel, src in files.items():
            src, dst = os.path.join(ROOT, src), os.path.join(dest, rel)
            if not os.path.exists(src):
                raise RuntimeError(f"faltou {src}")
            if os.path.exists(dst) and os.path.getsize(dst) == os.path.getsize(src) \
                    and open(dst, "rb").read() == open(src, "rb").read():
                continue
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            shutil.copyfile(src, dst)
            changed += 1
            self.rep.log(f"  {rel}")
        self.rep.log(f"  {changed} de {len(files)} arquivos mudaram na overlay")

    # ---- jogo

    def close_game(self) -> None:
        # a janela de erro do jogo (CrashRpt) segura a Steam: com ela aberta, o jogo "ainda roda" e não abre de novo
        names = (EXE, CRASH_EXE)
        if not game_pids(names):
            self.rep.log("  jogo fechado")
            return
        for name in names:
            subprocess.run(["pkill", "-TERM", "-x", name])
        for _ in range(40):
            if not game_pids(names):
                break
            time.sleep(0.5)
        for name in names:
            if game_pids((name,)):
                subprocess.run(["pkill", "-KILL", "-x", name])
        # um boot com o wineserver do jogo anterior ainda saindo crashou uma vez (exe+0x8d1d20, 2026-10-07)
        for _ in range(30):
            if not prefix_servers():
                break
            time.sleep(0.5)
        time.sleep(2.0)  # a Steam leva um instante para ver o jogo fechado
        self.rep.log("  jogo anterior fechado")

    def open_game(self) -> None:
        g = self.a.game
        try:
            os.remove(os.path.join(g, "dr2hook_cmd.txt"))
        except FileNotFoundError:
            pass
        set_ini(os.path.join(g, "dr2hook_autostage.ini"),
                {"enabled": "1", "once": "1", "location": LOCATION, "track": TRACK, "route": ROUTE})
        force_splash_skip(os.path.join(g, "dr2hook_intro.ini"))
        quick = os.path.join(g, "dr2hook_quickload.ini")
        if self.a.quick:
            cover = os.path.join(ROOT, OVERLAY, "dr2hook", f"loadcover_{TRACK}_{ROUTE}.ppm")
            with open(quick, "w", encoding="utf-8") as fh:
                fh.write("; ring_deploy.py --quick: o core deixa a tela preta ate a largada e apaga este arquivo\n")
                if self.a.cover and os.path.exists(cover):
                    # com --cover, a foto da última tela de carregamento gerada atrás do terminal (caminho do Wine: Z: = /)
                    fh.write("fundo=Z:" + os.path.abspath(cover).replace("/", "\\") + "\n")
        elif os.path.exists(quick):
            os.remove(quick)
        self.t0 = time.time()
        self.log_pos, self.reached, self.opens, self.last_open = 0, -1, 0, 0.0
        steam = shutil.which("steam")
        if not steam:
            raise RuntimeError("não achei o comando steam")
        subprocess.Popen([steam, STEAM_URL], stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                         stderr=subprocess.DEVNULL, start_new_session=True)
        for _ in range(240):
            if game_pids():
                self.rep.log("  processo do jogo aberto")
                return
            time.sleep(0.5)
        raise RuntimeError("o jogo não abriu em 2 minutos")

    def poll_log(self) -> None:
        """Lê o que entrou no dr2hook.log desde a última vez: marcos, arquivos abertos, linhas para o log, crash."""
        p = os.path.join(self.a.game, "dr2hook.log")
        try:
            if os.path.getmtime(p) < self.t0:
                return  # ainda o log da sessão anterior
            if os.path.getsize(p) < self.log_pos:  # o boot trunca o log
                self.log_pos = 0
            with open(p, "rb") as fh:  # em bytes: o log tem \r\n, e a posição tem que ser a do arquivo
                fh.seek(self.log_pos)
                chunk = fh.read()
        except OSError:
            return
        end = chunk.rfind(b"\n") + 1  # só linhas completas
        self.log_pos += end
        last = len(MILESTONES) - 1
        for line in chunk[:end].decode("utf-8", errors="replace").splitlines():
            if OPEN_MARK in line and self.reached >= 1:
                self.opens += 1
                self.last_open = time.time()
            for i, (_k, _l, mark) in enumerate(MILESTONES):
                if mark and mark in line and (self.reached >= i - 1 or i == last):
                    self.reached = max(self.reached, i)
            if GAME_LINE.search(line):
                self.rep.log("  jogo: " + re.sub(r"^\[[^\]]*\] \[\w+\] ", "", line))
            m = CRASH.search(line)
            if m:
                raise RuntimeError(f"o jogo travou ({m.group(1)}); veja o dr2hook.log")

    def wait_milestone(self, i: int) -> None:
        """Espera a fase i fechar (ou uma posterior)."""
        key = MILESTONES[i][0]
        expected = self.cache.get("load", {}).get("opens", LOAD_OPENS_GUESS)
        deadline = time.time() + 300
        while time.time() < deadline:
            self.poll_log()
            if key == "load":
                self.sub = min(self.opens / max(expected, 1), 0.98)
                quiet = time.time() - self.last_open > LOAD_QUIET
                if self.opens >= 0.97 * expected or (self.opens >= 0.5 * expected and quiet):
                    self.reached = max(self.reached, i)
            if self.reached >= i:
                if key == "start" and self.opens:
                    self.cache.setdefault("load", {})["opens"] = self.opens  # a próxima carga compara com esta
                return
            if not game_pids():
                raise RuntimeError("o jogo fechou durante a carga (crash?); veja o dr2hook.log")
            time.sleep(0.2)
        raise RuntimeError(f"{MILESTONES[i][1].lower()}: nada em 5 minutos")

    def freecam(self) -> None:
        path = os.path.join(self.a.game, "dr2hook_cmd.txt")
        with open(path, "w", encoding="utf-8") as fh:
            fh.write("key f9\n")
        for _ in range(40):  # o core lê e apaga em ~150 ms
            if not os.path.exists(path):
                self.rep.log("  câmera livre ligada (F9 alterna; o carro segue com o bot)")
                return
            time.sleep(0.1)
        raise RuntimeError("o jogo não leu o dr2hook_cmd.txt")

    # ---- execução

    def preflight(self) -> None:
        missing = [p for p in HOSTS + (TRACK_DIR,) if not os.path.exists(os.path.join(ROOT, p))]
        if missing:
            raise RuntimeError("faltam os moldes da Montalegre: " + ", ".join(missing))
        if not os.path.isdir(os.path.join(ROOT, OVERLAY, DEST)):
            raise RuntimeError(f"a overlay não tem {DEST} (veja custom_track.py)")
        if self.a.edits and not os.path.exists(self.a.edits):
            raise RuntimeError(f"não achei {self.a.edits}")
        if self.a.no_game:
            return
        ini = os.path.join(self.a.game, "dr2hook_loadprobe.ini")
        if not os.path.exists(os.path.join(self.a.game, "dr2hook_autostage.ini")):
            raise RuntimeError(f"não achei dr2hook_autostage.ini em {self.a.game}")
        dxgi = os.path.join(self.a.game, "dxgi.dll")
        if not os.path.exists(dxgi) or b"SplashSkip" not in open(dxgi, "rb").read():
            raise RuntimeError("a dxgi.dll do jogo não tem o SplashSkip (copie a do build/win-redirect)")
        text = open(ini, encoding="utf-8", errors="replace").read() if os.path.exists(ini) else ""
        if not re.search(r"(?m)^enabled\s*=\s*1", text) or "overlay_dir" not in text \
                or f"track_alias = {TRACK}=" not in text:
            raise RuntimeError("o dr2hook_loadprobe.ini não está pronto (enabled = 1, overlay_dir e track_alias do Ring)")

    def run(self) -> int:
        try:
            self.preflight()
            steps = self.steps()
            self.plan(steps)
        except Exception as e:  # noqa: BLE001
            self.rep.line(f"@fail {e}")
            return 1
        weight = [0.02 if st.skip else self.cache.get(st.key, {}).get("secs", GUESS.get(st.key, 1.0)) for st in steps]
        total = sum(weight) or 1.0
        done = 0.0
        for k, st in enumerate(steps):
            self.rep.line(f"@step {k + 1} {len(steps)} {st.label}")
            if st.skip:
                self.rep.log("  sem mudança: mantida a saída anterior")
                done += weight[k]
                self.rep.line(f"@progress {done / total:.4f}")
                continue
            stop = threading.Event()
            self.sub = None  # andamento dentro da etapa, quando ela sabe (senão, pelo tempo da última vez)

            def tick(base=done, w=weight[k], start=time.time()):
                while not stop.wait(0.1):
                    frac = self.sub if self.sub is not None else min((time.time() - start) / max(w, 0.05), 0.95)
                    self.rep.line(f"@progress {(base + frac * w) / total:.4f}")

            th = threading.Thread(target=tick, daemon=True)
            th.start()
            start = time.time()
            if self.cache.get(st.key, {}).pop("sig", None):  # interrompida no meio, a saída não vale
                self.save_cache()
            for out in st.outputs:  # o ring_tracksplit.py não cria a pasta de saída
                os.makedirs(os.path.dirname(os.path.join(ROOT, out)), exist_ok=True)
            try:
                st.run()
            except Exception as e:  # noqa: BLE001 - qualquer erro vira @fail para o viewer
                stop.set()
                th.join()
                self.rep.line(f"@fail {st.label}: {e}")
                return 1
            finally:
                stop.set()
                th.join()
            secs = time.time() - start
            self.rep.log(f"  {secs:.1f} s")
            entry = self.cache.setdefault(st.key, {})
            entry["secs"] = round(secs, 2)
            if st.cached:
                entry["sig"] = st.sig
            self.save_cache()
            done += weight[k]
            self.rep.line(f"@progress {done / total:.4f}")
        if self.a.no_game:
            self.rep.line("@done overlay pronta (o jogo não foi aberto)")
        else:
            self.rep.line("@done na pista" + (" com a câmera livre" if self.a.mode == "freecam" else ", o bot dirige"))
        return 0

    def save_cache(self) -> None:
        os.makedirs(self.out, exist_ok=True)
        tmp = self.cache_path + ".tmp"
        with open(tmp, "w") as fh:
            json.dump(self.cache, fh, indent=1)
        os.replace(tmp, self.cache_path)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--quick", action="store_true", help="pula a tela de carregamento (foto aérea e traçado)")
    ap.add_argument("--cover", action="store_true", help="com --quick, a foto aérea atrás do log (sem ela, tela preta)")
    ap.add_argument("--mode", choices=("bot", "freecam", "drive"), default="bot")
    ap.add_argument("--edits", help="edits.json do viewer3d (objetos movidos, apagados e copiados na route_0)")
    ap.add_argument("--no-game", action="store_true", help="só porta para a overlay, sem abrir o jogo")
    ap.add_argument("--force", action="store_true", help="refaz todas as etapas, mesmo sem mudança")
    ap.add_argument("--game", default=GAME)
    a = ap.parse_args()
    if a.edits:
        a.edits = os.path.abspath(a.edits)
    os.chdir(ROOT)
    signal.signal(signal.SIGTERM, lambda *_: sys.exit(1))
    rep = Report()
    if a.mode == "drive":
        rep.line("@fail \"Eu dirijo\" ainda não existe: o AutoStage usa o benchmark, e o carro anda sozinho")
        return 1
    return Deploy(a, rep).run()


if __name__ == "__main__":
    sys.exit(main())
