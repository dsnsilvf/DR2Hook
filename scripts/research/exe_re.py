"""Ajuda de análise estática do `dirtrally2.exe` (capstone + pefile).

Comandos:
  func <rva|va> [...]     disassembla a função (limites pelo .pdata), com chamadas e strings anotadas
  calls <rva|va>          só as chamadas da função, em ordem
  xrefs <rva|va>          quem chama/salta para o endereço (varredura de rel32 no .text)
  strref <texto>          strings do .rdata que contêm o texto e as funções que as usam
  data <rva|va>           funções que referenciam o endereço por RIP-relativo (globais/vtables)
  stack <e..|e..|...>     pilha da LoadProbe: função de cada quadro e a chamada que ele fez

Endereços aceitam RVA (`8f3901`, `e8f3901` como no trace da LoadProbe) ou VA (`0x1408f3901`).
Nada aqui escreve no jogo; a saída vai para o terminal (não versionar dumps).
Precisa de `capstone` e `pefile` (ex.: um venv fora do repositório).
"""

from __future__ import annotations

import bisect
import os
import struct
import sys
from functools import lru_cache

import capstone
import numpy as np
import pefile

EXE = os.environ.get("DR2_EXE", "/mnt/Jogos/SteamLibrary/steamapps/common/DiRT Rally 2.0/dirtrally2.exe")
BASE = 0x140000000


class Image:
    def __init__(self, path: str):
        self.pe = pefile.PE(path, fast_load=True)
        self.pe.parse_data_directories(directories=[pefile.DIRECTORY_ENTRY["IMAGE_DIRECTORY_ENTRY_EXCEPTION"]])
        self.data = self.pe.get_memory_mapped_image()
        self.sections = {s.Name.rstrip(b"\0").decode(): s for s in self.pe.sections}
        entries = sorted(
            (e.struct.BeginAddress, e.struct.EndAddress, e.struct.UnwindData)
            for e in getattr(self.pe, "DIRECTORY_ENTRY_EXCEPTION", [])
        )
        # Blocos com UNW_FLAG_CHAININFO pertencem à função do registro encadeado; juntamos os
        # blocos contíguos da mesma função para disassemblar o corpo inteiro.
        primary: dict[int, int] = {}
        for begin, _end, unwind in entries:
            root, info = begin, unwind & ~1
            for _ in range(8):
                flags = self.data[info] >> 3
                if not flags & 4:
                    break
                count = self.data[info + 2]
                root, _e, info = struct.unpack_from("<3I", self.data, info + 4 + ((count + 1) & ~1) * 2)
                info &= ~1
            primary[begin] = root
        self.funcs = [(b, e) for b, e, _u in entries]
        self.starts = [b for b, _e in self.funcs]
        self.primary = primary
        self.extent: dict[int, int] = {}
        for b, e, _u in entries:  # só blocos encadeados logo em seguida (blocos frios distantes ficam de fora)
            r = primary[b]
            if r == b:
                self.extent[r] = e
            elif self.extent.get(r, -64) + 64 >= b:
                self.extent[r] = max(self.extent[r], e)
        self.md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
        self.md.detail = True

    def func_of(self, rva: int) -> tuple[int, int] | None:
        """Função (início primário, fim do último bloco encadeado) que contém o endereço."""
        i = bisect.bisect_right(self.starts, rva) - 1
        if i < 0:
            return None
        begin, end = self.funcs[i]
        if not begin <= rva < end:
            return None
        root = self.primary.get(begin, begin)
        return root, self.extent.get(root, end)

    def section_of(self, rva: int) -> str:
        for name, s in self.sections.items():
            if s.VirtualAddress <= rva < s.VirtualAddress + max(s.Misc_VirtualSize, s.SizeOfRawData):
                return name
        return "?"

    def cstring(self, rva: int, limit: int = 120) -> str | None:
        if self.section_of(rva) not in (".rdata", ".data"):
            return None
        raw = self.data[rva : rva + limit]
        end = raw.find(b"\0")
        if end < 3:
            return None
        s = raw[:end]
        if all(32 <= c < 127 for c in s):
            return s.decode()
        return None

    def disasm(self, begin: int, end: int):
        return self.md.disasm(self.data[begin:end], BASE + begin)


def parse_addr(text: str) -> int:
    t = text.lower().removeprefix("0x")
    if t.startswith("e") and len(t) == 7:  # formato do trace: e + rva de 6 dígitos
        t = t[1:]
    v = int(t, 16)
    return v - BASE if v >= BASE else v


@lru_cache(maxsize=1)
def image() -> Image:
    return Image(EXE)


def rip_target(insn) -> int | None:
    for op in insn.operands:
        if op.type == capstone.x86.X86_OP_MEM and op.mem.base == capstone.x86.X86_REG_RIP:
            return insn.address + insn.size + op.mem.disp - BASE
    return None


def branch_target(insn) -> int | None:
    if insn.group(capstone.CS_GRP_CALL) or insn.group(capstone.CS_GRP_JUMP):
        op = insn.operands[0] if insn.operands else None
        if op is not None and op.type == capstone.x86.X86_OP_IMM:
            return op.imm - BASE
    return None


def annotate(img: Image, insn) -> str:
    notes = []
    tgt = branch_target(insn)
    if tgt is not None and insn.group(capstone.CS_GRP_CALL):
        notes.append(f"-> {tgt:x}")
    ref = rip_target(insn)
    if ref is not None:
        s = img.cstring(ref)
        if s is not None:
            notes.append(repr(s))
        else:
            sec = img.section_of(ref)
            notes.append(f"[{sec} {ref:x}]")
            if sec == ".rdata" and insn.mnemonic == "lea":
                ptr = struct.unpack_from("<Q", img.data, ref)[0]
                if BASE <= ptr < BASE + len(img.data):
                    notes.append(f"*={ptr - BASE:x}")
    return ("   ; " + " ".join(notes)) if notes else ""


def cmd_func(args: list[str]) -> None:
    img = image()
    for a in args:
        rva = parse_addr(a)
        f = img.func_of(rva)
        if f is None:
            print(f"{rva:x}: fora de função conhecida")
            continue
        begin, end = f
        print(f"== função {begin:x}..{end:x} ({end - begin} bytes), pedido {rva:x}")
        for insn in img.disasm(begin, end):
            mark = ">>" if insn.address - BASE <= rva < insn.address - BASE + insn.size else "  "
            print(f"{mark}{insn.address - BASE:7x}  {insn.mnemonic:6} {insn.op_str}{annotate(img, insn)}")


def cmd_calls(args: list[str]) -> None:
    img = image()
    for a in args:
        f = img.func_of(parse_addr(a))
        if f is None:
            continue
        print(f"== {f[0]:x}")
        for insn in img.disasm(*f):
            if insn.group(capstone.CS_GRP_CALL):
                print(f"  {insn.address - BASE:7x}  call {insn.op_str}{annotate(img, insn)}")
            else:
                ref = rip_target(insn)
                if ref is not None and img.cstring(ref):
                    print(f"  {insn.address - BASE:7x}  {insn.mnemonic} {annotate(img, insn).strip()}")


def text_range(img: Image) -> tuple[int, int]:
    s = img.sections[".text"]
    return s.VirtualAddress, s.VirtualAddress + s.Misc_VirtualSize


@lru_cache(maxsize=1)
def rel_index():
    """Para cada byte do .text, o destino de um rel32/disp32 que começasse ali (vetorizado).

    Devolve (destinos ordenados, posições correspondentes). Serve tanto para call/jmp rel32
    (posição = opcode + 1) quanto para [rip+disp32] (destino = fim do disp + tail).
    """
    img = image()
    lo, hi = text_range(img)
    raw = np.frombuffer(bytes(img.data[lo:hi + 3]), dtype=np.uint8)
    n = hi - lo
    disp = (raw[0:n].astype(np.int64) | (raw[1:n + 1].astype(np.int64) << 8) | (raw[2:n + 2].astype(np.int64) << 16)
            | (raw[3:n + 3].astype(np.int64) << 24))
    disp = np.where(disp >= 1 << 31, disp - (1 << 32), disp)
    pos = np.arange(lo, hi, dtype=np.int64)
    dest = pos + 4 + disp
    order = np.argsort(dest, kind="stable")
    return dest[order], pos[order]


def disp_sites(target: int, tails=(0,)):
    dest, pos = rel_index()
    for tail in tails:
        a = np.searchsorted(dest, target - tail, "left")
        b = np.searchsorted(dest, target - tail, "right")
        for p in pos[a:b]:
            yield int(p), tail


def scan_rel32(img: Image, target: int, opcodes=(0xE8, 0xE9)):
    for p, _ in disp_sites(target):
        if img.data[p - 1] in opcodes:
            yield p - 1


def cmd_xrefs(args: list[str]) -> None:
    img = image()
    for a in args:
        target = parse_addr(a)
        print(f"== xrefs de {target:x}")
        for site in scan_rel32(img, target):
            f = img.func_of(site)
            kind = "call" if img.data[site] == 0xE8 else "jmp"
            print(f"  {site:7x} {kind}  (função {f[0]:x})" if f else f"  {site:7x} {kind}")


def scan_riprefs(img: Image, target: int):
    """Instruções com [rip+disp32] apontando para target."""
    seen = set()
    for p, tail in disp_sites(target, tails=(0, 1, 2, 4)):
        for start in range(p - 1, p - 11, -1):
            insn = next(img.md.disasm(img.data[start:start + 16], BASE + start, 1), None)
            if insn is None or start + insn.size <= p:
                continue
            if rip_target(insn) == target and insn.address not in seen and img.func_of(start):
                seen.add(insn.address)
                yield insn
                break


def cmd_data(args: list[str]) -> None:
    img = image()
    for a in args:
        target = parse_addr(a)
        print(f"== referências a {target:x}")
        seen = set()
        for insn in scan_riprefs(img, target):
            if insn.address in seen:
                continue
            seen.add(insn.address)
            f = img.func_of(insn.address - BASE)
            print(f"  {insn.address - BASE:7x}  {insn.mnemonic} {insn.op_str}   (função {f[0]:x})")


def cmd_strref(args: list[str]) -> None:
    img = image()
    needle = " ".join(args).encode()
    rdata = img.sections[".rdata"]
    lo = rdata.VirtualAddress
    blob = img.data[lo : lo + rdata.Misc_VirtualSize]
    pos = blob.find(needle)
    count = 0
    while pos != -1 and count < 40:
        start = blob.rfind(b"\0", 0, pos) + 1
        rva = lo + start
        s = img.cstring(rva, 200)
        if s:
            refs = sorted({img.func_of(insn.address - BASE)[0] for insn in scan_riprefs(img, rva)})
            print(f"{rva:7x} {s!r}  usado em: {' '.join(f'{r:x}' for r in refs) or '-'}")
            count += 1
        pos = blob.find(needle, pos + 1)


def call_before(img: Image, ret: int):
    """A instrução de chamada que termina em `ret` (endereço de retorno da pilha)."""
    for size in (5, 6, 2, 3, 7):
        insn = next(img.md.disasm(img.data[ret - size:ret], BASE + ret - size, 1), None)
        if insn is not None and insn.size == size and insn.group(capstone.CS_GRP_CALL):
            return insn
    return None


def cmd_stack(args: list[str]) -> None:
    """Pilha da LoadProbe (`e8f3901|e8ee095|...`): função de cada quadro e a chamada feita."""
    img = image()
    frames = [f for a in args for f in a.split("|") if f.startswith("e")]
    for fr in frames:
        ret = parse_addr(fr)
        f = img.func_of(ret)
        insn = call_before(img, ret)
        what = f"{insn.mnemonic} {insn.op_str}{annotate(img, insn)}" if insn else "?"
        print(f"  ret {ret:7x}  em {f[0] if f else 0:7x}  {what}")


def main() -> None:
    if len(sys.argv) < 3:
        print(__doc__)
        return
    cmd, args = sys.argv[1], sys.argv[2:]
    {"stack": cmd_stack, "func": cmd_func, "calls": cmd_calls, "xrefs": cmd_xrefs, "strref": cmd_strref, "data": cmd_data}[cmd](args)


if __name__ == "__main__":
    main()
