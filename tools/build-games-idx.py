#!/usr/bin/env python3
"""Fusiona el DAT Retroplay (XML), el DAT libretro (clrmamepro) y el listado del
repo libretro-thumbnails en games.idx (v2).
Uso: build-games-idx.py <retroplay.dat> <libretro.dat> <thumbs-tree.txt> <salida.idx>
Registro (32 B, little-endian), ordenado por crc32:
  u32 crc | u32 title_off | u32 file_off | u16 flags | char lang[2] | char ver[8]
  | u32 box_off | u32 snap_off      (offsets a la tabla de strings; 0xFFFFFFFF = no hay)
Cabecera (20 B): "AGDB" | u32 version=2 | u32 count | u32 rec_off | u32 str_off
box_off/snap_off apuntan al nombre EXACTO (sin .png) en Named_Boxarts/Named_Snaps."""
import re, struct, sys

F_AGA, F_CD32, F_NTSC, F_CDTV, F_LIBRETRO, F_BETA = 1, 2, 4, 8, 16, 32
LANGS = {"De", "Fr", "It", "Es", "Pl", "Dk", "Cz", "Se", "Gr", "Nl", "Fi", "Pt", "Hu", "No"}
NONE = 0xFFFFFFFF
# Etiquetas de plataforma/variante: una imagen con alguna que el titulo NO tiene es peor opcion
PLATFORM = {"cd32", "cdtv", "aga", "ocs", "demo", "beta", "proto", "unl", "sample", "chip"}

def parse_file(name):
    toks = name.rsplit(".", 1)[0].split("_")
    flags, lang, ver = 0, "", ""
    for t in toks[1:]:
        if t == "AGA": flags |= F_AGA
        elif t == "CD32": flags |= F_CD32
        elif t == "NTSC": flags |= F_NTSC
        elif t == "CDTV": flags |= F_CDTV
        elif t == "BETA": flags |= F_BETA
        elif t in LANGS and not lang: lang = t
        elif re.fullmatch(r"v\d.*", t) and not ver: ver = t[1:]
    return toks[0], flags, lang, ver

def humanize(camel):
    s = re.sub(r"(?<=[a-z0-9])(?=[A-Z])", " ", camel)
    return re.sub(r"(?<=[A-Z])(?=[A-Z][a-z])", " ", s)

def san(n): return re.sub(r'[&*/:`<>?\\|]', '_', n)
def base(n): return re.sub(r"\s*\(.*$", "", n).strip()
def norm(s): return re.sub(r"[^a-z0-9]", "", s.lower())
def tags(n): return {t.lower() for t in re.findall(r"\(([^)]*)\)", n)}

def load_tree(path):
    kinds = {"Named_Boxarts": set(), "Named_Snaps": set()}
    for line in open(path, encoding="utf-8"):
        line = line.rstrip("\n")
        if "/" not in line: continue
        d, f = line.split("/", 1)
        if d in kinds and f.endswith(".png"): kinds[d].add(f[:-4])
    return kinds

class Resolver:
    def __init__(self, names):
        self.names = names
        self.by_base = {}
        for n in sorted(names): self.by_base.setdefault(norm(base(n)), []).append(n)
    def resolve(self, title):
        s = san(title)
        if s in self.names: return s
        c = self.by_base.get(norm(base(title)))
        if not c: return None
        t = tags(title)
        return min(c, key=lambda x: (len((tags(x) - t) & PLATFORM), -len(t & tags(x)), len(x), x))

def main(rp_path, lib_path, tree_path, out_path):
    rp = open(rp_path, encoding="utf-8").read().replace("&amp;", "&")
    entries = {}
    for n, c in re.findall(r'<rom name="([^"]*)" size="\d+" crc="([0-9a-fA-F]+)"', rp):
        entries[int(c, 16)] = [n, None]
    lib = open(lib_path, encoding="utf-8", errors="replace").read()
    for g, n, c in re.findall(r'game \(\s*name "([^"]*)"\s*rom \( name "([^"]*)" size \d+ crc ([0-9A-Fa-f]+)', lib):
        crc = int(c, 16)
        title = " ".join(g.split())
        if crc in entries: entries[crc][1] = title
        else: entries[crc] = [n, title]
    kinds = load_tree(tree_path)
    rbox, rsnap = Resolver(kinds["Named_Boxarts"]), Resolver(kinds["Named_Snaps"])
    strtab, offs = bytearray(), {}
    def sadd(s):
        if s not in offs:
            offs[s] = len(strtab); strtab.extend(s.encode("utf-8") + b"\0")
        return offs[s]
    recs, n_box, n_snap, n_any = [], 0, 0, 0
    for crc in sorted(entries):
        fname, title = entries[crc]
        stem, flags, lang, ver = parse_file(fname)
        if title: flags |= F_LIBRETRO
        else: title = humanize(stem)
        b, s = rbox.resolve(title), rsnap.resolve(title)
        n_box += b is not None; n_snap += s is not None; n_any += (b or s) is not None
        recs.append(struct.pack("<IIIH2s8sII", crc, sadd(title), sadd(fname), flags,
                                lang.encode().ljust(2, b"\0"), ver.encode()[:7].ljust(8, b"\0"),
                                sadd(b) if b else NONE, sadd(s) if s else NONE))
    rec_off = 20
    str_off = rec_off + 32 * len(recs)
    with open(out_path, "wb") as f:
        f.write(b"AGDB" + struct.pack("<IIII", 2, len(recs), rec_off, str_off))
        f.write(b"".join(recs)); f.write(strtab)
    n = len(recs)
    print(f"{n} entradas -> {out_path} ({str_off+len(strtab)} B)")
    print(f"con boxart: {n_box} ({100*n_box//n}%)  con snap: {n_snap} ({100*n_snap//n}%)  con alguna: {n_any} ({100*n_any//n}%)")

if __name__ == "__main__":
    if len(sys.argv) != 5: sys.exit(__doc__)
    main(*sys.argv[1:])
