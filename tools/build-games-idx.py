#!/usr/bin/env python3
"""Fusiona el DAT Retroplay (XML) y el DAT libretro (clrmamepro) en games.idx.
Uso: build-games-idx.py <retroplay.dat> <libretro.dat> <salida.idx>
Registro (24 B, little-endian), ordenado por crc32:
  u32 crc | u32 title_off | u32 file_off | u16 flags | char lang[2] | char ver[8]
Cabecera (20 B): "AGDB" | u32 version=1 | u32 count | u32 rec_off | u32 str_off"""
import re, struct, sys

F_AGA, F_CD32, F_NTSC, F_CDTV, F_LIBRETRO, F_BETA = 1, 2, 4, 8, 16, 32
LANGS = {"De", "Fr", "It", "Es", "Pl", "Dk", "Cz", "Se", "Gr", "Nl", "Fi", "Pt", "Hu", "No"}

def parse_file(name):
    base = name.rsplit(".", 1)[0]
    toks = base.split("_")
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

def main(rp_path, lib_path, out_path):
    rp = open(rp_path, encoding="utf-8").read().replace("&amp;", "&")
    entries = {}  # crc -> [filename, title|None]
    for n, c in re.findall(r'<rom name="([^"]*)" size="\d+" crc="([0-9a-fA-F]+)"', rp):
        entries[int(c, 16)] = [n, None]
    lib = open(lib_path, encoding="utf-8", errors="replace").read()
    for g, n, c in re.findall(r'game \(\s*name "([^"]*)"\s*rom \( name "([^"]*)" size \d+ crc ([0-9A-Fa-f]+)', lib):
        crc = int(c, 16)
        title = " ".join(g.split())  # normaliza TAB/espacios dobles
        if crc in entries: entries[crc][1] = title
        else: entries[crc] = [n, title]
    strtab, offs = bytearray(), {}
    def sadd(s):
        if s not in offs:
            offs[s] = len(strtab); strtab.extend(s.encode("utf-8") + b"\0")
        return offs[s]
    recs = []
    for crc in sorted(entries):
        fname, title = entries[crc]
        stem, flags, lang, ver = parse_file(fname)
        if title: flags |= F_LIBRETRO
        else: title = humanize(stem)
        recs.append(struct.pack("<IIIH2s8s", crc, sadd(title), sadd(fname), flags,
                                lang.encode().ljust(2, b"\0"), ver.encode()[:7].ljust(8, b"\0")))
    rec_off = 20
    str_off = rec_off + 24 * len(recs)
    with open(out_path, "wb") as f:
        f.write(b"AGDB" + struct.pack("<IIII", 1, len(recs), rec_off, str_off))
        f.write(b"".join(recs)); f.write(strtab)
    n_lib = sum(1 for r in recs if struct.unpack_from("<IIIH", r)[3] & F_LIBRETRO)
    print(f"{len(recs)} entradas ({n_lib} con nombre libretro, {len(recs)-n_lib} derivadas) -> {out_path} ({str_off+len(strtab)} B)")

if __name__ == "__main__":
    if len(sys.argv) != 4: sys.exit(__doc__)
    main(*sys.argv[1:])
