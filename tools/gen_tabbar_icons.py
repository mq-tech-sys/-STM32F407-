#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
生成小程序 tabBar 图标（零依赖，只用标准库）。

微信 tabBar 的 iconPath 只认 png/jpg，不认 svg，也不能用 data URI，
所以这里直接按像素画 + 自己压 PNG。

用法：  python tools/gen_tabbar_icons.py
输出：  mini-program/images/tabbar/*.png   （81x81，透明底）
"""

import math
import os
import struct
import zlib

SIZE = 81          # 微信推荐 81x81
SS = 4             # 每个像素 4x4 超采样，用来做抗锯齿

NORMAL = (0x8A, 0x94, 0xA6)   # 未选中：灰
ACTIVE = (0x25, 0x63, 0xEB)   # 选中：主色蓝


# ------------------------------------------------------------------ 形状判定

def _inside_round_rect(x, y, cx, cy, hw, hh, r):
    """圆角矩形（中心 cx,cy；半宽 hw；半高 hh；圆角 r）"""
    dx = abs(x - cx) - (hw - r)
    dy = abs(y - cy) - (hh - r)
    dx = dx if dx > 0 else 0.0
    dy = dy if dy > 0 else 0.0
    if dx * dx + dy * dy > r * r:
        return False
    return abs(x - cx) <= hw and abs(y - cy) <= hh


def _inside_annulus(x, y, cx, cy, r_out, r_in):
    d = math.hypot(x - cx, y - cy)
    return r_in <= d <= r_out


def _inside_rot_rect(x, y, cx, cy, hw, hh, deg):
    a = math.radians(deg)
    ca, sa = math.cos(a), math.sin(a)
    px, py = x - cx, y - cy
    rx = px * ca + py * sa
    ry = -px * sa + py * ca
    return abs(rx) <= hw and abs(ry) <= hh


def _inside_disc(x, y, cx, cy, r):
    return math.hypot(x - cx, y - cy) <= r


# ------------------------------------------------------------------ 三个图标

def icon_lock(x, y):
    """控制页：挂锁。锁体圆角矩形 + 半圆锁梁，锁体上挖一个钥匙孔"""
    body = _inside_round_rect(x, y, 0.50, 0.66, 0.30, 0.24, 0.08)
    shackle_out = _inside_annulus(x, y, 0.50, 0.44, 0.215, 0.135)
    # 只要上半圈
    shackle = shackle_out and y <= 0.46
    hole = _inside_disc(x, y, 0.50, 0.63, 0.045) or _inside_round_rect(x, y, 0.50, 0.71, 0.022, 0.06, 0.0)
    return (body or shackle) and not hole


def icon_grid(x, y):
    """功能页：2x2 圆角方块"""
    r = 0.055
    w, h = 0.135, 0.135
    pts = [(0.325, 0.325), (0.675, 0.325), (0.325, 0.675), (0.675, 0.675)]
    for cx, cy in pts:
        if _inside_round_rect(x, y, cx, cy, w, h, r):
            return True
    return False


def icon_gear(x, y):
    """设置页：齿轮。圆环 + 8 个齿 + 中心孔"""
    ring = _inside_annulus(x, y, 0.5, 0.5, 0.245, 0.105)
    teeth = False
    for k in range(8):
        a = k * 45.0
        rad = math.radians(a)
        tx = 0.5 + 0.295 * math.cos(rad)
        ty = 0.5 + 0.295 * math.sin(rad)
        if _inside_rot_rect(x, y, tx, ty, 0.062, 0.045, a):
            teeth = True
            break
    return ring or teeth


ICONS = {
    "control": icon_lock,
    "features": icon_grid,
    "settings": icon_gear,
}


# ------------------------------------------------------------------ 渲染 PNG

def render(fn, rgb):
    """按超采样渲染成 RGBA 像素表"""
    inv = 1.0 / (SIZE * SS)
    rows = []
    for py in range(SIZE):
        row = bytearray()
        for px in range(SIZE):
            hit = 0
            for sy in range(SS):
                yy = (py * SS + sy + 0.5) * inv
                for sx in range(SS):
                    xx = (px * SS + sx + 0.5) * inv
                    if fn(xx, yy):
                        hit += 1
            a = int(round(255 * hit / (SS * SS)))
            row += bytes((rgb[0], rgb[1], rgb[2], a))
        rows.append(bytes(row))
    return rows


def write_png(path, rows):
    w, h = SIZE, SIZE
    raw = bytearray()
    for r in rows:
        raw.append(0)          # filter type 0
        raw += r
    comp = zlib.compress(bytes(raw), 9)

    def chunk(tag, data):
        return (struct.pack(">I", len(data)) + tag + data
                + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF))

    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0))
    png += chunk(b"IDAT", comp)
    png += chunk(b"IEND", b"")
    with open(path, "wb") as f:
        f.write(png)
    return len(png)


def main():
    here = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    out = os.path.join(here, "mini-program", "images", "tabbar")
    os.makedirs(out, exist_ok=True)

    for name, fn in ICONS.items():
        for suffix, color in (("", NORMAL), ("-on", ACTIVE)):
            path = os.path.join(out, "tab-%s%s.png" % (name, suffix))
            size = write_png(path, render(fn, color))
            print("%-22s %5d bytes" % (os.path.basename(path), size))


if __name__ == "__main__":
    main()
