#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
OTA 固件打包工具（纯标准库，Python 3.8+）

把 Keil 生成的 app.bin 打成带头的 .ota 包：
    ┌───────────────────────── 20 字节头 ─────────────────────────┐
    │ magic "SLFW" │ ver(2) │ flags(2) │ size(4) │ crc32(4) │ hdr_crc16(2) │ rsv(2) │
    └──────────────────────────────────────────────────────────────┘
    + 原始 bin 数据

用法：
    python tools\\ota_pack.py OBJ\\app.bin -v 1.3
    python tools\\ota_pack.py OBJ\\app.bin -v 1.3 -o OBJ\\app_v1.3.ota
    python tools\\ota_pack.py OBJ\\app_v1.3.ota --verify

版本号写法：1.3 -> 0x0103，1.10 -> 0x010A，直接写 0x0103 也可以。
"""

import argparse
import struct
import sys
import zlib
from pathlib import Path

MAGIC = b"SLFW"
HDR_LEN = 20
HDR_FMT = ">4sHHIIHH"          # magic, ver, flags, size, crc32, hdr_crc16, reserved
IMAGE_MAX = 128 * 1024          # 下载区 128KB

# 与固件用同一份分区表（docs/OTA升级方案.md 第三节）
LAYOUT = {
    "boot":   (0x08000000, 32 * 1024),
    "meta":   (0x08008000, 16 * 1024),
    "params": (0x0800C000, 16 * 1024),   # 密码/卡片，OTA 不碰
    "run":    (0x08010000, 192 * 1024),
    "dl":     (0x08040000, 128 * 1024),
    "bak":    (0x08060000, 128 * 1024),
}


def crc16_ccitt(data: bytes, init: int = 0xFFFF) -> int:
    """CRC16-CCITT（poly 0x1021，不反射），与固件侧实现保持一致。"""
    crc = init
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


def parse_version(text: str) -> tuple[int, str]:
    """'1.3' -> (0x0103, '1.3')；'0x0103' -> (0x0103, '1.3')。"""
    t = text.strip()
    if t.lower().startswith("0x"):
        val = int(t, 16)
        if not 0 <= val <= 0xFFFF:
            raise ValueError("版本号超出 16 位")
        return val, f"{val >> 8}.{val & 0xFF}"

    if "." in t:
        major, minor = t.split(".", 1)
    else:
        major, minor = t, "0"
    major, minor = int(major), int(minor)
    if not (0 <= major <= 255 and 0 <= minor <= 255):
        raise ValueError("版本号每段必须在 0~255 之间")
    return (major << 8) | minor, f"{major}.{minor}"


def looks_like_hex(data: bytes) -> bool:
    """Intel HEX 每行都以 ':' 开头，误传 hex 是最常见的错误。"""
    head = data[:64].lstrip()
    return head.startswith(b":")


def build_header(version: int, size: int, crc32: int, flags: int = 0) -> bytes:
    body = struct.pack(">4sHHII", MAGIC, version, flags, size, crc32)
    return body + struct.pack(">HH", crc16_ccitt(body), 0xFFFF)


def parse_header(blob: bytes) -> dict:
    if len(blob) < HDR_LEN:
        raise ValueError("文件太短，不是合法的 .ota")
    magic, ver, flags, size, crc32, hdr_crc, _rsv = struct.unpack(HDR_FMT, blob[:HDR_LEN])
    if magic != MAGIC:
        raise ValueError(f"magic 不对：{magic!r}，期望 {MAGIC!r}")
    want = crc16_ccitt(blob[:16])
    if want != hdr_crc:
        raise ValueError(f"头校验失败：算出 0x{want:04X}，文件里 0x{hdr_crc:04X}")
    return {
        "version": ver,
        "flags": flags,
        "size": size,
        "crc32": crc32,
        "payload": blob[HDR_LEN:],
    }


def do_pack(args) -> int:
    src = Path(args.input)
    if not src.is_file():
        print(f"找不到文件：{src}")
        return 2

    data = src.read_bytes()
    if looks_like_hex(data):
        print("这个文件看着是 Intel HEX（每行以 ':' 开头），不是 bin。")
        print("请在 Keil 里用 fromelf --bin 生成 bin：")
        print('  fromelf --bin --output OBJ\\app.bin OBJ\\demo.axf')
        return 2

    if not data:
        print("文件是空的。")
        return 2

    if len(data) > IMAGE_MAX:
        print(f"镜像 {len(data)} 字节，超过下载区上限 {IMAGE_MAX} 字节（128 KB）。")
        print("先查链接配置：IROM1 应为 0x08010000 / 0x30000，且不能把 bootloader 一起编进来。")
        return 2

    try:
        version, ver_text = parse_version(args.version)
    except ValueError as e:
        print(f"版本号非法：{e}")
        return 2

    crc = zlib.crc32(data) & 0xFFFFFFFF
    header = build_header(version, len(data), crc)
    out = Path(args.output) if args.output else src.with_suffix(".ota")
    out.write_bytes(header + data)

    percent = len(data) * 100 // IMAGE_MAX
    print(f"镜像      {src}")
    print(f"大小      {len(data):,} 字节 ({len(data) / 1024:.1f} KB)")
    print(f"版本      v{ver_text} (0x{version:04X})")
    print(f"CRC32     0x{crc:08X}")
    print(f"下载区    {IMAGE_MAX:,} 字节，余量 {IMAGE_MAX - len(data):,} 字节（占用 {percent}%）")
    print(f"输出      {out} ({out.stat().st_size:,} 字节)")

    if percent > 80:
        print("注意：已用超过 80%，再长就该考虑换更大的下载区了。")
    return 0


def do_verify(args) -> int:
    src = Path(args.input)
    if not src.is_file():
        print(f"找不到文件：{src}")
        return 2

    blob = src.read_bytes()
    try:
        info = parse_header(blob)
    except ValueError as e:
        print(f"校验失败：{e}")
        return 1

    payload = info["payload"]
    if len(payload) != info["size"]:
        print(f"长度不符：头里写 {info['size']}，实际 {len(payload)}")
        return 1
    crc = zlib.crc32(payload) & 0xFFFFFFFF
    if crc != info["crc32"]:
        print(f"CRC32 不符：头里 0x{info['crc32']:08X}，实算 0x{crc:08X}")
        return 1

    sp = struct.unpack("<I", payload[0:4])[0] if len(payload) >= 8 else 0
    pc = struct.unpack("<I", payload[4:8])[0] if len(payload) >= 8 else 0
    sp_ok = 0x20000000 <= sp <= 0x20020000
    pc_ok = LAYOUT["run"][0] <= pc < LAYOUT["run"][0] + LAYOUT["run"][1]

    print(f"文件      {src}")
    print(f"版本      v{info['version'] >> 8}.{info['version'] & 0xFF}")
    print(f"大小      {len(payload):,} 字节")
    print(f"CRC32     0x{crc:08X}  (一致)")
    print(f"栈顶     0x{sp:08X}  {'ok' if sp_ok else '异常：不在 SRAM 范围'}")
    print(f"复位向量 0x{pc:08X}  {'ok' if pc_ok else '异常：不在运行区 0x08010000~0x0803FFFF'}")

    if not (sp_ok and pc_ok):
        print("\n向量表不合法，这多半不是 App 的 bin：")
        print("  - 链接地址还是 0x08000000（没改 IROM1）")
        print("  - 或者误把 bootloader 的 bin 传了上来")
        return 1
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description="STM32 门锁 OTA 固件打包 / 校验")
    ap.add_argument("input", help="app.bin，或 --verify 时的 .ota 文件")
    ap.add_argument("-v", "--version", default="0.1", help="版本号，如 1.3（默认 0.1）")
    ap.add_argument("-o", "--output", help="输出文件（默认同名 .ota）")
    ap.add_argument("--verify", action="store_true", help="校验已有 .ota 文件")
    args = ap.parse_args()

    if args.verify:
        return do_verify(args)
    return do_pack(args)


if __name__ == "__main__":
    sys.exit(main())
