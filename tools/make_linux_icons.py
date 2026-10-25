#!/usr/bin/env python3
"""Генерация PNG-иконок для Linux-пакетов из resources/app_256.png.

Результат: packaging/linux/icons/hicolor/<размер>x<размер>/apps/netanalyzer.png
(стандартная иерархия hicolor — используется desktop-файлом и DEB/RPM-пакетами).

Запуск:  python tools/make_linux_icons.py
Требуется Pillow:  python -m pip install pillow
"""

from pathlib import Path

from PIL import Image

SIZES = (16, 24, 32, 48, 64, 128, 256)
SOURCE = Path(__file__).resolve().parent.parent / "resources" / "app_256.png"
OUT_ROOT = Path(__file__).resolve().parent.parent / "packaging" / "linux" / "icons"


def main() -> int:
    if not SOURCE.exists():
        print(f"нет исходной иконки: {SOURCE}")
        return 1

    src = Image.open(SOURCE).convert("RGBA")
    for size in SIZES:
        dst = OUT_ROOT / "hicolor" / f"{size}x{size}" / "apps" / "netanalyzer.png"
        dst.parent.mkdir(parents=True, exist_ok=True)
        src.resize((size, size), Image.LANCZOS).save(dst, "PNG")
        print(f"  {dst.relative_to(OUT_ROOT)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
