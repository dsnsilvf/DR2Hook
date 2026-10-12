"""Lista dos carros do jogo para a aba Veículo do Testar no jogo (viewer3d): nome, classe, ano e pinturas.

Lê do game_1.dat o catálogo (`catalogues/base.ctpk`) e os textos em inglês e português
(`language/language_eng.lng`, `language_bra.lng`). Tabelas do catálogo usadas:

    0x7245e185  veículos: campo 3 = modelo, 5 = id do carro ("fr5"), 11 = classe
    0x24f948d5  modelos: campo 5 = chave do nome completo, 10 = ano
    0x2442a3ba  classes: campo 4 = id ("rally_r5"), 21 = chave do nome
    0xc16a87df  pinturas: campo 3 = veículo, 5 = "<carro>_<NN>" (ou "<carro>_user" nas de equipe)

Só entram as pinturas numeradas: o benchmark (AutoStage) monta "<carro>_<pintura>" e a pasta é
`livery_<NN>` no cars/<carro>.nefs. As de equipe ("fr5_user" + desenho em campo 37) usam o editor de
pintura do jogo e ficam de fora até alguém achar como o benchmark as aceita.

Grava JSON (padrão build/viewer3d/cars/catalog.json, ao lado dos modelos do editor):
    {"version": 1, "classes": [{"id", "pt", "en"}], "cars": [{"id", "pt", "en", "class", "year", "liveries"}]}
"""
from __future__ import annotations

import argparse
import json
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, ROOT)
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from ctpk import Ctpk, djb2  # noqa: E402
from tools.egodata import lng  # noqa: E402

T_VEHICLES = 0x7245E185
T_MODELS = 0x24F948D5
T_CLASSES = 0x2442A3BA
T_LIVERIES = 0xC16A87DF
OUT = os.path.join(ROOT, "build", "viewer3d", "cars", "catalog.json")
# ordem do menu de carros do jogo: históricos, modernos, depois rallycross; classe nova vai para o fim
CLASS_ORDER = ("rally_h1_fwd", "rally_h2_fwd", "rally_h2_rwd", "rally_h3_rwd", "rally_grp_b_rwd", "rally_grp_b_4wd",
               "rally_r2", "rally_kitcar", "rally_grp_a", "rally_nr4_r4", "rally_up_to_2000_4wd", "rally_r5",
               "rally_r_gt", "multi_crosskart", "rallyx_lites", "rallyx_1600s", "rallyx_group_b_rallycross",
               "rallyx_supercars", "rallyx_supercars_2019")


def fields(row) -> dict:
    return {f: v for f, _, v in row.fields}


def build(ctpk: Ctpk, eng: dict[str, str], bra: dict[str, str], on_disk: set[str] | None) -> dict:
    """Junta as quatro tabelas; `on_disk` (ids com cars/<id>.nefs) filtra o que o jogo não tem."""
    strings = {djb2(s): s.decode("utf-8", "replace") for s in ctpk.strings}

    def s(v) -> str:
        return strings.get(v, "") if isinstance(v, int) else ""

    def names(key: str) -> dict:
        en = eng.get(key) or bra.get(key) or key
        return {"pt": bra.get(key) or en, "en": en}

    models = {r.id: fields(r) for r in ctpk.tables[T_MODELS].rows}
    classes = {r.id: fields(r) for r in ctpk.tables[T_CLASSES].rows}
    liveries: dict[int, list[str]] = {}
    for r in ctpk.tables[T_LIVERIES].rows:
        f = fields(r)
        liveries.setdefault(f.get(3), []).append(s(f.get(5)))

    cars, used = [], set()
    for r in ctpk.tables[T_VEHICLES].rows:
        f = fields(r)
        cid = s(f.get(5))
        if not cid or (on_disk is not None and cid not in on_disk):
            continue
        m = models.get(f.get(3), {})
        cls = classes.get(f.get(11), {})
        nums = sorted({v[len(cid) + 1:] for v in liveries.get(r.id, [])
                       if v.startswith(cid + "_") and v[len(cid) + 1:].isdigit()})
        cars.append({"id": cid, **names(s(m.get(5))), "class": s(cls.get(4)), "year": m.get(10, 0),
                     "liveries": nums or ["00"]})
        used.add(f.get(11))
    def rank(cid: str) -> int:
        return CLASS_ORDER.index(cid) if cid in CLASS_ORDER else len(CLASS_ORDER)

    cls_list = [{"id": s(classes[k].get(4)), **names(s(classes[k].get(21)))} for k in used if k in classes]
    cls_list.sort(key=lambda c: (rank(c["id"]), c["id"]))
    order = [c["id"] for c in cls_list]
    cars.sort(key=lambda c: (order.index(c["class"]) if c["class"] in order else len(order), c["year"], c["en"].lower()))
    return {"version": 1, "classes": cls_list, "cars": cars}


def main() -> None:
    from tools.egodata.cli import DEFAULT_GAME, _open

    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--game", default=DEFAULT_GAME)
    ap.add_argument("--out", default=OUT)
    ap.add_argument("--all", action="store_true", help="mantém carros sem cars/<id>.nefs na pasta do jogo")
    a = ap.parse_args()

    pack = _open(a.game, "game_1.dat")
    ctpk = Ctpk(pack.read("catalogues/base.ctpk"))
    eng = lng.decode(pack.read("language/language_eng.lng"))
    bra = lng.decode(pack.read("language/language_bra.lng"))
    on_disk = None
    if not a.all:
        on_disk = {n[:-5] for n in os.listdir(os.path.join(a.game, "cars")) if n.endswith(".nefs")}
    cat = build(ctpk, eng, bra, on_disk)
    os.makedirs(os.path.dirname(os.path.abspath(a.out)), exist_ok=True)
    tmp = a.out + ".tmp"
    with open(tmp, "w", encoding="utf-8") as fh:
        json.dump(cat, fh, ensure_ascii=False, indent=1)
    os.replace(tmp, a.out)
    print(f"{len(cat['cars'])} carros, {len(cat['classes'])} classes → {a.out}")


if __name__ == "__main__":
    main()
