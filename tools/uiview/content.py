"""Imagens e diálogos do frontend para a galeria do visualizador.

Imagens: todas as texturas dos bundles PSSG (`frontend/bundles/*.pssg`) e as
`frontend/streamed_textures/**/*.tpk` (carregamento, traçados das especiais,
pinturas). Saem em WebP (máx. 2048 px) com miniatura de 256 px; arquivos já
gerados são reaproveitados.

`.tpk`: cabeçalho de 128 bytes, little-endian. `u32[10]` formato (49 = BC1,
38 = BC7), `u32[11]` largura, `u32[12]` altura, `u32[14]` tamanho dos dados,
nome a partir de 0x44; os dados começam em 0x80.
"""

from __future__ import annotations

import io
import json
import os
import struct
from concurrent.futures import ThreadPoolExecutor
from typing import Any, Callable

from tools.egodata import cfgxml
from tools.pssg import PSSGFile

FULL_MAX = 2048
THUMB_MAX = 256
ARCHIVES = ("game_1.dat", "game.nefs", "game.dat")
TPK_FORMATS = {49: ("DXT1", None), 38: (None, 98)}
DIALOGS = ["frontend/message_dialogs/flow.xml", "frontend/message_dialogs/network.xml",
           "frontend/message_dialogs/save.xml"]


def safe_name(name: str) -> str:
    return "".join(c if c.isalnum() or c in "-_." else "_" for c in name)


def dds(width: int, height: int, data: bytes, fourcc: str | None, dxgi: int | None) -> bytes:
    header = struct.pack(
        "<4s7I44s2I4s5I5I", b"DDS ", 124, 0x1 | 0x2 | 0x4 | 0x1000 | 0x80000, height, width,
        len(data), 0, 1, b"\0" * 44, 32, 4, (fourcc or "DX10").encode(), 0, 0, 0, 0, 0, 0x1000, 0, 0, 0, 0)
    if dxgi is not None:
        header += struct.pack("<5I", dxgi, 3, 0, 1, 0)
    return header + data


def decode_texture(fmt: str, width: int, height: int, data: bytes):
    """Imagem PIL de uma TEXTURE de PSSG, ou None se o formato não é lido."""
    from PIL import Image

    pixels = width * height
    if fmt == "u8" and len(data) >= pixels:
        return Image.frombytes("L", (width, height), data[:pixels])
    if fmt == "ui8x4" and len(data) >= pixels * 4:
        # BGRA na memória (D3D); troca para RGBA.
        b, g, r, a = Image.frombytes("RGBA", (width, height), data[: pixels * 4]).split()
        return Image.merge("RGBA", (r, g, b, a))
    if str(fmt).startswith("BC7"):
        image = Image.open(io.BytesIO(dds(width, height, data, None, 99)))
        image.load()
        return image
    fourcc = {"dxt1": "DXT1", "dxt3": "DXT3", "dxt5": "DXT5"}.get(str(fmt).split("_", 1)[0])
    if fourcc and width >= 4 and height >= 4:
        image = Image.open(io.BytesIO(dds(width, height, data, fourcc, None)))
        image.load()
        return image
    return None


def save_pair(image, full_path: str, thumb_path: str) -> None:
    os.makedirs(os.path.dirname(full_path), exist_ok=True)
    os.makedirs(os.path.dirname(thumb_path), exist_ok=True)
    if image.mode not in ("RGB", "RGBA", "L"):
        image = image.convert("RGBA")
    full = image.copy()
    full.thumbnail((FULL_MAX, FULL_MAX))
    full.save(full_path, "WEBP", quality=88, method=4)
    thumb = image.copy()
    thumb.thumbnail((THUMB_MAX, THUMB_MAX))
    thumb.save(thumb_path, "WEBP", quality=80, method=4)


class ContentExporter:
    def __init__(self, reader, out: str, force: bool, log: Callable[[str], None]):
        self.reader = reader
        self.out = out
        self.force = force
        self.log = log
        self.assets: list[dict[str, Any]] = []
        self.by_name: dict[str, dict[str, Any]] = {}
        self.unknown: set[str] = set()

    def _paths(self, group: str, name: str) -> tuple[str, str]:
        rel = f"{group}/{safe_name(name)}.webp"
        return "img/" + rel, "thumb/" + rel

    only: set[str] | None = None

    def _add(self, asset: dict[str, Any], job) -> None:
        if self.only is not None and asset["n"] not in self.only:
            return
        self.assets.append(asset)
        # Para desenhar as cenas vale a primeira ocorrência (patch antes da base).
        self.by_name.setdefault(asset["n"], asset)
        full = os.path.join(self.out, asset["p"])
        if self.force or not os.path.exists(full):
            self._jobs.append((job, full, os.path.join(self.out, asset["t"])))

    def _flush(self) -> None:
        """Gera as imagens pendentes e libera os dados brutos."""
        def work(item):
            job, full, thumb = item
            try:
                image = job()
            except Exception as exc:
                return f"{os.path.basename(full)}: {exc}"
            if image is None:
                return f"{os.path.basename(full)}: formato não lido"
            save_pair(image, full, thumb)
            return None

        for error in self._pool.map(work, self._jobs):
            if error:
                self._failed.add(error.split(":")[0])
                if len(self._failed) <= 5:
                    self.log(f"  falhou {error}")
            else:
                self._done += 1
        self._jobs = []

    def _list(self, prefix: str, suffix: str) -> list[tuple[str, str]]:
        found: dict[str, str] = {}
        for archive in ARCHIVES:
            try:
                entries = self.reader.entries(archive)
            except Exception:
                continue
            items = self.reader.items(archive)
            for entry in entries:
                if (entry.is_file and entry.size and entry.id in items
                        and entry.path.startswith(prefix) and entry.path.endswith(suffix)):
                    found.setdefault(entry.path, archive)
        return sorted(((a, p) for p, a in found.items()), key=lambda x: ("patch" not in x[1], x[1]))

    def bundles(self) -> None:
        from tools.uiview.export import _attr, _index

        for archive, path in self._list("frontend/bundles/", ".pssg"):
            group = os.path.basename(path)[:-5]
            self.log(f"imagens: {path}")
            try:
                pssg = PSSGFile(self.reader.read(archive, path))
            except Exception as exc:
                self.log(f"  não lido {path}: {exc}")
                continue
            for node in _index(pssg.root).values():
                if node.type_name != "TEXTURE":
                    continue
                name = str(_attr(node, "id"))
                width, height, fmt = int(_attr(node, "width")), int(_attr(node, "height")), str(_attr(node, "texelFormat"))
                block = next((c for c in node.children if c.type_name == "TEXTUREIMAGEBLOCK"), None)
                data = next((c.data for c in block.children if c.data), None) if block else None
                if not data:
                    continue
                full, thumb = self._paths(group, name)
                asset = {"n": name, "g": group, "w": width, "h": height, "f": fmt, "p": full, "t": thumb}
                self._add(asset, (lambda f=fmt, w=width, h=height, d=data: decode_texture(f, w, h, d)))
            del pssg
            self._flush()

    def streamed(self) -> None:
        from PIL import Image

        for archive, path in self._list("frontend/streamed_textures/", ".tpk"):
            parts = path.split("/")
            group = "streamed_" + parts[2]
            name = os.path.basename(path)[:-4]
            try:
                data = self.reader.read(archive, path)
            except Exception as exc:
                self.log(f"  não lido {path}: {exc}")
                continue
            head = struct.unpack_from("<16I", data, 0)
            code, width, height, size = head[10], head[11], head[12], head[14]
            full, thumb = self._paths(group, name)
            asset = {"n": name, "g": group, "w": width, "h": height, "f": f"tpk{code}", "p": full, "t": thumb}
            if code not in TPK_FORMATS:
                self.unknown.add(f"tpk {code}")
                continue

            def job(d=data, w=width, h=height, s=size, c=code):
                fourcc, dxgi = TPK_FORMATS[c]
                image = Image.open(io.BytesIO(dds(w, h, d[0x80 : 0x80 + s], fourcc, dxgi)))
                image.load()
                return image

            self._add(asset, job)
            if len(self._jobs) >= 64:
                self._flush()
        self._flush()
        self.log(f"imagens: {sum(1 for a in self.assets if a['g'].startswith('streamed_'))} streamed")

    def _signature(self) -> list[Any]:
        sig = []
        for prefix, suffix in (("frontend/bundles/", ".pssg"), ("frontend/streamed_textures/", ".tpk")):
            for archive, path in self._list(prefix, suffix):
                sig.append([archive, path])
        for archive in ARCHIVES:
            full = os.path.join(self.reader.game, "game", archive)
            if os.path.exists(full):
                st = os.stat(full)
                sig.append([archive, st.st_size, int(st.st_mtime)])
        return sig

    def run(self) -> list[dict[str, Any]]:
        # Índice em cache: se os pacotes não mudaram e as imagens existem,
        # não reabre os bundles (a leitura dos PSSG leva minutos).
        cache_path = os.path.join(self.out, "data", "content_cache.json")
        signature = self._signature()
        if not self.force and self.only is None and os.path.exists(cache_path):
            try:
                cached = json.load(open(cache_path, encoding="utf-8"))
                if cached.get("sig") == signature and all(
                        os.path.exists(os.path.join(self.out, a["p"])) for a in cached["assets"]):
                    self.log(f"imagens: {len(cached['assets'])} do cache")
                    for asset in cached["assets"]:
                        self.by_name.setdefault(asset["n"], asset)
                    return cached["assets"]
            except (OSError, ValueError, KeyError):
                pass
        self._jobs: list[Any] = []
        self._failed: set[str] = set()
        self._done = 0
        with ThreadPoolExecutor(max_workers=os.cpu_count() or 4) as pool:
            self._pool = pool
            self.bundles()
            self.streamed()
        self.log(f"imagens: {len(self.assets)} no total, {self._done} geradas agora")
        if self._failed:
            self.log(f"imagens: {len(self._failed)} não geradas")
        for message in sorted(self.unknown):
            self.log(f"imagens: formato não lido: {message}")
        ok = [a for a in self.assets if os.path.exists(os.path.join(self.out, a["p"]))]
        self.by_name = {}
        for asset in ok:
            self.by_name.setdefault(asset["n"], asset)
        if self.only is None:
            os.makedirs(os.path.dirname(cache_path), exist_ok=True)
            json.dump({"sig": signature, "assets": ok}, open(cache_path, "w", encoding="utf-8"))
        return ok


def texture_name(tex_id: str, surface: str = "") -> str:
    """Nome do arquivo. `surface` (`tm`, `gr`, `sn`, `wt`) separa a roda de cada piso.

    Os quatro PSSG de piso repetem o mesmo id (`037_wheel_d.tga`). Sem o sufixo o
    primeiro piso gravado fica no lugar dos outros.
    """
    base = safe_name(tex_id)
    if not surface or "wheel" not in base.lower():
        return base
    root, dot, ext = base.rpartition(".")
    if dot:
        return f"{root}_{surface}.{ext}"
    return f"{base}_{surface}"


def export_pssg_images(data: bytes, group: str, out: str, force: bool, log: Callable[[str], None],
                       surface: str = "") -> list[dict[str, Any]]:
    """Texturas TEXTURE de um PSSG, no mesmo WebP da galeria. Libera o arquivo ao terminar."""
    from tools.uiview.export import _attr, _index

    pssg = PSSGFile(data)
    extracted: list[tuple[str, int, int, str, bytes]] = []
    used: set[str] = set()
    try:
        for node in _index(pssg.root).values():
            if node.type_name != "TEXTURE":
                continue
            name = str(_attr(node, "id") or "")
            width, height = int(_attr(node, "width") or 0), int(_attr(node, "height") or 0)
            fmt = str(_attr(node, "texelFormat") or "")
            block = next((c for c in node.children if c.type_name == "TEXTUREIMAGEBLOCK"), None)
            blob = next((c.data for c in block.children if c.data), None) if block else None
            if not name or not blob or width < 1 or height < 1:
                continue
            base = texture_name(name, surface)
            unique = base
            n = 2
            while unique in used:
                unique = f"{base}_{n}"
                n += 1
            used.add(unique)
            extracted.append((unique, width, height, fmt, bytes(blob)))
    finally:
        del pssg

    assets: list[dict[str, Any]] = []
    jobs = []
    for name, width, height, fmt, blob in extracted:
        rel = f"{group}/{name}.webp"
        asset = {"n": name, "g": group, "w": width, "h": height, "f": fmt, "p": "img/" + rel, "t": "thumb/" + rel}
        assets.append(asset)
        full = os.path.join(out, asset["p"])
        if force or not os.path.exists(full):
            jobs.append((fmt, width, height, blob, full, os.path.join(out, asset["t"]), name))

    def work(item):
        fmt, width, height, blob, full, thumb, name = item
        try:
            image = decode_texture(fmt, width, height, blob)
        except Exception as exc:
            return f"{name}: {exc}"
        if image is None:
            return f"{name}: formato não lido ({fmt})"
        try:
            save_pair(image, full, thumb)
        finally:
            image.close()
        return None

    failed = 0
    if jobs:
        with ThreadPoolExecutor(max_workers=min(8, os.cpu_count() or 4)) as pool:
            for error in pool.map(work, jobs):
                if error:
                    failed += 1
                    if failed <= 5:
                        log(f"  textura {error}")
    del extracted
    return [a for a in assets if os.path.exists(os.path.join(out, a["p"]))]


def export_dialogs(reader) -> list[dict[str, Any]]:
    out = []
    for path in DIALOGS:
        root = cfgxml.decode(reader.read_any(path))
        source = os.path.basename(path)[:-4]
        for message in root[3]:
            parts = [{"t": part[0], "a": dict(part[2])} for part in message[3]]
            out.append({"src": source, "a": dict(message[2]), "parts": parts})
    return out
