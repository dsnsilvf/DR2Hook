"""Idioma do que os scripts mostram no viewer3d (@step/@done/@fail e rótulos): "pt" ou "en".

O viewer passa o dele em DR2_LANG. Sem DR2_LANG (script rodado à mão), vale o do sistema, como no viewer:
LANGUAGE (lista com ':'), LC_ALL, LC_MESSAGES e LANG; qualquer idioma que não seja português fica em inglês.
"""
from __future__ import annotations

import os


def detect(env=os.environ) -> str:
    v = env.get("DR2_LANG", "")
    if v in ("pt", "en"):
        return v
    cands = [c for c in env.get("LANGUAGE", "").split(":") if c]
    for name in ("LC_ALL", "LC_MESSAGES", "LANG"):
        x = env.get(name)
        if x and x not in ("C", "POSIX"):
            cands.append(x)
            break  # o primeiro definido manda, como no setlocale
    for c in cands:
        lang = c.replace("-", "_").split("_")[0].split(".")[0].split("@")[0].lower()
        if lang in ("pt", "en"):
            return lang
    return "en"


LANG = detect()


def t(pt: str, en: str) -> str:
    """O texto no idioma da vez."""
    return pt if LANG == "pt" else en
