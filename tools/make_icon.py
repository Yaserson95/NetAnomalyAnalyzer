"""Генерация иконки приложения NetAnomalyAnalyzer (resources/app.ico).

Рисует: тёмно-синий скруглённый «щит», схему сети из узлов и рёбер
и красную точку-аномалию с свечением. Сохраняет мультиразмерный .ico.
"""
import math
import os
from PIL import Image, ImageDraw, ImageFilter

S = 512  # базовый размер рендера


def rounded_gradient(size, radius, top, bottom):
    img = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    grad = Image.new("RGB", (1, size))
    for y in range(size):
        t = y / (size - 1)
        grad.putpixel((0, y), tuple(int(top[i] + (bottom[i] - top[i]) * t) for i in range(3)))
    grad = grad.resize((size, size))
    mask = Image.new("L", (size, size), 0)
    ImageDraw.Draw(mask).rounded_rectangle([0, 0, size - 1, size - 1], radius=radius, fill=255)
    img.paste(grad, (0, 0), mask)
    return img


def main():
    img = rounded_gradient(S, int(S * 0.20), (23, 42, 79), (44, 62, 100))
    draw = ImageDraw.Draw(img)

    cx, cy = S / 2, S / 2
    R = S * 0.42

    # Радарные дуги
    for frac, alpha in ((0.45, 40), (0.70, 28), (0.95, 18)):
        r = R * frac
        bbox = [cx - r, cy - r, cx + r, cy + r]
        draw.arc(bbox, start=205, end=515, fill=(120, 200, 255, alpha), width=max(2, S // 90))

    # Узлы сети и рёбра
    nodes = [(0.30, 0.34), (0.66, 0.26), (0.74, 0.58), (0.38, 0.70), (0.54, 0.48)]
    pts = [(x * S, y * S) for x, y in nodes]
    edges = [(0, 1), (1, 2), (2, 4), (4, 3), (3, 0), (4, 0)]
    w = max(3, S // 60)
    for a, b in edges:
        draw.line([pts[a], pts[b]], fill=(140, 210, 255, 230), width=w)

    anomaly = pts[2]
    for a, b in edges:
        if b == 2 or a == 2:
            pass
    for a, b in edges:
        if 2 in (a, b):
            p1, p2 = pts[a], pts[b]
            draw.line([p1, p2], fill=(255, 92, 92, 235), width=w)

    node_r = S // 22
    for i, p in enumerate(pts):
        if i == 2:
            continue
        draw.ellipse([p[0] - node_r, p[1] - node_r, p[0] + node_r, p[1] + node_r],
                     fill=(235, 245, 255, 255))

    # Точка-аномалия: свечение + кольцо + ядро
    glow = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    g = ImageDraw.Draw(glow)
    gr = S * 0.17
    g.ellipse([anomaly[0] - gr, anomaly[1] - gr, anomaly[0] + gr, anomaly[1] + gr],
              fill=(255, 60, 60, 150))
    glow = glow.filter(ImageFilter.GaussianBlur(S // 30))
    img = Image.alpha_composite(img, glow)
    draw = ImageDraw.Draw(img)

    rr = S * 0.105
    draw.ellipse([anomaly[0] - rr, anomaly[1] - rr, anomaly[0] + rr, anomaly[1] + rr],
                 outline=(255, 210, 210, 255), width=max(3, S // 80))
    core = S * 0.072
    draw.ellipse([anomaly[0] - core, anomaly[1] - core, anomaly[0] + core, anomaly[1] + core],
                 fill=(230, 45, 45, 255), outline=(255, 255, 255, 220), width=max(2, S // 130))

    out_dir = os.path.join(os.path.dirname(__file__), "..", "resources")
    os.makedirs(out_dir, exist_ok=True)
    ico_path = os.path.join(out_dir, "app.ico")

    sizes = [(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)]
    img.save(ico_path, format="ICO", sizes=sizes, append_images=[])
    img.save(os.path.join(out_dir, "app_256.png"))
    print("saved:", os.path.abspath(ico_path))


if __name__ == "__main__":
    main()
