# -*- coding: utf-8 -*-
"""Зонд-анализатор файлов WinHelp (.hlp).

Независимый ридер формата (по Winterhoff/helpdeco). Используется для
разбора эталонных настоящих .hlp и для проверки собственного компилятора
справки (tools/build_help.py) — оба должны читаться одинаково.

Запуск:  python hlp_probe.py file.hlp [--topics N] [--hex N]
"""
import struct
import sys

# ---------------------------------------------------------------- LZ77 ----

def lz77_decompress(src: bytes, max_out: int) -> bytes:
    """Декодер LZ77 из WinHelp (Winterhoff: флаг-байт, биты LSB-first)."""
    out = bytearray()
    k = 0  # индекс в src
    while k < len(src) and len(out) < max_out:
        flags = src[k]
        k += 1
        for mask in range(8):
            if k >= len(src):
                break
            if flags & (1 << mask):
                if k + 2 > len(src):
                    return bytes(out)
                ref = src[k] | (src[k + 1] << 8)
                k += 2
                length = ((ref >> 12) & 15) + 3
                dist = (ref & 0xFFF) + 1
                for _ in range(length):
                    if len(out) >= max_out:
                        return bytes(out)
                    b = out[len(out) - dist] if dist <= len(out) else 0
                    out.append(b)
            else:
                out.append(src[k])
                k += 1
    return bytes(out)


F_DEFAULT = 16

# ------------------------------------------------------------- заголовок --

def u32(b, o): return struct.unpack_from("<I", b, o)[0]
def s32(b, o): return struct.unpack_from("<i", b, o)[0]
def u16(b, o): return struct.unpack_from("<H", b, o)[0]
def s16(b, o): return struct.unpack_from("<h", b, o)[0]


class Hlp:
    def __init__(self, path):
        with open(path, "rb") as f:
            self.data = f.read()
        d = self.data
        self.magic = s32(d, 0)
        self.dir_start = s32(d, 4)
        self.free_chain = s32(d, 8)
        self.file_size = s32(d, 12)

    # ---- внутренние файлы ----
    def file_header(self, off):
        reserved, used, flags = u32(self.data, off), u32(self.data, off + 4), self.data[off + 8]
        return reserved, used, flags

    def btree_header(self, off):
        """BTREEHEADER (38 байт) сразу после FILEHEADER."""
        magic, flags, page_size = struct.unpack_from("<HHH", self.data, off)
        structure = self.data[off + 6:off + 22]
        zero, splits, root, negone, total_pages, levels = struct.unpack_from(
            "<hhhhhh", self.data, off + 22)
        total = u32(self.data, off + 34)
        return dict(magic=magic, flags=flags, page_size=page_size,
                    structure=structure, root=root, negone=negone,
                    total_pages=total_pages, levels=levels, total=total)

    BTREE_HDR_SIZE = 38

    def directory(self):
        """Возвращает {имя: offset FILEHEADER}."""
        fo = self.dir_start
        reserved, used, flags = self.file_header(fo)
        bh = self.btree_header(fo + 9)
        base = fo + 9 + self.BTREE_HDR_SIZE
        node = base + bh["root"] * bh["page_size"]
        (unused, nentries, prev, nxt) = struct.unpack_from("<HHhh", self.data, node)
        entries = {}
        p = node + 8
        for _ in range(nentries):
            e = self.data.index(b"\x00", p)
            name = self.data[p:e].decode("cp1251", "replace")
            p = e + 1
            off = u32(self.data, p)
            p += 4
            entries[name] = off
        return entries, bh, reserved, used, flags

    def walk(self):
        entries, bh, dres, dused, dflags = self.directory()
        print(f"HELPHEADER magic=0x{self.magic & 0xFFFFFFFF:08X} dir={self.dir_start} "
              f"free={self.free_chain} size={self.file_size} (реально {len(self.data)})")
        print(f"DIR filehdr reserved={dres} used={dused} flags={dflags}")
        print(f"DIR btree: magic=0x{bh['magic']:04X} page_size={bh['page_size']} "
              f"root={bh['root']} levels={bh['levels']} pages={bh['total_pages']} "
              f"total={bh['total']} flags=0x{bh['flags']:04X} "
              f"structure={bh['structure']!r}")
        for name, off in sorted(entries.items()):
            r, u, fl = self.file_header(off)
            print(f"  {name:12s} @ {off:7d}  reserved={r:6d} used={u:6d} flags={fl} "
                  f"content={u} (согласие={r == u + 9})")
        return entries

    def system(self, off):
        r, u, fl = self.file_header(off)
        p = off + 9
        magic, minor, major, gendate, flags = struct.unpack_from("<HHHIH", self.data, p)
        p += 12
        print(f"|SYSTEM: magic=0x{magic:04X} major={major} minor={minor} flags={flags} "
              f"gendate={gendate}")
        end = off + 9 + u
        while p + 4 <= end:
            rt, sz = struct.unpack_from("<HH", self.data, p)
            payload = self.data[p + 4:p + 4 + sz]
            print(f"  rec type=0x{rt:04X} size={sz} data={payload[:60]!r}")
            p += 4 + sz

    def font(self, off):
        r, u, fl = self.file_header(off)
        p = off + 9
        num_faces, num_desc, faces_off, desc_off = struct.unpack_from("<HHHH", self.data, p)
        head = self.data[p:p + 16]
        print(f"|FONT: faces={num_faces} desc={num_desc} faces_off={faces_off} "
              f"desc_off={desc_off}")
        print(f"  header hex={head.hex(' ')}")
        if faces_off >= 12:
            nstyles, ncolors, styles_off, colors_off = struct.unpack_from(
                "<HHHH", self.data, p + 8)
            print(f"  (+new: styles={nstyles} colors={ncolors} styles_off={styles_off} "
                  f"colors_off={colors_off})")
        base = p + faces_off
        stride = (desc_off - faces_off) // num_faces if num_faces else 0
        for i in range(num_faces):
            raw = self.data[base + i * stride: base + (i + 1) * stride]
            print(f"  face[{i}]={raw!r}")
        dbase = p + desc_off
        # формат дескриптора выбирается по FacenamesOffset (как в helpdeco):
        # <12 — OLDFONT (11 байт), >=12 — новый
        size = 11 if faces_off < 12 else 16
        for i in range(num_desc):
            rec = self.data[dbase + i * size: dbase + (i + 1) * size]
            print(f"  desc[{i}] hex={rec.hex(' ')}")
            if size == 11:
                attr, half, fam = rec[0], rec[1], rec[2]
                fi = struct.unpack_from("<H", rec, 3)[0]
                rgb = rec[5:8].hex()
                bgr = rec[8:11].hex()
            else:
                attr, half, fam = rec[0], rec[1], rec[2]
                fi = struct.unpack_from("<H", rec, 3)[0]
                rgb, bgr = rec[7:10].hex(), rec[10:13].hex()
            print(f"       attr=0x{attr:02X} half={half} fam=0x{fam:02X} "
                  f"face={fi} fg=#{rgb} bg=#{bgr}")

    def topic(self, off, max_links=24, hex_bytes=64):
        r, u, fl = self.file_header(off)
        p = off + 9
        print(f"|TOPIC used={u} flags={fl}")
        # предполагаем 4k/16k (LZ77) — определяем по SYSTEM
        sysp = self._system_params()
        minor, sysflags = sysp["minor"], sysp["flags"]
        lz = minor > 16 and sysflags in (4, 8)
        tbs = 4096 if (minor > 16 and sysflags != 8) else 2048
        ds = 0x4000 if minor > 16 else 2048
        print(f"  (minor={minor} flags={sysflags} -> lz={lz} TopicBlockSize={tbs} "
              f"DecompressSize={ds})")

        def load_block(blk):
            """Возвращает (заголовок-12-байт, декомпрессированные данные блока)."""
            fs = p + blk * tbs
            hdr = self.data[fs:fs + 12]
            body = self.data[fs + 12:fs + tbs]
            if lz:
                body = lz77_decompress(body, ds)
            return hdr, body

        hdr0, body0 = load_block(0)
        last_tl, first_tl, last_th = struct.unpack("<III", hdr0)
        print(f"  block0 hdr: LastTopicLink={last_tl:#x} FirstTopicLink={first_tl} "
              f"LastTopicHeader={last_th:#x}")
        # заголовки всех блоков
        if "--blocks" in sys.argv:
            nblocks = (u + tbs - 1) // tbs
            for b in range(nblocks):
                h, _body = load_block(b)
                l1_, f1_, lh_ = struct.unpack("<III", h)
                print(f"  block{b} hdr: Last={l1_:#x} First={f1_:#x} "
                      f"LastHdr={lh_:#x} raw={h.hex(' ')}")
        pos = first_tl  # TOPICPOS (абсолютный от начала |TOPIC content)
        n = 0
        cur_blk = None
        cur_body = None
        chars = 0  # накопленный CharacterCount внутри текущего блока
        while n < max_links:
            blk = (pos - 12) // ds
            off_blk = (pos - 12) % ds
            if blk != cur_blk:
                cur_hdr, cur_body = load_block(blk)
                cur_blk = blk
                chars = 0
            file_pos = off_blk
            bs, dl2, prev, nxt, dl1 = struct.unpack_from("<IIIii", cur_body,
                                                         file_pos)
            rt = cur_body[file_pos + 20]
            l1len = dl1 - 21
            l1 = cur_body[file_pos + 21:file_pos + 21 + l1len]
            l2len = bs - dl1
            l2 = cur_body[file_pos + 21 + l1len:file_pos + bs]
            print(f"  link@{pos}: size={bs} rt=0x{rt:02X} dl1={dl1} l1={l1len} "
                  f"dl2={dl2} l2region={l2len} prev={prev} next={nxt}")
            if rt in (0x20, 0x23, 0x01):
                # показать начало LinkData1
                hx = l1[:hex_bytes].hex(" ")
                print(f"      L1[{len(l1)}]: {hx}")
                if rt == 0x20:
                    # раскодировать TopicSize/TopicLength
                    q = 0
                    ts, q = self.clong(l1, q)
                    tl, q = self.cword(l1, q)
                    chars += tl
                    print(f"      TopicSize={ts} TopicLength={tl} "
                          f"chars_total={chars} (дальше hex "
                          f"{l1[q:q+16].hex(' ')})")
            if rt == 2:  # TOPICHEADER
                to = (blk << 15) | chars
                print(f"      TopicOffset=0x{to:X} (block={blk} chars={chars})")
                if len(l1) >= 28:
                    size, bwd, bfr, tnum, nsc, sc, nxt_t = struct.unpack_from(
                        "<iiiiiii", l1, 0)
                    print(f"      TopicHdr: BlockSize={size} back={bwd} fwd={bfr} "
                          f"num={tnum} nonscroll={nsc} scroll={sc} next={nxt_t}")
                print(f"      L2={l2[:40]!r}")
            if nxt <= 0:
                break
            pos = nxt
            n += 1

    def context(self, off, show=8):
        r, u, fl = self.file_header(off)
        bh = self.btree_header(off + 9)
        print(f"|CONTEXT: reserved={r} used={u} flags={fl}")
        print(f"  btree: magic=0x{bh['magic']:04X} page_size={bh['page_size']} "
              f"root={bh['root']} levels={bh['levels']} total={bh['total']} "
              f"flags=0x{bh['flags']:04X} structure={bh['structure']!r}")
        base = off + 9 + self.BTREE_HDR_SIZE
        node = base + bh["root"] * bh["page_size"]
        (unused, nentries, prev, nxt) = struct.unpack_from("<HHhh", self.data, node)
        print(f"  leaf: entries={nentries} prev={prev} next={nxt}")
        p = node + 8
        for i in range(min(nentries, show)):
            h, t = struct.unpack_from("<Ii", self.data, p)
            print(f"    [{i}] hash=0x{h:08X} offset=0x{t:X} "
                  f"(block={t >> 15} chars={t & 0x7FFF})")
            p += 8

    def ttlbtree(self, off, show=8):
        r, u, fl = self.file_header(off)
        bh = self.btree_header(off + 9)
        print(f"|TTLBTREE: used={u} magic=0x{bh['magic']:04X} "
              f"page_size={bh['page_size']} total={bh['total']} "
              f"flags=0x{bh['flags']:04X} structure={bh['structure']!r}")
        base = off + 9 + self.BTREE_HDR_SIZE
        node = base + bh["root"] * bh["page_size"]
        (unused, nentries, prev, nxt) = struct.unpack_from("<HHhh", self.data, node)
        p = node + 8
        for i in range(min(nentries, show)):
            t = u32(self.data, p)
            e = self.data.index(b"\x00", p + 4)
            title = self.data[p + 4:e].decode("cp1251", "replace")
            print(f"    [{i}] offset=0x{t:X} title={title!r}")
            p = e + 1

    # ---- кодирование/декодирование сжатых чисел ----
    @staticmethod
    def cword(b, p):
        if b[p] & 1:
            return (struct.unpack_from("<H", b, p)[0] >> 1), p + 2
        return b[p] >> 1, p + 1

    @staticmethod
    def clong(b, p):
        if b[p] & 1:
            return (struct.unpack_from("<I", b, p)[0] >> 1) - 0x40000000, p + 4
        return (struct.unpack_from("<H", b, p)[0] >> 1) - 0x4000, p + 2

    def _system_params(self):
        if not hasattr(self, "_sys"):
            entries, *_ = self.directory()
            off = entries["|SYSTEM"]
            magic, minor, major, gendate, flags = struct.unpack_from(
                "<HHHIH", self.data, off + 9)
            self._sys = dict(magic=magic, major=major, minor=minor, flags=flags)
        return self._sys


def main():
    path = sys.argv[1]
    topics = 24
    hexb = 64
    if "--topics" in sys.argv:
        topics = int(sys.argv[sys.argv.index("--topics") + 1])
    if "--hex" in sys.argv:
        hexb = int(sys.argv[sys.argv.index("--hex") + 1])
    h = Hlp(path)
    print(f"=== {path} ({len(h.data)} байт) ===")
    entries = h.walk()
    if "|SYSTEM" in entries:
        h.system(entries["|SYSTEM"])
    if "|FONT" in entries:
        h.font(entries["|FONT"])
    if "|TOPIC" in entries:
        h.topic(entries["|TOPIC"], max_links=topics, hex_bytes=hexb)
    if "|CONTEXT" in entries:
        h.context(entries["|CONTEXT"])
    if "|TTLBTREE" in entries:
        h.twcb = None
        h.ttlbtree(entries["|TTLBTREE"])


if __name__ == "__main__":
    main()
