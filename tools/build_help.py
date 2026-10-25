#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Генератор справки NetAnomalyAnalyzer.

Из markdown-исходника (help_src/manual.md) собирает артефакты:

  1) resources/netanalyzer.hlp — настоящий файл Windows Help (WinHelp,
     формат .hlp). Реализован по формату Winterhoff/HELPDECO: внутренние
     файлы |SYSTEM, |FONT, |TOPIC, |CONTEXT, |TTLBTREE, директория B+,
     темы сжаты LZ77 (SYSTEM: Minor=21, Flags=4 -> TopicBlockSize=4k,
     DecompressSize=16k). Проверяется независимыми ридерами:
     tools/hlp_probe.py и helpdeco (helpdeco - декомпилятор в RTF).

  2) HTML-версия той же справки для встроенного окна приложения
     (fallback там, где нет просмотрщика WinHlp32):

       resources/manual.html     — одной страницей (оглавление + темы),
                                   для предпросмотра в браузере;
       resources/manual_toc.html — только оглавление (левая неподвижная
                                   колонка окна справки);
       resources/manual_body.html — только темы (правая колонка,
                                   единственный скроллящийся блок).

Разметка исходника (сознательно минимальная):

    ## topic_id = Заголовок темы      — начало новой темы (раздела)
    Обычный текст.                    — абзац
    ### Подзаголовок                 — подзаголовок внутри темы
    - пункт списка                    — элемент списка
    [[topic_id|текст ссылки]]         — ссылка на другую тему
    Текст **жирный** текст            — выделение снимается (жирный в HLP
                                        пока не передаётся, в HTML — да)

Запуск:
    python tools/build_help.py                 # регенерация артефактов
    python tools/build_help.py --check         # проверить, не записывая
"""
import argparse
import os
import re
import struct
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
PROJECT = os.path.dirname(HERE)
SRC = os.path.join(PROJECT, "help_src", "manual.md")
OUT_HLP = os.path.join(PROJECT, "resources", "netanalyzer.hlp")
OUT_HTML = os.path.join(PROJECT, "resources", "manual.html")
OUT_TOC_HTML = os.path.join(PROJECT, "resources", "manual_toc.html")
OUT_BODY_HTML = os.path.join(PROJECT, "resources", "manual_body.html")
HELP_TITLE = "NetAnomalyAnalyzer - Руководство пользователя"

# Параметры блоков |TOPIC (SYSTEM: Minor=21, Flags=4)
LOGICAL_BLOCK = 0x4000      # DecompressSize: распакованный блок, шаг адресации
PHYS_BLOCK = 4096           # TopicBlockSize
BLOCK_DATA_LIMIT = PHYS_BLOCK - 12   # максимум сжатых байт на блок (4084)
DATA_CAP = LOGICAL_BLOCK    # максимум распакованных данных на блок
# При данных меньше этого предела сжатый поток гарантированно влезает
# в BLOCK_DATA_LIMIT (худший случай: чистые литералы, 9/8 от входа).
SAFE_UNC = BLOCK_DATA_LIMIT * 8 // 9

FONT_BODY, FONT_TITLE, FONT_SUB = 0, 1, 2


def cp1251(text, where=""):
    """Кодировка WinHelp-строк (ANSI). Невместимые символы — ошибка сборки."""
    try:
        return text.encode("cp1251")
    except UnicodeEncodeError as e:
        raise SystemExit(
            f"Символ {text[e.start:e.end]!r} не входит в cp1251 ({where}): "
            f"{text[:60]}...")


# =========================================================================
#  Сжатые числа WinHelp (compressed word/long)
# =========================================================================

def cword(v):
    """Компрессированный unsigned short: 1 байт (<128) либо 2 байта."""
    if not 0 <= v <= 0x7FFF:
        raise ValueError(f"cword вне диапазона: {v}")
    if v <= 127:
        return bytes([v << 1])
    return struct.pack("<H", (v << 1) | 1)


def clong(v):
    """Компрессированный signed long: 2 байта либо 4 байта."""
    if -0x4000 <= v <= 0x3FFF:
        return struct.pack("<H", (v + 0x4000) << 1)
    if -0x40000000 <= v <= 0x3FFFFFFF:
        return struct.pack("<I", (((v + 0x40000000) << 1) | 1) & 0xFFFFFFFF)
    raise ValueError(f"clong вне диапазона: {v}")


# =========================================================================
#  Контекстный хэш WinHelp (таблица из Winterhoff/HELPDECO, signed-значения)
# =========================================================================

_HASH_RAW = [
    0x00, 0xD1, 0xD2, 0xD3, 0xD4, 0xD5, 0xD6, 0xD7, 0xD8, 0xD9, 0xDA, 0xDB,
    0xDC, 0xDD, 0xDE, 0xDF, 0xE0, 0xE1, 0xE2, 0xE3, 0xE4, 0xE5, 0xE6, 0xE7,
    0xE8, 0xE9, 0xEA, 0xEB, 0xEC, 0xED, 0xEE, 0xEF, 0xF0, 0x0B, 0xF2, 0xF3,
    0xF4, 0xF5, 0xF6, 0xF7, 0xF8, 0xF9, 0xFA, 0xFB, 0xFC, 0xFD, 0x0C, 0xFF,
    0x0A, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B,
    0x0C, 0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
    0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F, 0x20, 0x21, 0x22, 0x23,
    0x24, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0D,
    0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1A, 0x1B,
    0x1C, 0x1D, 0x1E, 0x1F, 0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27,
    0x28, 0x29, 0x2A, 0x2B, 0x2C, 0x2D, 0x2E, 0x2F, 0x50, 0x51, 0x52, 0x53,
    0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5A, 0x5B, 0x5C, 0x5D, 0x5E, 0x5F,
    0x60, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6A, 0x6B,
    0x6C, 0x6D, 0x6E, 0x6F, 0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77,
    0x78, 0x79, 0x7A, 0x7B, 0x7C, 0x7D, 0x7E, 0x7F, 0x80, 0x81, 0x82, 0x83,
    0x0B, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8A, 0x8B, 0x8C, 0x8D, 0x8E, 0x8F,
    0x90, 0x91, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9A, 0x9B,
    0x9C, 0x9D, 0x9E, 0x9F, 0xA0, 0xA1, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7,
    0xA8, 0xA9, 0xAA, 0xAB, 0xAC, 0xAD, 0xAE, 0xAF, 0xB0, 0xB1, 0xB2, 0xB3,
    0xB4, 0xB5, 0xB6, 0xB7, 0xB8, 0xB9, 0xBA, 0xBB, 0xBC, 0xBD, 0xBE, 0xBF,
    0xC0, 0xC1, 0xC2, 0xC3, 0xC4, 0xC5, 0xC6, 0xC7, 0xC8, 0xC9, 0xCA, 0xCB,
    0xCC, 0xCD, 0xCE, 0xCF,
]
# значения таблицы — signed char
_HASH_TABLE = [v - 0x100 if v >= 0x80 else v for v in _HASH_RAW]


def context_hash(name):
    """Хэш контекстной строки WinHelp (пустая строка даёт 1)."""
    if not name:
        return 1
    h = 0
    for ch in name.encode("ascii"):
        h = (h * 43 + _HASH_TABLE[ch]) & 0xFFFFFFFF
    return h


# =========================================================================
#  LZ77-компрессор WinHelp (Winterhoff: флаг-байт, биты LSB-first,
#  совпадение = u16 ((len-3)<<12 | dist-1), окно 4096, длина 3..18)
# =========================================================================

def lz77_compress(data):
    N, F, T = 4096, 16, 3
    out = bytearray()
    pos_index = {}          # байт -> позиции в уже обработанном префиксе
    i, n = 0, len(data)
    flag_at, mask = None, 1

    def emit(token, is_match):
        nonlocal flag_at, mask
        if mask == 1:
            flag_at = len(out)
            out.append(0)
        if is_match:
            out[flag_at] |= mask          # бит 1 = совпадение, 0 = литерал
        out.extend(token)
        mask <<= 1
        if mask == 0x100:
            mask = 1

    while i < n:
        best_len, best_dist = 0, 0
        if i + T <= n:
            cands = pos_index.get(data[i])
            if cands:
                limit = min(F, n - i)
                probe = data[i:i + 3]
                for p in cands:
                    if p < i - N:
                        continue
                    if data[p:p + 3] != probe:
                        continue
                    l = 3
                    while l < limit and data[p + l] == data[i + l]:
                        l += 1
                    if l > best_len:
                        best_len, best_dist = l, i - p - 1
                        if l == limit:
                            break
        if best_len >= T:
            emit(struct.pack("<H", ((best_len - 3) << 12)
                             | (best_dist & 0xFFF)), True)
            for k in range(best_len):
                pos_index.setdefault(data[i + k], []).append(i + k)
            i += best_len
        else:
            emit(bytes([data[i]]), False)
            pos_index.setdefault(data[i], []).append(i)
            i += 1
    return bytes(out)


def zero_pad_junk(pad):
    """Сколько выходных байт добавит нулевой паддинг при декомпрессии."""
    q, r = divmod(pad, 9)
    return 8 * q + max(r - 1, 0)


# =========================================================================
#  Разбор markdown-исходника
# =========================================================================

RE_TOPIC = re.compile(r"^##\s+([A-Za-z0-9_.]+)\s*=\s*(.+?)\s*$")
RE_SUB = re.compile(r"^###\s+(.+?)\s*$")
RE_BULLET = re.compile(r"^-\s+(.+?)\s*$")
RE_LINK = re.compile(r"\[\[([A-Za-z0-9_.]+)\|([^\]]+)\]\]")


def parse_runs(text):
    """Строка -> сегменты: ("text", s) | ("link", id, label).

    Жирное выделение **...** при компиляции снимается (в WinHelp стиль
    передаётся отдельным механизмом, здесь он не нужен).
    """
    text = re.sub(r"\*\*([^*]+)\*\*", r"\1", text)
    runs = []
    pos = 0
    for m in RE_LINK.finditer(text):
        if m.start() > pos:
            runs.append(("text", text[pos:m.start()]))
        runs.append(("link", m.group(1), m.group(2)))
        pos = m.end()
    if pos < len(text):
        runs.append(("text", text[pos:]))
    return runs or [("text", "")]


def parse_manual(path):
    with open(path, "r", encoding="utf-8") as f:
        lines = f.read().splitlines()
    topics = []
    cur = None
    for raw in lines:
        line = raw.rstrip()
        m = RE_TOPIC.match(line)
        if m:
            cur = {"id": m.group(1), "title": m.group(2).strip(), "paras": []}
            topics.append(cur)
            continue
        if cur is None:
            if line.strip():
                raise SystemExit(f"Текст до первой темы: {raw!r}")
            continue
        if not line.strip():
            continue
        m = RE_SUB.match(line)
        if m:
            cur["paras"].append(
                {"kind": "sub", "runs": [("text", m.group(1).strip())]})
            continue
        m = RE_BULLET.match(line)
        if m:
            cur["paras"].append(
                {"kind": "bullet", "runs": parse_runs("\u2022 " + m.group(1))})
            continue
        cur["paras"].append({"kind": "body", "runs": parse_runs(line)})
    ids = [t["id"] for t in topics]
    if len(ids) != len(set(ids)):
        raise SystemExit("Дубликаты идентификаторов тем в исходнике")
    for t in topics:
        cp1251(t["title"], f"тема {t['id']}")
        for p in t["paras"]:
            for r in p["runs"]:
                if r[0] == "link" and r[1] not in ids:
                    raise SystemExit(
                        f"Ссылка [[{r[1]}|...]] в теме {t['id']} "
                        f"ведёт на неизвестную тему")
    return topics


# =========================================================================
#  Записи тем WinHelp
# =========================================================================

def make_link(record_type, l1, l2):
    """TOPICLINK (21 байт) + LinkData1 + LinkData2."""
    data_len1 = 21 + len(l1)
    block_size = data_len1 + len(l2)
    return (struct.pack("<IIiii", block_size, len(l2), -1, -1, data_len1)
            + bytes([record_type]) + l1 + l2)


def topic_header_record(title_cp, span, topic_num, scroll_pos, next_topic_pos):
    """TL_TOPICHDR (RecordType=2): LinkData1 = TOPICHEADER (28 байт)."""
    l1 = struct.pack("<iiiiiii", span, -1, -1, topic_num, -1,
                     scroll_pos, next_topic_pos)
    return make_link(2, l1, title_cp + b"\x00")


def display_record_size(para):
    """Длина display-записи (не зависит от значений целей ссылок)."""
    return len(display_record(para, {})[0])


def display_record(para, id_to_pos):
    """TL_DISPLAY (RecordType=0x20): один абзац.

    Строки (LinkData2, NUL-терминированные) и команды (LinkData1)
    чередуются: строка, затем управляющая команда; завершает цепочку 0xFF.
    """
    font = {"title": FONT_TITLE, "sub": FONT_SUB}.get(para.get("kind"),
                                                      FONT_BODY)
    seq = [(b"", None)]            # (строка, команда после неё)

    def set_cmd(c):
        seq[-1] = (seq[-1][0], c)

    for run in para["runs"]:
        if run[0] == "link":
            target = id_to_pos.get(run[1], 0)
            if seq[-1][1] is None:
                set_cmd(("e1", target))         # прыжок -> следующая строка —
            else:                               # метка хотспота
                seq.append((b"", ("e1", target)))
            seq.append((cp1251(run[2], "ссылка"), ("89",)))
        else:
            if seq[-1][1] is None:
                set_cmd(("font", font))
            seq.append((cp1251(run[1], "текст"), None))
    if seq[-1][1] is None:
        set_cmd(("82",))                        # конец абзаца
    else:
        seq.append((b"", ("82",)))
    seq.append((b"", ("ff",)))                  # конец форматирования

    l2 = b"".join(s + b"\x00" for s, _c in seq)
    topic_length = len(l2)                      # Σ(strlen+1)

    cmds = bytearray()
    for _s, c in seq:
        if c[0] == "font":
            cmds.append(0x80)
            cmds.extend(struct.pack("<h", c[1]))
        elif c[0] == "e1":
            cmds.append(0xE1)
            cmds.extend(struct.pack("<I", c[1]))
        elif c[0] == "89":
            cmds.append(0x89)
        elif c[0] == "82":
            cmds.append(0x82)
        elif c[0] == "ff":
            cmds.append(0xFF)

    # параграф: 4 служебных байта (00 80 00 00) + u16 флагов (0)
    l1_rest = b"\x00\x80\x00\x00" + struct.pack("<H", 0) + bytes(cmds)
    # TopicSize = длина остальной части LinkData1 + размеры кодов префиксов
    size_ts = 2
    for _ in range(3):
        ts = len(l1_rest) + size_ts + (1 if topic_length <= 127 else 2)
        size_ts = len(clong(ts))
    l1 = clong(ts) + cword(topic_length) + l1_rest
    return make_link(0x20, l1, l2), topic_length


# =========================================================================
#  Сборка |TOPIC
# =========================================================================

def pack_records(recs, sizes, limit=BLOCK_DATA_LIMIT):
    """Жадная раскладка записей по блокам по АКТУАЛЬНЫМ записям recs.

    Блок закрывается, если добавление записи превысило бы лимит
    распакованных данных (16384), сжатый размер limit либо если после
    нулевого паддинга распакованный хвост (паддинг декомпрессиируется в
    нули) перешагнул бы буфер 16384. Записи не делятся: ушедшая запись
    начинает следующий блок.
    """
    blocks, cur, cur_len = [], [], 0
    for i, sz in enumerate(sizes):
        if cur:
            total = cur_len + sz
            bad = total > DATA_CAP
            if not bad and total >= SAFE_UNC:
                packed = len(lz77_compress(
                    b"".join(recs[j] for j in cur + [i])))
                bad = (packed > limit
                       or total + zero_pad_junk(limit - packed) > DATA_CAP)
            if bad:
                blocks.append(cur)
                cur, cur_len = [], 0
        cur.append(i)
        cur_len += sz
    if cur:
        blocks.append(cur)
    return blocks


def assemble_topic(topics):
    """Возвращает (байты |TOPIC, info по темам).

    info[id] = {"pos": TOPICPOS заголовка, "offset": TOPICOFFSET}
    """
    # записи: заголовок темы + display-абзац с заголовком + абзацы
    entries = []
    for t in topics:
        entries.append(("hdr", t["id"]))
        entries.append(("disp", {"kind": "title", "runs": [("text",
                                                            t["title"])]}))
        for p in t["paras"]:
            entries.append(("disp", p))

    # предварительные записи (цели ссылок — заглушки 0; длины записей от
    # значений целей не зависят — меняется лишь содержимое, влияющее на
    # сжатие)
    stub_recs, sizes = [], []
    for e in entries:
        if e[0] == "hdr":
            t = next(tt for tt in topics if tt["id"] == e[1])
            rec = topic_header_record(
                cp1251(t["title"], t["id"]), 0, 0, 0, -1)
        else:
            rec = display_record(e[1], {})[0]
        stub_recs.append(rec)
        sizes.append(len(rec))

    def finalize(blocks):
        """Позиции, charcount и финальные записи для раскладки blocks."""
        pos = [0] * len(entries)
        chars = [0] * len(entries)
        block_of = [0] * len(entries)
        for bi, group in enumerate(blocks):
            off, bc = 0, 0
            for i in group:
                pos[i] = bi * LOGICAL_BLOCK + 12 + off
                chars[i] = bc
                block_of[i] = bi
                off += sizes[i]
                if entries[i][0] == "disp":
                    bc += display_record(entries[i][1], {})[1]
        id_to_pos = {e[1]: pos[i] for i, e in enumerate(entries)
                     if e[0] == "hdr"}
        info = {}
        for i, e in enumerate(entries):
            if e[0] == "hdr":
                info[e[1]] = {"pos": pos[i],
                              "offset": (block_of[i] << 15) | chars[i]}
        recs = []
        for i, e in enumerate(entries):
            prev = pos[i - 1] if i > 0 else -1
            nxt = pos[i + 1] if i + 1 < len(entries) else -1
            if e[0] == "hdr":
                t = next(tt for tt in topics if tt["id"] == e[1])
                j = i + 1
                while j < len(entries) and entries[j][0] != "hdr":
                    j += 1
                span = (pos[j] if j < len(entries)
                        else pos[-1] + sizes[-1]) - pos[i]
                scroll = pos[i + 1] if i + 1 < len(entries) else -1
                nxt_topic = pos[j] if j < len(entries) else -1
                rec = topic_header_record(cp1251(t["title"], t["id"]), span,
                                          topics.index(t), scroll, nxt_topic)
            else:
                rec, _tl = display_record(e[1], id_to_pos)
            b = bytearray(rec)
            struct.pack_into("<i", b, 8, prev)      # PrevBlock
            struct.pack_into("<i", b, 12, nxt)      # NextBlock
            recs.append(bytes(b))
        assert all(len(a) == b for a, b in zip(recs, sizes))
        return recs, pos, info

    # Раскладка и содержимое зависят друг от друга (цели ссылок и поля
    # заголовков — это TOPICPOS, меняющие сжимаемость): ищем неподвижную
    # точку итерацией.
    blocks = pack_records(stub_recs, sizes)
    recs, pos, info = finalize(blocks)
    for _ in range(10):
        nb = pack_records(recs, sizes)
        if nb == blocks:
            break
        blocks = nb
        recs, pos, info = finalize(blocks)

    # сжатие блоков; последний блок — без паддинга (как в реальных файлах)
    out = bytearray()
    last = len(blocks) - 1
    for bi, group in enumerate(blocks):
        data = b"".join(recs[j] for j in group)
        packed = lz77_compress(data)
        if len(packed) > BLOCK_DATA_LIMIT:
            raise SystemExit(f"Блок {bi} |TOPIC не ужимается в "
                             f"{BLOCK_DATA_LIMIT} байт")
        # заголовок блока: {Last=последняя запись пред. блока, First=смещение
        # блока, LastHdr=последний заголовок темы пред. блока}
        if bi == 0:
            prev_last_link, prev_last_hdr = -1, 0
        else:
            prev_group = blocks[bi - 1]
            prev_last_link = pos[prev_group[-1]]
            prev_hdrs = [i for i in prev_group if entries[i][0] == "hdr"]
            prev_last_hdr = pos[prev_hdrs[-1]] if prev_hdrs else 0
        out += struct.pack("<III", prev_last_link & 0xFFFFFFFF,
                           bi * LOGICAL_BLOCK + 12, prev_last_hdr)
        out += packed
        if bi != last:
            pad = BLOCK_DATA_LIMIT - len(packed)
            out += b"\x00" * pad
    return bytes(out), info


# =========================================================================
#  B+ деревья (одна страница-лист), внутренние файлы, сборка файла
# =========================================================================

BTREE_HDR_SIZE = 38


def btree_leaf(entries, page_size, structure, flags):
    """Содержимое внутреннего файла B+ дерева: BTREEHEADER + один лист."""
    hdr = struct.pack("<HHH", 0x293B, flags, page_size)
    hdr += structure.encode("ascii") + b"\x00" * (16 - len(structure))
    hdr += struct.pack("<hhhhHHi", 0, 0, 0, -1, 1, 1, len(entries))
    body = b"".join(entries)
    node = struct.pack("<HHhh", page_size - 8 - len(body), len(entries),
                       -1, -1)
    page = node + body
    if len(page) > page_size:
        raise SystemExit(f"Btree-страница {structure} переполнена: "
                         f"{len(page)} > {page_size}")
    page += b"\x00" * (page_size - len(page))
    return hdr + page


def file_block(content, flags=0):
    """FILEHEADER (9 байт) + содержимое. UsedSpace не считает FILEHEADER."""
    return struct.pack("<IIB", len(content) + 9, len(content), flags) \
        + content


def build_hlp(topics):
    # --- |SYSTEM: Minor=21 (HC31), Flags=4 (LZ77 4k) ---
    sys_rec = struct.pack("<HHHIH", 0x036C, 21, 1, int(time.time()), 4)
    title = cp1251(HELP_TITLE, "заголовок") + b"\x00"
    sys_rec += struct.pack("<HH", 1, len(title)) + title   # TITLE
    sys_rec += struct.pack("<HH", 2, 1) + b"\x00"          # «всегда 0»
    topic_data, info = assemble_topic(topics)
    contents = info[topics[0]["id"]]["offset"]
    sys_rec += struct.pack("<HH", 3, 4) + struct.pack("<I", contents)
    system_file = file_block(sys_rec)

    # --- |FONT: старый формат (FacenamesOffset=8), oldfont 11 байт ---
    faces = [b"Arial".ljust(20, b"\x00")] * 3
    fhdr = struct.pack("<HHHH", 3, 3, 8, 8 + 20 * 3)
    descs = b""
    for half, attr in ((20, 0x00), (28, 0x01), (24, 0x03)):
        descs += (bytes([attr, half, 0x03]) + struct.pack("<H", 0)
                  + bytes([0, 0, 0, 255, 255, 255]))
    font_file = file_block(fhdr + b"".join(faces) + descs)

    # --- |TOPIC ---
    topic_file = file_block(topic_data)

    # --- |CONTEXT: {hash, TOPICOFFSET}, сортировка по signed ---
    ctx_entries = [struct.pack("<Ii", context_hash(t["id"]),
                               info[t["id"]]["offset"]) for t in topics]
    ctx_entries.sort(key=lambda e: struct.unpack("<i", e[:4])[0])
    ctx_file = file_block(btree_leaf(ctx_entries, 2048, "L4", 0x0002))

    # --- |TTLBTREE: {TOPICOFFSET, title} ---
    order = sorted(range(len(topics)),
                   key=lambda i: info[topics[i]["id"]]["offset"])
    ttl_entries = [struct.pack("<I", info[topics[i]["id"]]["offset"])
                   + cp1251(topics[i]["title"], topics[i]["id"]) + b"\x00"
                   for i in order]
    ttl_file = file_block(btree_leaf(ttl_entries, 2048, "Lz", 0x0002))

    # --- размещение внутренних файлов, каталог ---
    parts = [("|SYSTEM", system_file), ("|FONT", font_file),
             ("|TOPIC", topic_file), ("|CONTEXT", ctx_file),
             ("|TTLBTREE", ttl_file)]
    cursor, offsets, body = 16, {}, bytearray()
    for name, content in parts:
        offsets[name] = cursor
        body += content
        cursor += len(content)

    dir_entries = []
    for name, _c in sorted(parts):
        e = cp1251(name, "каталог") + b"\x00" + struct.pack("<I",
                                                           offsets[name])
        dir_entries.append(e)
    dir_file = file_block(btree_leaf(dir_entries, 1024, "z4", 0x0402),
                          flags=4)
    dir_offset = cursor

    file_size = cursor + len(dir_file)
    header = struct.pack("<iiii", 0x00035F3F, dir_offset, -1, file_size)
    data = header + bytes(body) + dir_file
    assert len(data) == file_size
    return data, info


# =========================================================================
#  HTML-версия
# =========================================================================

def html_escape(s):
    return (s.replace("&", "&amp;").replace("<", "&lt;")
             .replace(">", "&gt;"))


_HTML_STYLE = ("<style>\n"
               " body { font-family: 'Segoe UI', Arial, sans-serif;\n"
               "        color: #1c2733; background: #fff; }\n"
               " .toc { background: #f2f5f8; border-right: 1px solid #d6dde4; }\n"
               " .tochd { font-weight: bold; font-size: 12px; color: #5a6b7c; }\n"
               " .tocitem { margin: 0; padding: 4px 14px; font-size: 13px; }\n"
               " .tocitem a { color: #23405c; text-decoration: none; }\n"
               " .tocitem a:hover { background: #dfe9f3; }\n"
               " h1 { font-size: 21px; color: #14344f;\n"
               "      border-bottom: 2px solid #e3e9ef; padding-bottom: 6px;\n"
               "      margin-top: 0; }\n"
               " h2 { font-size: 16px; color: #1d4f76; margin-top: 22px; }\n"
               " p { line-height: 1.5; margin: 9px 0; font-size: 14px; }\n"
               " ul { margin: 8px 0; padding-left: 24px; }\n"
               " li { font-size: 14px; line-height: 1.5; margin: 3px 0; }\n"
               " a { color: #1d5fa8; }\n"
               " code { font-family: Consolas, monospace; background: #f0f4f8;\n"
               "        padding: 1px 4px; font-size: 13px; }\n"
               "</style></head>\n")


def _html_head():
    return ('<!DOCTYPE html>\n<html lang="ru"><head><meta charset="utf-8">\n'
            '<title>' + html_escape(HELP_TITLE) + '</title>\n' + _HTML_STYLE)


def _topic_html(t):
    """Одна тема справки: якорь + заголовок + абзацы (общий код для всех версий)."""
    out = ['<div class="topic"><a name="%s"></a>' % t["id"]]  # <a name> — якорь, по нему Qt находит переход на тему
    out.append("<h1>%s</h1>" % html_escape(t["title"]))
    for p in t["paras"]:
        bullet = p["kind"] == "bullet"
        inner = []
        for i, r in enumerate(p["runs"]):
            if r[0] == "link":
                inner.append('<a href="#%s">%s</a>'
                             % (r[1], html_escape(r[2])))
            else:
                text = r[1]
                # Маркер «• » из исходника убирается: маркер списка
                # задаёт сам <li>, иначе получается двойной маркер.
                if bullet and i == 0:
                    text = text.lstrip("\u2022 \u00a0")
                inner.append(html_escape(text))
        text = "".join(inner)
        if bullet:
            out.append("<ul><li>%s</li></ul>" % text)
        elif p["kind"] == "sub":
            out.append("<h2>%s</h2>" % text)
        else:
            out.append("<p>%s</p>" % text)
    out.append("</div>")
    return "".join(out)


def _toc_html(topics, heading=True):
    parts = []
    # Каждый пункт содержания — в своём абзаце: ссылки не слипаются
    # ни в QTextBrowser, ни в браузере.
    if heading:
        # Qt не применяет margin-top к самому первому блоку документа,
        # поэтому сверху — разделительный абзац (высота задаёт отступ).
        parts.append('<p style="margin:0; font-size:9px;">&#160;</p>\n')
        parts.append('<p class="tochd" style="margin-top:0; '
                     'margin-bottom:8px; margin-left:14px;">Содержание</p>\n')
    for t in topics:
        parts.append('<p class="tocitem"><a href="#%s">%s</a></p>\n'
                     % (t["id"], html_escape(t["title"])))
    return "".join(parts)


def build_body_html(topics):
    """Только темы — правая колонка встроенного окна справки (прокручивается)."""
    return _html_head() + "<body>\n" \
        + "".join(_topic_html(t) for t in topics) \
        + "</body></html>\n"


def build_toc_html(topics):
    """Только оглавление — левая неподвижная колонка окна справки."""
    head = ('<!DOCTYPE html>\n<html lang="ru"><head><meta charset="utf-8">\n'
            '<title>Содержание</title>\n'
            "<style>\n"
            " body { font-family: 'Segoe UI', Arial, sans-serif;\n"
            "        color: #1c2733; background: #f2f5f8; }\n"
            " .tochd { font-weight: bold; font-size: 12px; color: #5a6b7c; }\n"
            " .tocitem { margin: 0; padding: 4px 14px; font-size: 13px; }\n"
            " a { color: #23405c; text-decoration: none; }\n"
            "</style></head>\n")
    return head + "<body>\n" + _toc_html(topics) + "</body></html>\n"


def build_html(topics):
    """Полный HTML одной страницей (оглавление + темы) — предпросмотр в браузере."""
    parts = [_html_head(),
             # Двухколоночная раскладка — таблицей, а не flexbox: QTextBrowser
             # (встроенное окно справки) flexbox не понимает, зато таблицы
             # рендерит корректно; в браузере результат тот же.
             "<body>\n"
             '<table width="100%" cellpadding="0" cellspacing="0" border="0">\n'
             '<tr valign="top">\n'
             '<td class="toc" width="240" bgcolor="#f2f5f8"\n'
             '    style="background-color:#f2f5f8; '
             'border-right:1px solid #d6dde4; padding:10px 0;">\n',
             _toc_html(topics),
             '</td>\n<td class="doc" style="padding:14px 24px;">\n']
    for t in topics:
        parts.append(_topic_html(t))
    parts.append("</td>\n</tr>\n</table>\n</body></html>\n")
    return "".join(parts)


# =========================================================================
#  main
# =========================================================================

def main():
    ap = argparse.ArgumentParser(
        description="Генератор справки NetAnomalyAnalyzer (.hlp + .html)")
    ap.add_argument("--src", default=SRC)
    ap.add_argument("--hlp", default=OUT_HLP)
    ap.add_argument("--html", default=OUT_HTML)
    ap.add_argument("--toc-html", default=OUT_TOC_HTML)
    ap.add_argument("--body-html", default=OUT_BODY_HTML)
    ap.add_argument("--check", action="store_true",
                    help="проверить исходник и артефакт, ничего не записывая")
    args = ap.parse_args()

    topics = parse_manual(args.src)
    if not topics:
        raise SystemExit("Пустой мануал: нет ни одной темы ## id = Заголовок")
    hlp, info = build_hlp(topics)
    html = build_html(topics)
    toc = build_toc_html(topics)
    body = build_body_html(topics)

    if not args.check:
        os.makedirs(os.path.dirname(args.hlp), exist_ok=True)
        with open(args.hlp, "wb") as f:
            f.write(hlp)
        with open(args.html, "w", encoding="utf-8") as f:
            f.write(html)
        with open(args.toc_html, "w", encoding="utf-8") as f:
            f.write(toc)
        with open(args.body_html, "w", encoding="utf-8") as f:
            f.write(body)
    print(f"тем: {len(topics)}, записей: ~{sum(len(t['paras']) + 2 for t in topics)},"
          f" hlp: {len(hlp)} байт, html: {len(html)} байт"
          f" (toc: {len(toc)}, body: {len(body)})")
    for t in topics:
        o = info[t["id"]]["offset"]
        print(f"  {t['id']:12s} offset=0x{o:04X} (блок {o >> 15},"
              f" симв {o & 0x7FFF}) {t['title']}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
