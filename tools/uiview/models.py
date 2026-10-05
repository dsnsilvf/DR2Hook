"""Índice e malhas 3D dos pacotes do jogo para o visualizador.

O índice lista carros (`cars/<id>.nefs`), personagens e genéricos de `game.nefs`
e objetos de pista (`locations/*.nefs`). A malha sai em `models/*.bin` para o
preview. Arquivos acima de 120 MB (terreno `tracksplit`, por exemplo) ficam só
no índice. Sem `--models`, a malha dos arquivos principais de até 40 MB é
exportada; com `--models`, só o que o filtro pede. Com `--all-models`, entra
tudo que cabe no teto de 120 MB: carro, personagem, local, prop, interior e
LOD, um pacote por vez, reaproveitando imagem e malha já gravadas.

As pinturas do carro não são os `.tpk` sem payload de `game_1.dat`. Elas estão
no próprio pacote, em `livery_00/textures_high/*.pssg`.
"""

from __future__ import annotations

import gc
import json
import os
from typing import Any, Callable

from tools.egodata import bxml
from tools.egodata.nefs import NefsArchive
from tools.pssg import PSSGFile
from tools.uiview.content import export_pssg_images, safe_name
from tools.uiview.carmodel import CAR_REV, build_car_model, pack_resources
from tools.uiview.mesh import extract_meshes, pack_geom, summarize_geom

# Revisão do formato da malha e das regras de seleção. Entra na assinatura do cache.
MESH_REV = 2
HARD_CAP = 120 * 1024 * 1024
SOFT_CAP = 40 * 1024 * 1024
PRIMARY = {"carro", "personagem", "local", "prop", "generico"}
# Tipos que ganham a árvore estrutural do Car Model Explorer (`models/<id>.car.json`).
CAR_KINDS = {"carro", "lod", "interior"}


def classify_path(path: str) -> str | None:
    """Tipo de modelo, ou None se o caminho não é uma malha que a gente abre."""
    base = os.path.basename(path)
    low = path.lower()
    if base == "props.pssg" and "/anims/" in low:
        return "prop"
    if any(s in low for s in ("/livery_", "/decals/", "decal_", "/skies/", "sponsor_", "_textures", "texture", "_anims", "/anims/")):
        return None
    if base.endswith("_highLOD.pssg"):
        return "carro"
    if base.endswith("_lowLOD.pssg"):
        return "lod"
    if base.startswith("int_") and base.endswith(".pssg"):
        return "interior"
    if base in ("codriver.pssg", "driver.pssg"):
        return "personagem"
    if base == "pace_notes.pssg":
        return "prop"
    if base in ("objects.pssg", "route_objects.pssg", "trees.pssg"):
        return "local"
    if base.startswith("car_gen") and "_high" in base and base.endswith(".pssg"):
        return "generico"
    if base.startswith("car_genglobal") and "high" in base and base.endswith(".pssg"):
        return "generico"
    return None


def parse_decision(kind: str | None, size: int, src: str, path: str, tokens: list[str] | None,
                   soft: int = SOFT_CAP, hard: int = HARD_CAP, all_models: bool = False) -> str:
    """`parse` abre o PSSG; `index` só registra; `skip` ignora o caminho."""
    if kind is None:
        return "skip"
    if size > hard:
        return "index"
    if all_models:
        return "parse"
    tokens = [t.strip() for t in (tokens or []) if t.strip()]
    if not tokens:
        return "parse" if kind in PRIMARY and size <= soft else "index"
    stem = os.path.basename(src).replace(".nefs", "").lower()
    base = os.path.basename(path).lower()
    hay = f"{src}/{path}".lower()
    for raw in tokens:
        t = raw.lower()
        if t.endswith(".pssg") and t in hay:
            return "parse"
        if t != stem and t not in hay:
            continue
        if t == stem or t in hay:
            if kind in PRIMARY:
                return "parse"
            if t in base and t != stem:
                return "parse"
    return "index"


def package_list(game: str) -> list[str]:
    rels = []
    for name in ("game.nefs", "game.dat", "game_1.dat", "game_2.dat", "game_2_1.dat"):
        if os.path.exists(os.path.join(game, "game", name)):
            rels.append(f"game/{name}")
    for folder in ("cars", "locations"):
        folder_path = os.path.join(game, folder)
        if not os.path.isdir(folder_path):
            continue
        for name in sorted(os.listdir(folder_path)):
            if name.endswith(".nefs"):
                rels.append(f"{folder}/{name}")
    return rels


def open_package(game: str, rel: str) -> NefsArchive:
    path = os.path.join(game, rel)
    if rel.endswith(".dat"):
        return NefsArchive.open_headless(path, os.path.join(game, "dirtrally2.exe"))
    return NefsArchive.open_path(path)


def _files(arc: NefsArchive) -> list[tuple[str, int]]:
    items = set(arc._items)
    out = []
    for entry in arc.entries():
        if entry.is_file and entry.size and entry.id in items:
            out.append((entry.path, entry.size))
    return out


def _model_id(src: str, path: str) -> str:
    raw = (src + "_" + path).replace("/", "_").replace(".pssg", "").replace(".nefs", "")
    return safe_name(raw)


def _group(src: str, path: str) -> str:
    if src.startswith("cars/"):
        return "car_" + os.path.basename(src).replace(".nefs", "")
    if src.startswith("locations/"):
        return "loc_" + os.path.basename(src).replace(".nefs", "")[:48]
    parts = path.split("/")
    if len(parts) >= 3 and parts[0] == "characters":
        return "char_" + parts[2][:48]
    return "mdl_" + safe_name(os.path.basename(path).replace(".pssg", ""))[:48]


def surface_suffix(path: str) -> str:
    """`tm`/`gr`/`sn`/`wt` quando o PSSG é a textura de um piso; senão vazio."""
    leaf = os.path.basename(path).lower()
    for token in ("_gr_", "_sn_", "_tm_", "_wt_"):
        if token in leaf:
            return token[1:3]
    return ""


def _texture_paths(model_path: str, files: dict[str, int], hard: int) -> list[str]:
    parent = model_path.rsplit("/", 1)[0]
    base = os.path.basename(model_path)
    found = []
    for name, size in files.items():
        if size > hard:
            continue
        folder, leaf = name.rsplit("/", 1) if "/" in name else ("", name)
        if folder == parent and (leaf.endswith("_textures.pssg") or leaf in ("objectstextures.pssg", "treestextures.pssg")):
            found.append(name)
    if base.endswith("_highLOD.pssg"):
        prefix = parent + "/livery_00/textures_high/"
        for name, size in files.items():
            if size > hard or not name.startswith(prefix) or not name.endswith(".pssg"):
                continue
            if "_user" in os.path.basename(name):
                continue
            found.append(name)
    return sorted(set(found))


def _note(decision: str, kind: str, size: int, tokens: list[str] | None, meshes: int | None) -> str:
    if meshes == 0:
        return "nenhuma malha com posição e índice legíveis"
    if decision == "parse":
        return ""
    if size > HARD_CAP:
        return "acima de 120 MB (terreno ou textura de stream); fica só no índice"
    if tokens:
        return "fora deste --models; sem filtro, python -m tools.uiview gera a malha dos arquivos principais"
    if kind not in PRIMARY:
        return "LOD baixo ou interior; passe o nome do arquivo em --models para extrair (ex.: int_037.pssg)"
    return "acima de 40 MB; passe o nome em --models para extrair mesmo assim"


def _empty(src: str, path: str, kind: str, size: int, note: str) -> dict[str, Any]:
    return {
        "id": _model_id(src, path), "n": os.path.basename(path).replace(".pssg", ""), "k": kind,
        "src": src, "path": path, "bytes": size, "meshes": None, "mats": [], "verts": None,
        "tris": None, "tex": [], "geom": None, "car": None, "note": note,
    }


def _bind_textures(arc: NefsArchive, rel: str, path: str, files: dict[str, int], out: str, force: bool,
                   log: Callable[[str], None], assets: list[dict[str, Any]],
                   seen_tex: set[tuple[str, str]], seen_tex_ids: dict[tuple[str, str], list[str]]) -> list[str]:
    tex_ids: list[str] = []
    group = _group(rel, path)
    for tex_path in _texture_paths(path, files, HARD_CAP):
        key = (rel, tex_path)
        if key in seen_tex:
            tex_ids.extend(seen_tex_ids.get(key, []))
            continue
        log(f"  texturas: {tex_path}")
        try:
            blob = arc.read(tex_path)
            made = export_pssg_images(blob, group, out, force, log, surface_suffix(tex_path))
        except Exception as exc:
            log(f"  texturas não lidas {tex_path}: {exc}")
            seen_tex.add(key)
            seen_tex_ids[key] = []
            continue
        del blob
        ids = []
        have = {f"{a['g']}/{a['n']}" for a in assets}
        for asset in made:
            ident = f"{asset['g']}/{asset['n']}"
            if ident not in have:
                assets.append(asset)
                have.add(ident)
            ids.append(ident)
        seen_tex.add(key)
        seen_tex_ids[key] = ids
        tex_ids.extend(ids)
    return tex_ids


def _car_cameras(arc: NefsArchive, path: str) -> list[dict[str, Any]]:
    """Vistas de câmera do jogo (`cameras.xml` ao lado do PSSG), no espaço do carro.

    Cada vista traz os parâmetros crus (`params`) e, quando há, a posição (`pos`) e os ângulos.
    Câmeras de perseguição não têm posição fixa: guardam o alvo e a distância.
    """
    cam_path = os.path.dirname(path) + "/cameras.xml"
    try:
        root = bxml.decode(arc.read(cam_path))
    except Exception:
        return []
    views: list[dict[str, Any]] = []

    def num(v: str) -> float:
        try:
            return float(v)
        except ValueError:
            return 0.0

    def walk(node: list) -> None:
        if node[0] == "View":
            ident = dict(node[2]).get("ident", "")
            params: dict[str, Any] = {}
            for child in node[3]:
                a = dict(child[2])
                kind, name = a.get("type"), a.get("name", "")
                if kind == "vector3":
                    params[name] = [num(a.get("x", "0")), num(a.get("y", "0")), num(a.get("z", "0"))]
                elif kind == "scalar":
                    params[name] = num(a.get("value", "0"))
                elif kind == "bool":
                    params[name] = a.get("value") == "true"
                else:
                    params[name] = a.get("value", "")
            view: dict[str, Any] = {"id": ident, "locked": bool(params.get("isLocked")), "params": params}
            pos = params.get("position") or params.get("offset")
            if pos:
                view["pos"] = pos
                view["fixed"] = "position" in params          # câmeras de cabine: pitch em graus
                view["pitch"], view["yaw"], view["roll"] = (params.get(k, 0.0) for k in ("pitch", "yaw", "roll"))
            elif "target" in params:
                view["target"] = params["target"]
                view["dist"] = params.get("overrideOffset", 0.0)
            views.append(view)
        for child in node[3]:
            walk(child)

    walk(root)
    return views


def _export_car(arc: NefsArchive, rel: str, path: str, row: dict[str, Any], out: str, force: bool,
                log: Callable[[str], None]) -> str | None:
    """Grava a árvore (`.car.json`) e os buffers compartilhados (`.car.bin`) de um carro."""
    json_rel = f"models/{row['id']}.car.json"
    bin_rel = f"models/{row['id']}.car.bin"
    json_abs, bin_abs = os.path.join(out, json_rel), os.path.join(out, bin_rel)
    if not force and os.path.exists(json_abs) and os.path.exists(bin_abs):
        try:
            cached = json.load(open(json_abs, encoding="utf-8"))
            if cached.get("rev") == CAR_REV:
                if "cameras" not in cached:      # árvores antigas ganham só as câmeras, sem refazer a malha
                    cached["cameras"] = _car_cameras(arc, path)
                    with open(json_abs, "w", encoding="utf-8") as fh:
                        json.dump(cached, fh, separators=(",", ":"))
                return json_rel
        except (OSError, ValueError):
            pass
    try:
        pssg = PSSGFile(arc.read(path))
        if pssg.root is None:
            return None
        model, resources = build_car_model(pssg.root, row["id"], {"package": rel, "path": path, "bytes": row["bytes"]})
    except Exception as exc:
        log(f"  árvore do carro não lida {path}: {exc}")
        return None
    if not resources:
        return None
    os.makedirs(os.path.join(out, "models"), exist_ok=True)
    with open(bin_abs, "wb") as fh:
        fh.write(pack_resources(resources))
    model["bin"] = bin_rel
    model["cameras"] = _car_cameras(arc, path)
    with open(json_abs, "w", encoding="utf-8") as fh:
        json.dump(model, fh, separators=(",", ":"))
    lods = ", ".join(f"{l['name']}={l['nodes']} nós/{l['slices']} fatias" for l in model["lods"]) or "sem LOD"
    log(f"  árvore {row['n']}: {lods}; {len(resources)} buffers; {len(model['notes'])} notas")
    return json_rel


def _decide(kind: str, size: int, rel: str, path: str, tokens: list[str] | None, all_models: bool) -> str:
    return parse_decision(kind, size, rel, path, tokens, all_models=all_models)


def _default_jobs() -> int:
    """Processos de exportação: metade dos núcleos (cada um abre PSSG de centenas de MB)."""
    return max(1, min(8, (os.cpu_count() or 2) // 2))


def _export_package(game: str, rel: str, out: str, force: bool, tokens: list[str] | None,
                    all_models: bool) -> dict[str, Any]:
    """Exporta um pacote. Roda num processo próprio: devolve as linhas de log em vez de imprimir."""
    lines: list[str] = []
    log = lines.append
    models: list[dict[str, Any]] = []
    assets: list[dict[str, Any]] = []
    counts = {"parsed": 0, "reused": 0, "hard": 0}
    seen_tex: set[tuple[str, str]] = set()
    seen_tex_ids: dict[tuple[str, str], list[str]] = {}

    def done() -> dict[str, Any]:
        return {"log": lines, "models": models, "assets": assets, **counts}

    def remember_index(path: str, kind: str, size: int, decision: str) -> None:
        if size > HARD_CAP:
            counts["hard"] += 1
            log(f"  só índice, acima de 120 MB: {path} ({size / (1024 * 1024):.0f} MB)")
        models.append(_empty(rel, path, kind, size, _note(decision, kind, size, tokens, None)))

    try:
        arc = open_package(game, rel)
    except Exception as exc:
        log(f"modelo: não abriu {rel}: {exc}")
        return done()
    files = dict(_files(arc))
    chosen = [(path, size, classify_path(path)) for path, size in files.items()]
    chosen = [(p, s, k) for p, s, k in chosen if k]
    if not any(_decide(k, s, rel, p, tokens, all_models) == "parse" for p, s, k in chosen):
        for path, size, kind in chosen:
            remember_index(path, kind, size, _decide(kind, size, rel, path, tokens, all_models))
        return done()
    log(f"modelo: {rel}")
    for path, size, kind in chosen:
        decision = _decide(kind, size, rel, path, tokens, all_models)
        if decision != "parse":
            remember_index(path, kind, size, decision)
            continue
        row = _empty(rel, path, kind, size, _note(decision, kind, size, tokens, None))
        geom_rel = f"models/{row['id']}.bin"
        geom_abs = os.path.join(out, geom_rel)
        summary = None if force else summarize_geom(geom_abs)
        fresh = summary is None
        if summary is None:
            try:
                data = arc.read(path)
                pssg = PSSGFile(data)
                meshes = extract_meshes(pssg.root) if pssg.root is not None else []
                del pssg
            except Exception as exc:
                row["note"] = f"não lido: {exc}"
                models.append(row)
                log(f"  não lido {path}: {exc}")
                continue
            del data
            if not meshes:
                row["note"] = _note(decision, kind, size, tokens, 0)
                models.append(row)
                continue
            os.makedirs(os.path.join(out, "models"), exist_ok=True)
            with open(geom_abs, "wb") as fh:
                fh.write(pack_geom(meshes))
            summary = {
                "meshes": len(meshes),
                "mats": list(dict.fromkeys(m["material"] for m in meshes)),
                "verts": sum(len(m["positions"]) for m in meshes),
                "tris": sum(len(m["indices"]) // 3 for m in meshes),
            }
            del meshes
        else:
            counts["reused"] += 1
        tex_ids = _bind_textures(arc, rel, path, files, out, force, log, assets, seen_tex, seen_tex_ids)
        row.update({**summary, "tex": tex_ids, "geom": geom_rel, "note": ""})
        if kind in CAR_KINDS:
            row["car"] = _export_car(arc, rel, path, row, out, force, log)
        models.append(row)
        counts["parsed"] += 1
        origin = "nova" if fresh else "reaproveitada"
        log(f"  {row['n']}: {summary['meshes']} malhas, {summary['verts']} vértices, {len(tex_ids)} texturas ({origin})")
    del arc
    gc.collect()
    return done()


def export_models(game: str, out: str, force: bool, log: Callable[[str], None],
                  tokens: list[str] | None, all_models: bool = False,
                  jobs: int | None = None) -> tuple[list[dict[str, Any]], dict[str, Any]]:
    cache_path = os.path.join(out, "data", "model_cache.json")
    pkg_sig = []
    for rel in package_list(game):
        full = os.path.join(game, rel)
        st = os.stat(full)
        pkg_sig.append([rel, st.st_size, int(st.st_mtime)])
    signature = {"rev": MESH_REV, "car": CAR_REV, "tokens": [] if all_models else (tokens or []),
                 "all": bool(all_models), "pkgs": pkg_sig}
    if not force and os.path.exists(cache_path):
        try:
            cached = json.load(open(cache_path, encoding="utf-8"))
            if cached.get("sig") == signature and _cache_ok(out, cached):
                log(f"modelos: {len(cached['models'])} do cache ({cached['info'].get('parsed', 0)} com malha)")
                return cached["assets"], {"models": cached["models"], "info": cached["info"]}
        except (OSError, ValueError, KeyError):
            pass

    if all_models:
        log("modelos: todos os PSSG de até 120 MB (carro, personagem, local, prop, interior, LOD); acima disso, só o índice")
    models: list[dict[str, Any]] = []
    assets: list[dict[str, Any]] = []
    parsed = reused_n = hard_n = 0
    known: set[str] = set()

    rels = package_list(game)
    workers = max(1, min(jobs or _default_jobs(), len(rels)))
    args = [(game, rel, out, force, tokens, all_models) for rel in rels]
    if workers == 1:
        results = (_export_package(*a) for a in args)
        pool = None
    else:
        from concurrent.futures import ProcessPoolExecutor

        log(f"modelos: {workers} processos")
        pool = ProcessPoolExecutor(max_workers=workers)
        futures = [pool.submit(_export_package, *a) for a in args]
        results = (f.result() for f in futures)
    try:
        for result in results:
            for line in result["log"]:
                log(line)
            models.extend(result["models"])
            for asset in result["assets"]:
                ident = f"{asset['g']}/{asset['n']}"
                if ident not in known:
                    known.add(ident)
                    assets.append(asset)
            parsed += result["parsed"]
            reused_n += result["reused"]
            hard_n += result["hard"]
    finally:
        if pool is not None:
            pool.shutdown(wait=True, cancel_futures=True)

    info = {
        "partial": bool(tokens) and not all_models,
        "filter": "" if all_models else ", ".join(tokens or []),
        "all": bool(all_models),
        "parsed": parsed,
        "indexed": len(models),
        "reused": reused_n,
        "hard": hard_n,
    }
    os.makedirs(os.path.dirname(cache_path), exist_ok=True)
    json.dump({"sig": signature, "models": models, "assets": assets, "info": info},
              open(cache_path, "w", encoding="utf-8"))
    log(f"modelos: {len(models)} no índice, {parsed} com malha, {reused_n} reaproveitadas, {hard_n} acima de 120 MB")
    return assets, {"models": models, "info": info}


def _cache_ok(out: str, cached: dict[str, Any]) -> bool:
    for asset in cached.get("assets", []):
        if not os.path.exists(os.path.join(out, asset["p"])):
            return False
    for model in cached.get("models", []):
        if model.get("geom") and not os.path.exists(os.path.join(out, model["geom"])):
            return False
        if model.get("car") and not os.path.exists(os.path.join(out, model["car"])):
            return False
    return True
