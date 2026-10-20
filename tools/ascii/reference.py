# cpu reference of the ascii cells (shaders/wc_ascii_cells.glsl), written independently of the C tables: rebuilds the
# sampling weights, glyph vectors and fill ramp from Monocraft's characters.json, re-runs every stroke, cloud and fill
# cell on the dumped ldr target and compares the glyph the gpu picked
# usage: python -I tools/ascii/reference.py <dump prefix, e.g. ascii_1> cols=<cols> rows=<rows>
import json
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
CHARS = os.path.join(ROOT, "tools", "ascii", "monocraft", "characters.json")
HEADER = os.path.join(ROOT, "src", "webcraft", "wc_ascii.h")
RENDER = os.path.join(ROOT, "src", "webcraft", "wc_render.c")
CLASSIC = "$@B%8&WM#*oahkbdpqwmZO0QLCJUYXzcvunxrjft/\|()1{}[]?-_+~<>i!lI;:,\"^`'. "

CELL_W, CELL_H = 6, 9
SITES = [(30, 23), (70, 18), (30, 50), (70, 50), (30, 82), (70, 77),
         (7, -21), (93, -21), (-25, 7), (125, 7), (-25, 50), (125, 50), (-25, 93), (125, 93), (7, 121), (93, 121)]
# Harri: outer circles affecting each inner circle
AFFECTS = [[0, 1, 2, 4], [0, 1, 3, 5], [2, 4, 6], [3, 5, 7], [4, 6, 8, 9], [5, 7, 8, 9]]
UNIT, RADIUS, SS = 3200, 6075, 8


def vocab_from_header(name="WC_ASCII_VOCAB"):
    text = open(HEADER, encoding="utf-8").read()
    m = re.search(r'#define ' + name + r'\s*(?:\\\s*\n\s*)?"((?:[^"\\]|\\.)*)"', text)
    return bytes(m.group(1), "utf-8").decode("unicode_escape")


def render_define(name):
    m = re.search(r'#define ' + name + r'\s+([0-9.]+)', open(RENDER, encoding="utf-8").read())
    return float(m.group(1))


def fill_ramp(cells):
    """(ramp position -> code, code -> position, mean luma -> position) for the fill vocabulary"""
    vocab = [ord(c) - 32 for c in vocab_from_header("WC_ASCII_FILL")]

    def key(g):
        lit = sum(map(sum, cells[g]))
        light = 0
        for i, c in enumerate(CLASSIC):
            if ord(c) - 32 == g:
                light = len(CLASSIC) - i
        return (lit, 128 - light, g)

    ramp = sorted(vocab, key=key)
    rank = {g: i for i, g in enumerate(ramp)}
    lo, hi, curve = render_define("WC_FILL_LO"), render_define("WC_FILL_HI"), render_define("WC_FILL_CURVE")
    # a surface is never blank: what would map to the lightest step takes the next one (wc_ascii_build_ramp)
    lut = [max(1, int(min(1.0, max(0.0, (l - lo) / (hi - lo))) ** curve * (len(ramp) - 1) + 0.5)) for l in range(256)]
    return ramp, rank, lut


def site_entries():
    out = []
    for sx, sy in SITES:
        cx, cy = sx * CELL_W * UNIT // 100, sy * CELL_H * UNIT // 100
        ents = []
        for y in range(-20, 30):
            for x in range(-20, 30):
                w = 0
                for j in range(SS):
                    for i in range(SS):
                        dx = (x * SS * 2 + i * 2 + 1) * UNIT // (SS * 2) - cx
                        dy = (y * SS * 2 + j * 2 + 1) * UNIT // (SS * 2) - cy
                        if dx * dx + dy * dy <= RADIUS * RADIUS:
                            w += 1
                if w:
                    ents.append((x, y, w))
        out.append(ents)
    return out


def glyph_cells():
    data = json.load(open(CHARS, encoding="utf-8"))
    by = {c["codepoint"]: c for c in data if "codepoint" in c}
    cells = {}
    for cp in range(32, 127):
        cell = [[0] * CELL_W for _ in range(CELL_H)]
        for r, line in enumerate(by[cp]["pixels"]):
            for c, v in enumerate(line):
                cell[1 + r][c] = 1 if v else 0
        cells[cp - 32] = cell
    return cells


def glyph_vectors(entries, cells, vocab, normalise=False):
    """raw ink coverage of each inner circle (0..65535); normalise rescales each circle to the vocabulary's max"""
    raw = []
    for ch in vocab:
        cell = cells[ord(ch) - 32]
        vec = []
        for s in range(6):
            tot = sum(w for _, _, w in entries[s])
            ink = sum(w for x, y, w in entries[s] if 0 <= x < CELL_W and 0 <= y < CELL_H and cell[y][x])
            vec.append(ink * 65535 // tot)
        raw.append(vec)
    if not normalise:
        return raw
    maxc = [max(v[s] for v in raw) for s in range(6)]
    return [[(v[s] * 65535 // maxc[s]) if maxc[s] else 0 for s in range(6)] for v in raw]


def contrast(v, m, n):
    if m == 0:
        return 0
    t = (v << 15) // m
    r = t
    for _ in range(n - 1):
        r = (r * t) >> 15
    return (r * m) >> 15


def match_cell(lum, ox, oy, entries, gv, codes, pg):
    """glyph code the matcher picks for the cell at (ox, oy); lum(x, y) clamps; pg = previous glyph code (or 255)"""
    vals = []
    for s in range(16):
        tot = acc = 0
        for x, y, w in entries[s]:
            acc += w * lum(ox + x, oy + y)
            tot += w
        vals.append(acc * 257 // tot)
    v = []
    for i in range(6):
        m = max([vals[i]] + [vals[6 + j] for j in AFFECTS[i]])
        v.append(contrast(vals[i], m, 7))
    vmax = max(v)
    v = [contrast(x, vmax, 3) >> 4 for x in v]
    best, best_d, prev_slot, prev_d = 0, 0xFFFFFFFF, None, 0
    for g, vec in enumerate(gv):
        d = sum((v[i] - (vec[i] >> 4)) ** 2 for i in range(6))
        if d < best_d:
            best, best_d = g, d
        if codes[g] == pg:
            prev_slot, prev_d = g, d
    if prev_slot is not None and prev_d * 10 <= best_d * 11:
        best = prev_slot
    return codes[best]


def load(path, w, h):
    data = open(path, "rb").read()
    assert len(data) == w * h * 4, (path, len(data), w * h * 4)
    return data


KIND_NAMES = ["edge", "fill", "sky", "water"]
FLAG_HAND, FLAG_CLOUD, FLAG_OVERLAY = 4, 8, 16


def main():
    prefix = sys.argv[1]
    kv = dict(a.split("=") for a in sys.argv[2:])
    cols, rows = int(kv["cols"]), int(kv["rows"])
    W, H = cols * CELL_W, rows * CELL_H
    ldr = load(prefix + "_ldr.rgba", W, H)
    glyph = load(prefix + "_glyph.rgba", cols, rows)
    back = load(prefix + "_back.rgba", cols, rows)
    prev = load(prefix + "_prev.rgba", cols, rows)
    entries = site_entries()
    cells = glyph_cells()
    edge_vocab, cloud_vocab = vocab_from_header("WC_ASCII_VOCAB"), vocab_from_header("WC_ASCII_CLOUD")
    edge = (glyph_vectors(entries, cells, edge_vocab), [ord(c) - 32 for c in edge_vocab])
    cloud = (glyph_vectors(entries, cells, cloud_vocab), [ord(c) - 32 for c in cloud_vocab])
    raw = [ldr[i * 4 + 3] for i in range(W * H)]
    # the matcher image: outlines are 255 for the edge matcher; cloud cover 0..127 is doubled for the cloud matcher
    edge_img = [255 if a == 255 else 0 for a in raw]
    cloud_img = [a * 2 if a < 128 else 0 for a in raw]

    def lum_of(img):
        return lambda x, y: img[min(max(y, 0), H - 1) * W + min(max(x, 0), W - 1)]

    edge_lum, cloud_lum = lum_of(edge_img), lum_of(cloud_img)
    ramp, rank, lut = fill_ramp(cells)
    hyst = int(render_define("WC_RAMP_HYSTERESIS"))

    def luma8(i):
        return (54 * ldr[i * 4] + 183 * ldr[i * 4 + 1] + 19 * ldr[i * 4 + 2] + 128) >> 8

    def fill_glyph(cx, cy, pg, hand=False):
        total = sum(luma8((cy * CELL_H + y) * W + cx * CELL_W + x) for y in range(CELL_H) for x in range(CELL_W))
        l = (total + 27) // 54
        # the held item's luma is pushed into the dense half of the ramp (hand_luma in wc_ascii_cells.glsl)
        p = lut[96 + l * 5 // 8 if hand else l]
        if pg in rank and rank[pg] != 0 and abs(rank[pg] - p) <= hyst:
            return pg
        return ramp[p]

    counts = {"edge": [0, 0], "cloud": [0, 0], "fill": [0, 0]}
    skipped = overlays = 0
    kind_cells = [0] * 4
    kind_changes = [0] * 4
    shown = 0
    for cy in range(rows):
        for cx in range(cols):
            ci = (cy * cols + cx) * 4
            # flags: kind in bits 0-1 (edge stroke, fill, sky, water fill); held item 4; cloud 8; overlay 16 (weather, lightning or bubbles replaced the glyph); selected 32
            flags = back[ci + 3]
            kind = flags & 3
            kind_cells[kind] += 1
            if glyph[ci + 3] != prev[ci + 3]:
                kind_changes[kind] += 1
            if flags & FLAG_OVERLAY:
                overlays += 1
                continue
            if kind == 0:
                which = "edge"
                want = match_cell(edge_lum, cx * CELL_W, cy * CELL_H, entries, edge[0], edge[1], prev[ci + 3])
            elif kind == 2 and flags & FLAG_CLOUD:
                which = "cloud"
                want = match_cell(cloud_lum, cx * CELL_W, cy * CELL_H, entries, cloud[0], cloud[1], prev[ci + 3])
            elif kind != 2:
                which, want = "fill", fill_glyph(cx, cy, prev[ci + 3], bool(flags & FLAG_HAND))
            else:
                skipped += 1
                continue
            got = glyph[ci + 3]
            if want == got:
                counts[which][0] += 1
            else:
                counts[which][1] += 1
                if shown < 10:
                    shown += 1
                    print("MISMATCH %s cell (%d,%d): cpu %r gpu %r" % (which, cx, cy, chr(32 + want), chr(32 + got)))
    total = cols * rows
    mismatched = sum(c[1] for c in counts.values())
    matched = sum(c[0] for c in counts.values())
    print("reference: %d/%d cells identical (%s), %d differ; %d plain sky cells, %d overlays" %
          (matched, matched + mismatched, ", ".join("%s %d/%d" % (k, c[0], sum(c)) for k, c in counts.items()),
           mismatched, skipped, overlays))
    print("stability: %d of %d cells changed glyph since the previous frame (%.2f%%)" %
          (sum(kind_changes), total, 100.0 * sum(kind_changes) / total))
    for k in range(4):
        if kind_cells[k]:
            print("  %-8s %5d cells, %5d changed (%.2f%%)" %
                  (KIND_NAMES[k], kind_cells[k], kind_changes[k], 100.0 * kind_changes[k] / kind_cells[k]))
    return 1 if mismatched else 0


if __name__ == "__main__":
    sys.exit(main())
