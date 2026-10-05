#!/usr/bin/env python3
"""Dump das regioes graváveis do dirtrally2.exe (via /proc/<pid>/mem) mais a
imagem do executável, para análise offline. Uso:
    python3 scripts/research/dump_process.py captures/dumps/<nome>
Gera regions.tsv (inicio, fim, perms, arquivo) e um .bin por região."""
import os, re, subprocess, sys, time

out = sys.argv[1]
pid = int(subprocess.check_output(["pgrep", "-x", "dirtrally2.exe"]).split()[0])
os.makedirs(out, exist_ok=True)
t0 = time.time()
total = 0
with open(f"/proc/{pid}/maps") as maps, open(f"/proc/{pid}/mem", "rb", 0) as mem, \
        open(os.path.join(out, "regions.tsv"), "w") as index:
    for line in maps:
        m = re.match(r"([0-9a-f]+)-([0-9a-f]+) (\S+) \S+ \S+ \S+\s*(.*)", line)
        start, end, perms, path = int(m[1], 16), int(m[2], 16), m[3], m[4]
        image = 0x140000000 <= start < 0x143000000
        if not image and ("w" not in perms or "r" not in perms):
            continue
        if path.startswith("/dev/") or "[vvar" in path:
            continue
        name = f"{start:016x}-{end:016x}.bin"
        size = 0
        with open(os.path.join(out, name), "wb") as f:
            addr = start
            while addr < end:
                n = min(16 << 20, end - addr)
                try:
                    mem.seek(addr)
                    f.write(mem.read(n))
                    size += n
                except OSError:
                    pass
                addr += n
        if size == 0:
            os.remove(os.path.join(out, name))
            continue
        total += size
        index.write(f"{start:#x}\t{end:#x}\t{perms}\t{path}\n")
print(f"pid {pid}: {total / 2**20:.0f} MB em {time.time() - t0:.1f} s -> {out}")
