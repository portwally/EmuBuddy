#!/usr/bin/env python3
"""Pictures for the Apple IIgs: scale a modern picture down, reduce it to what Super Hi-Res can show,
and pack it as an Apple Preferred Format file (the IIgs's own packed picture format, type $C0/$0002).

Two kinds:
  "640"  640 mode, the 16 dithered colours of the standard desktop palette: for pictures inside a page.
  "320"  320 mode, 16 colours chosen for the picture: for the full-screen view.

    python3 gspic.py convert photo.jpg out.apf [320|640] [max width]
    python3 gspic.py view out.apf preview.png        what the IIgs will show, as a PNG

Decoding uses Pillow if it is installed, else the macOS tool "sips". Everything else is plain Python."""
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import zlib

# pixel shape on a 4:3 screen (width : height)
ASPECT = {"320": (4 / 320) / (3 / 200), "640": (4 / 640) / (3 / 200)}
# the standard 640 mode palette: pixels 0 and 2 of a byte use black/red/green/white,
# pixels 1 and 3 use black/blue/yellow/white
PAL640 = [0x000, 0xF00, 0x0F0, 0xFFF, 0x000, 0x00F, 0xFF0, 0xFFF] * 2
MAX_H = {"320": 200, "640": 180}


# ---- decoding and scaling (the only part that needs help from outside) ----

def target_size(w, h, kind, max_w):
    """Size on the IIgs for a picture of w x h square pixels."""
    if kind == "320":
        tw = min(w, 320, max_w)
    else:
        tw = min(w, max_w, 640)
    tw = max(4, tw - tw % 4)
    th = max(1, round(tw * h / w * ASPECT[kind]))
    if th > MAX_H[kind]:
        tw = max(4, int(tw * MAX_H[kind] / th))
        tw -= tw % 4
        th = max(1, round(tw * h / w * ASPECT[kind]))
    return tw, th


def load_rgb(path, kind, max_w):
    """(width, height, list of rows of (r, g, b)) scaled for the IIgs."""
    try:
        from PIL import Image                   # used when available (NAS, Raspberry Pi)
        im = Image.open(path)
        im.seek(0)
        im = im.convert("RGBA")
        bg = Image.new("RGBA", im.size, (255, 255, 255, 255))
        im = Image.alpha_composite(bg, im).convert("RGB")
        tw, th = target_size(im.width, im.height, kind, max_w)
        im = im.resize((tw, th), Image.LANCZOS)
        px = list(im.getdata())
        return tw, th, [px[y * tw:(y + 1) * tw] for y in range(th)]
    except ImportError:
        pass
    if not shutil.which("sips"):
        raise RuntimeError("no picture decoder: install Pillow (pip install pillow)")
    out = subprocess.run(["sips", "-g", "pixelWidth", "-g", "pixelHeight", path], capture_output=True, text=True)
    dims = {}
    for line in out.stdout.splitlines():
        parts = line.split(":")
        if len(parts) == 2 and parts[0].strip() in ("pixelWidth", "pixelHeight"):
            dims[parts[0].strip()] = int(parts[1])
    if len(dims) != 2:
        raise RuntimeError("not a picture this computer can read")
    tw, th = target_size(dims["pixelWidth"], dims["pixelHeight"], kind, max_w)
    with tempfile.TemporaryDirectory() as tmp:
        bmp = os.path.join(tmp, "p.bmp")
        r = subprocess.run(["sips", "-s", "format", "bmp", "-z", str(th), str(tw), path, "--out", bmp],
                           capture_output=True)
        if r.returncode or not os.path.exists(bmp):
            raise RuntimeError("could not convert the picture")
        return read_bmp(open(bmp, "rb").read())


def read_bmp(d):
    if d[:2] != b"BM":
        raise RuntimeError("not a BMP")
    off, = struct.unpack_from("<I", d, 10)
    w, h, _planes, bpp, comp = struct.unpack_from("<iiHHI", d, 18)
    if bpp not in (24, 32) or comp not in (0, 3):
        raise RuntimeError("unexpected BMP kind (%d bits)" % bpp)
    flip = h > 0
    h = abs(h)
    n = bpp // 8
    stride = (w * n + 3) & ~3
    rows = []
    for y in range(h):
        base = off + (h - 1 - y if flip else y) * stride
        row = []
        for x in range(w):
            b, g, r = d[base + x * n], d[base + x * n + 1], d[base + x * n + 2]
            if n == 4:                          # transparent parts become white
                a = d[base + x * n + 3]
                if a < 255:
                    r, g, b = (r * a + 255 * (255 - a)) // 255, (g * a + 255 * (255 - a)) // 255, (b * a + 255 * (255 - a)) // 255
            row.append((r, g, b))
        rows.append(row)
    return w, h, rows


# ---- colour reduction ----

def rgb12(c):
    return ((c >> 8) * 17, (c >> 4 & 15) * 17, (c & 15) * 17)


def median_cut(rows, n):
    """n colours (as 12-bit IIgs values) that suit the picture."""
    counts = {}
    for row in rows:
        for r, g, b in row:
            k = (r >> 4, g >> 4, b >> 4)
            counts[k] = counts.get(k, 0) + 1
    boxes = [list(counts.items())]
    while len(boxes) < n:
        best, span, axis = None, 0, 0
        for i, box in enumerate(boxes):
            if len(box) < 2:
                continue
            for a in range(3):
                lo = min(c[0][a] for c in box)
                hi = max(c[0][a] for c in box)
                weight = (hi - lo) * (sum(c[1] for c in box) ** 0.5)
                if weight > span:
                    best, span, axis = i, weight, a
        if best is None:
            break
        box = sorted(boxes.pop(best), key=lambda c: c[0][axis])
        total, run, cut = sum(c[1] for c in box), 0, 1
        for i, c in enumerate(box[:-1]):
            run += c[1]
            if run >= total / 2:
                cut = i + 1
                break
        boxes += [box[:cut], box[cut:]]
    pal = []
    for box in boxes:
        t = sum(c[1] for c in box)
        r, g, b = (round(sum(c[0][a] * c[1] for c in box) / t) for a in range(3))
        pal.append(r << 8 | g << 4 | b)
    pal += [0] * (n - len(pal))
    return sorted(pal, key=lambda c: sum(rgb12(c)))


def dither(rows, w, h, choices):
    """Floyd-Steinberg. choices(x) gives the list of (index, (r, g, b)) allowed in column x."""
    out = []
    err = [[0.0, 0.0, 0.0] for _ in range(w + 2)]
    for y in range(h):
        nxt = [[0.0, 0.0, 0.0] for _ in range(w + 2)]
        line = []
        row = rows[y]
        for x in range(w):
            e = err[x + 1]
            r = min(255.0, max(0.0, row[x][0] + e[0]))
            g = min(255.0, max(0.0, row[x][1] + e[1]))
            b = min(255.0, max(0.0, row[x][2] + e[2]))
            best, bd, bc = 0, 1e18, (0, 0, 0)
            for idx, c in choices(x):
                d = (r - c[0]) ** 2 * 3 + (g - c[1]) ** 2 * 4 + (b - c[2]) ** 2 * 3
                if d < bd:
                    best, bd, bc = idx, d, c
            line.append(best)
            dr, dg, db = r - bc[0], g - bc[1], b - bc[2]
            for ex, k in ((err[x + 2], 7 / 16), (nxt[x], 3 / 16), (nxt[x + 1], 5 / 16), (nxt[x + 2], 1 / 16)):
                ex[0] += dr * k
                ex[1] += dg * k
                ex[2] += db * k
        out.append(line)
        err = nxt
    return out


# ---- PackBytes and Apple Preferred Format ----

def pack_bytes(data):
    out = bytearray()
    i, n = 0, len(data)
    lit = bytearray()

    def flush():
        nonlocal lit
        while lit:
            chunk = lit[:64]
            out.append(len(chunk) - 1)
            out.extend(chunk)
            lit = lit[64:]

    while i < n:
        j = i
        while j < n and data[j] == data[i] and j - i < 64:
            j += 1
        run = j - i
        if run >= 3:
            flush()
            out.append(0x40 | (run - 1))
            out.append(data[i])
            i = j
        else:
            lit.append(data[i])
            i += 1
    flush()
    return bytes(out)


def unpack_bytes(data, want):
    out = bytearray()
    i = 0
    while i < len(data) and len(out) < want:
        f = data[i]
        cnt = (f & 0x3F) + 1
        kind = f >> 6
        i += 1
        if kind == 0:
            out += data[i:i + cnt]
            i += cnt
        elif kind == 1:
            out += bytes([data[i]]) * cnt
            i += 1
        elif kind == 2:
            out += data[i:i + 4] * cnt
            i += 4
        else:
            out += bytes([data[i]]) * (cnt * 4)
            i += 1
    return bytes(out[:want])


def to_apf(kind, w, h, palette, lines):
    """lines: rows of colour indexes. Returns the bytes of an Apple Preferred Format file."""
    mode = 0x80 if kind == "640" else 0x00
    packed = []
    for line in lines:
        if kind == "640":
            raw = bytes(line[x] << 6 | line[x + 1] << 4 | line[x + 2] << 2 | line[x + 3] for x in range(0, w, 4))
        else:
            raw = bytes(line[x] << 4 | line[x + 1] for x in range(0, w, 2))
        packed.append(pack_bytes(raw))
    body = struct.pack("<HHH", mode, w, 1)
    body += b"".join(struct.pack("<H", c) for c in palette)
    body += struct.pack("<H", h)
    body += b"".join(struct.pack("<HH", len(p), mode) for p in packed)
    body += b"".join(packed)
    name = b"\x04MAIN"
    return struct.pack("<I", 4 + len(name) + len(body)) + name + body


def from_apf(d):
    """(kind, width, height, palette, rows of colour indexes)"""
    pos = 0
    while pos < len(d):
        length, = struct.unpack_from("<I", d, pos)
        nlen = d[pos + 4]
        if d[pos + 5:pos + 5 + nlen] == b"MAIN":
            break
        pos += length
    else:
        raise RuntimeError("no MAIN block")
    p = pos + 5 + nlen
    mode, w, ntab = struct.unpack_from("<HHH", d, p)
    p += 6
    palette = list(struct.unpack_from("<16H", d, p))
    p += 32 * ntab
    h, = struct.unpack_from("<H", d, p)
    p += 2
    sizes = [struct.unpack_from("<HH", d, p + 4 * i)[0] for i in range(h)]
    p += 4 * h
    kind = "640" if mode & 0x80 else "320"
    rows = []
    for size in sizes:
        raw = unpack_bytes(d[p:p + size], w // (4 if kind == "640" else 2))
        p += size
        if kind == "640":
            rows.append([(b >> s) & 3 for b in raw for s in (6, 4, 2, 0)])
        else:
            rows.append([(b >> s) & 15 for b in raw for s in (4, 0)])
    return kind, w, h, palette, rows


# ---- the whole job ----

def convert_rows(kind, w, h, rows):
    if kind == "640":
        # A pair of neighbouring pixels (one from each colour set) blends into one of 16 "dithered
        # colours". Pictures are reduced pair by pair, which gives far truer colours than pixel by pixel.
        combos = []
        for i in range(4):
            for j in range(4):
                a, b = rgb12(PAL640[i]), rgb12(PAL640[4 + j])
                combos.append((i << 2 | j, ((a[0] + b[0]) / 2, (a[1] + b[1]) / 2, (a[2] + b[2]) / 2)))
        half = [[tuple((row[x][k] + row[x + 1][k]) / 2 for k in range(3)) for x in range(0, w, 2)] for row in rows]
        pairs = dither(half, w // 2, h, lambda x: combos)
        lines = [[v for p in line for v in (p >> 2, p & 3)] for line in pairs]
        return to_apf(kind, w, h, PAL640, lines)
    pal = median_cut(rows, 16)
    table = [(i, rgb12(c)) for i, c in enumerate(pal)]
    lines = dither(rows, w, h, lambda x: table)
    return to_apf(kind, w, h, pal, lines)


def convert_file(path, kind="640", max_w=600):
    w, h, rows = load_rgb(path, kind, max_w)
    return convert_rows(kind, w, h, rows), w, h


def preview_png(apf):
    """What the IIgs screen shows, as a PNG with square pixels (640 wide scale)."""
    kind, w, h, pal, rows = from_apf(apf)
    xs = 1 if kind == "640" else 2
    out_w = w * xs
    out_h = round(h * 2.4)
    raw = bytearray()
    for y in range(out_h):
        row = rows[min(h - 1, int(y / 2.4))]
        raw.append(0)
        if kind == "640":                       # a monitor blends each pair of pixels; show that
            for x in range(0, w, 2):
                a, b = rgb12(pal[row[x]]), rgb12(pal[4 + row[x + 1]])
                raw += bytes(((a[0] + b[0]) // 2, (a[1] + b[1]) // 2, (a[2] + b[2]) // 2)) * 2
        else:
            for x in range(w):
                raw += bytes(rgb12(pal[row[x]])) * xs

    def chunk(tag, data):
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", out_w, out_h, 8, 2, 0, 0, 0)) +
            chunk(b"IDAT", zlib.compress(bytes(raw), 6)) + chunk(b"IEND", b""))


if __name__ == "__main__":
    if len(sys.argv) >= 4 and sys.argv[1] == "convert":
        kind = sys.argv[4] if len(sys.argv) > 4 else "640"
        data, w, h = convert_file(sys.argv[2], kind, int(sys.argv[5]) if len(sys.argv) > 5 else 600)
        open(sys.argv[3], "wb").write(data)
        print("%s: %d x %d in %s mode, %d bytes" % (sys.argv[3], w, h, kind, len(data)))
    elif len(sys.argv) == 4 and sys.argv[1] == "view":
        open(sys.argv[3], "wb").write(preview_png(open(sys.argv[2], "rb").read()))
    else:
        print(__doc__)
