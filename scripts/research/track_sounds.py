#!/usr/bin/env python3
"""Sons de uma pista para ouvir no editor: os bancos que o `audio_dataset.xml` dela carrega, tirados do jogo.

Os bancos (`audio/soundbanks/*.bnk`) moram no `game/game.nefs`. Um `.bnk` do Wwise é uma lista de pedaços
(`BKHD`, `DIDX`, `DATA`, `HIRC`...): o `DIDX` tem 12 bytes por som embutido (id, deslocamento e tamanho dentro do
`DATA`), e cada som é um WEM, um RIFF com o `fmt ` de sempre. O codec diz o que dá para tocar:

- `0xFFFE` (PCM extensível) ou `0x0001`: PCM, copiado para um `.wav` comum.
- `0x0002`: IMA ADPCM do Wwise. Cada bloco de `block_align` bytes tem primeiro um cabeçalho de 4 bytes por canal
  (amostra inicial i16, índice do passo u8, 1 byte livre) e depois os nibbles de cada canal em sequência
  ((block_align − 4·canais)/canais bytes cada), o nibble baixo primeiro. Vira `.wav` PCM de 16 bits.
- `0xFFFF`: Vorbis do Wwise (precisa do vgmstream): fica de fora, só contado.

Os bancos `d_sfc_*` não têm som embutido (só eventos). Sai `<out>/index.json` (bancos, superfície, sons com id,
arquivo, codec, canais, taxa e duração) e um `.wav` por som em `<out>/<banco>/<id>.wav`. O editor (janela Sons) lê
o índice e toca os `.wav`.

    python3 scripts/research/track_sounds.py [--track-dir <pasta>] [--out build/re/track_sounds]
"""
from __future__ import annotations

import argparse
import json
import os
import re
import struct
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", ".."))
sys.path.insert(0, os.path.dirname(__file__))

from tools.egodata.nefs import NefsArchive  # noqa: E402
from ui_lang import t  # noqa: E402

GAME = os.environ.get("DR2_GAME_DIR", "/mnt/Jogos/SteamLibrary/steamapps/common/DiRT Rally 2.0")
TRACK_DIR = "captures/overlay/tracks/locations/portugal/dr2hook_ring"
OUT = "build/re/track_sounds"

# nome do banco sem s_sfc_/d_sfc_ -> superfície, para o rótulo do editor
SURFACES = {
    "grass": "grama", "gravel": "cascalho", "dirt": "terra", "ridge": "ressaltos", "tarmac": "asfalto",
    "rumblestrip": "zebra", "wood_smooth": "madeira lisa", "wet_generic": "chão molhado", "water": "água",
    "snow": "neve", "ice": "gelo", "concrete": "concreto", "cobbles": "paralelepípedo", "mud": "lama",
}

# o editor mostra "surface_en" quando está em inglês
SURFACES_EN = {
    "grass": "grass", "gravel": "gravel", "dirt": "dirt", "ridge": "ridges", "tarmac": "tarmac",
    "rumblestrip": "rumble strip", "wood_smooth": "smooth wood", "wet_generic": "wet ground", "water": "water",
    "snow": "snow", "ice": "ice", "concrete": "concrete", "cobbles": "cobbles", "mud": "mud",
}

IMA_STEPS = [
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97, 107,
    118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963,
    1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894,
    6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794,
    32767,
]
IMA_INDEX = [-1, -1, -1, -1, 2, 4, 6, 8]


def chunks(data: bytes, start: int = 0, end: int | None = None):
    """(etiqueta, início do conteúdo, tamanho) de cada pedaço RIFF/BNK."""
    pos, end = start, len(data) if end is None else end
    while pos + 8 <= end:
        tag, size = data[pos:pos + 4], struct.unpack_from("<I", data, pos + 4)[0]
        yield tag, pos + 8, size
        pos += 8 + size + (size & 1 if tag not in (b"DIDX", b"DATA", b"BKHD", b"HIRC") else 0)


def embedded(bank: bytes) -> list[tuple[int, bytes]]:
    """(id, WEM) de cada som embutido no banco."""
    didx = data = None
    for tag, off, size in chunks(bank):
        if tag == b"DIDX":
            didx = (off, size)
        elif tag == b"DATA":
            data = off
    if not didx or data is None:
        return []
    out = []
    for k in range(didx[1] // 12):
        wid, o, n = struct.unpack_from("<III", bank, didx[0] + k * 12)
        out.append((wid, bank[data + o:data + o + n]))
    return out


def wem_info(wem: bytes) -> tuple[dict, bytes] | None:
    """Campos do `fmt ` e o conteúdo do `data` de um WEM."""
    if wem[:4] != b"RIFF" or wem[8:12] != b"WAVE":
        return None
    fmt = pcm = None
    for tag, off, size in chunks(wem, 12):
        if tag == b"fmt ":
            codec, ch, rate, _bps, align, bits = struct.unpack_from("<HHIIHH", wem, off)
            fmt = {"codec": codec, "channels": ch, "rate": rate, "align": align, "bits": bits}
        elif tag == b"data":
            pcm = wem[off:off + size]
    return (fmt, pcm) if fmt and pcm is not None else None


def decode_ima(data: bytes, channels: int, align: int) -> tuple[bytes, float]:
    """IMA ADPCM do Wwise para PCM 16 bits intercalado. Devolve também o salto médio entre o fim de um bloco e a
    amostra inicial do seguinte, em relação à amplitude média (perto de 0 = blocos encadeados, layout certo)."""
    per = (align - 4 * channels) // channels
    out_ch: list[list[int]] = [[] for _ in range(channels)]
    jumps, amp = [], []
    for b in range(len(data) // align):
        blk = data[b * align:(b + 1) * align]
        for c in range(channels):
            hist, idx = struct.unpack_from("<hB", blk, 4 * c)
            idx = min(idx, 88)
            samples = out_ch[c]
            if samples:
                jumps.append(abs(hist - samples[-1]))
            samples.append(hist)
            base = 4 * channels + per * c
            for byte in blk[base:base + per]:
                for nib in (byte & 15, byte >> 4):
                    step = IMA_STEPS[idx]
                    diff = step >> 3
                    if nib & 1:
                        diff += step >> 2
                    if nib & 2:
                        diff += step >> 1
                    if nib & 4:
                        diff += step
                    hist = max(-32768, min(32767, hist - diff if nib & 8 else hist + diff))
                    idx = max(0, min(88, idx + IMA_INDEX[nib & 7]))
                    samples.append(hist)
                    amp.append(abs(hist))
    n = min(len(s) for s in out_ch) if out_ch else 0
    inter = [out_ch[c][i] for i in range(n) for c in range(channels)]
    mean_amp = sum(amp) / len(amp) if amp else 0.0
    score = (sum(jumps) / len(jumps)) / mean_amp if jumps and mean_amp > 0 else 0.0
    return struct.pack(f"<{len(inter)}h", *inter), score


def wav(pcm: bytes, channels: int, rate: int, bits: int) -> bytes:
    align = channels * bits // 8
    return (b"RIFF" + struct.pack("<I", 36 + len(pcm)) + b"WAVE" + b"fmt " +
            struct.pack("<IHHIIHH", 16, 1, channels, rate, rate * align, align, bits) +
            b"data" + struct.pack("<I", len(pcm)) + pcm)


def track_banks(track_dir: str) -> list[str]:
    text = open(os.path.join(track_dir, "audio_dataset.xml"), encoding="utf-8-sig").read()
    return re.findall(r'filename="(audio/soundbanks/[^"]+\.bnk)"', text)


def surface(bank: str, names: dict[str, str] = SURFACES) -> str:
    name = os.path.basename(bank)[:-4]
    key = re.sub(r"^[sd]_sfc_", "", name)
    return names.get(key, key.replace("_", " "))


def extract(game: str, track_dir: str, out: str) -> dict:
    arc = NefsArchive.open_path(os.path.join(game, "game", "game.nefs"))
    index = {"source": "game/game.nefs", "track_dir": track_dir, "banks": []}
    scores = []
    for path in track_banks(track_dir):
        name = os.path.basename(path)[:-4]
        entry = {"bank": name, "surface": surface(path), "surface_en": surface(path, SURFACES_EN), "sounds": [], "vorbis": 0, "unreadable": 0}
        try:
            bank = arc.read(path)
        except KeyError:
            entry["missing"] = True
            index["banks"].append(entry)
            continue
        folder = os.path.join(out, name)
        for wid, wem in embedded(bank):
            info = wem_info(wem)
            if not info:
                entry["unreadable"] += 1
                continue
            fmt, data = info
            if fmt["codec"] in (0x0001, 0xFFFE) and fmt["bits"] in (8, 16, 24, 32):
                pcm, bits, codec = data, fmt["bits"], "pcm"
            elif fmt["codec"] == 0x0002 and fmt["align"] > 4 * fmt["channels"]:
                pcm, score = decode_ima(data, fmt["channels"], fmt["align"])
                bits, codec = 16, "adpcm"
                scores.append(score)
            elif fmt["codec"] == 0xFFFF:
                entry["vorbis"] += 1
                continue
            else:
                entry["unreadable"] += 1
                continue
            os.makedirs(folder, exist_ok=True)
            rel = f"{name}/{wid}.wav"
            with open(os.path.join(out, rel), "wb") as fh:
                fh.write(wav(pcm, fmt["channels"], fmt["rate"], bits))
            frames = len(pcm) // (fmt["channels"] * bits // 8)
            entry["sounds"].append({"id": wid, "file": rel, "codec": codec, "channels": fmt["channels"],
                                    "rate": fmt["rate"], "seconds": round(frames / fmt["rate"], 3)})
        index["banks"].append(entry)
    if scores:
        index["adpcm_block_jump"] = round(sum(scores) / len(scores), 3)
    os.makedirs(out, exist_ok=True)
    with open(os.path.join(out, "index.json"), "w") as fh:
        json.dump(index, fh, ensure_ascii=False, indent=1)
    return index


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--track-dir", default=TRACK_DIR)
    ap.add_argument("--out", default=OUT)
    ap.add_argument("--game", default=GAME)
    a = ap.parse_args()
    try:
        index = extract(a.game, a.track_dir, a.out)
    except (OSError, ValueError) as e:
        print(f"@fail {e}")
        sys.exit(1)
    total = 0
    for b in index["banks"]:
        n = len(b["sounds"])
        total += n
        extra = (f", {b['vorbis']} Vorbis de fora" if b["vorbis"] else "") + (" (não está no jogo)" if b.get("missing") else "")
        if n or extra:
            print(f"{b['bank']:24s} {b['surface']:14s} {n:4d} sons{extra}")
    print(f"ADPCM: salto médio entre blocos {index['adpcm_block_jump']} da amplitude" if "adpcm_block_jump" in index else "sem ADPCM")
    print("@done " + t(f"{total} sons em {a.out}", f"{total} sounds in {a.out}"))


if __name__ == "__main__":
    main()
