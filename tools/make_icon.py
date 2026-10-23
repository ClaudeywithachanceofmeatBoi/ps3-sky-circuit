#!/usr/bin/env python3
"""Write a 320x176 ICON0.PNG (XMB icon) with no third-party dependencies."""
import struct
import sys
import zlib

W, H = 320, 176

def chunk(tag, data):
    return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

def pixel(x, y):
    sky = (0x70, 0xC5, 0xCE)
    # road stripe
    if 70 <= y <= 106:
        if abs(x - 160) < 46:
            return (0x2C, 0x3E, 0x50, 255)
        return sky + (255,)
    if 86 <= y <= 90 and abs((x // 14) % 2) == 0 and abs(x - 160) < 40:
        return (0xF1, 0xC4, 0x0F, 255)
    # car
    if 148 <= x <= 172 and 78 <= y <= 118:
        return (0x1B, 0x36, 0x5D, 255)
    if 152 <= x <= 168 and 90 <= y <= 104:
        return (0x11, 0x22, 0x33, 255)
    # clouds
    for cx, cy, r in ((60, 36, 18), (78, 40, 14), (250, 48, 20), (40, 140, 16)):
        if (x - cx) * (x - cx) + (y - cy) * (y - cy) < r * r:
            return (255, 255, 255, 230)
    return sky + (255,)

def main():
    out = sys.argv[1] if len(sys.argv) > 1 else "ICON0.PNG"
    raw = bytearray()
    for y in range(H):
        raw.append(0)
        for x in range(W):
            raw.extend(pixel(x, y))
    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", W, H, 8, 6, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(bytes(raw), 9))
    png += chunk(b"IEND", b"")
    with open(out, "wb") as f:
        f.write(png)

if __name__ == "__main__":
    main()
