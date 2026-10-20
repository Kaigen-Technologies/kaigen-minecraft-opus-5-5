# synthetic inputs with known answers for the ascii shape matcher (the cpu reference, proven identical to the gpu on
# read-backs by reference.py): lines, a gradient, a flat field, a step
# usage: python -I tools/ascii/synthetic.py
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import reference as ref  # noqa: E402

W, H = ref.CELL_W * 5, ref.CELL_H * 5  # 5x5 cells; the centre cell is judged, its neighbours feed the outer circles
OX, OY = ref.CELL_W * 2, ref.CELL_H * 2


def image(f):
    return [[f(x, y) for x in range(W)] for y in range(H)]


def lum_of(img):
    return lambda x, y: img[min(max(y, 0), H - 1)][min(max(x, 0), W - 1)]


def line(angle):
    # a 1 font px bright stroke through the centre of the glyph box (columns 0-4, rows 1-7), on black
    cx, cy = OX + 2.5, OY + 4.5
    d = {0: (1.0, 0.0), 45: (5.0, -7.0), 90: (0.0, 1.0), 135: (5.0, 7.0)}[angle]  # monocraft draws its diagonals 5 across, 7 down
    n = (d[1], -d[0])
    ln = (n[0] ** 2 + n[1] ** 2) ** 0.5

    def f(x, y):
        dist = abs((x + 0.5 - cx) * n[0] + (y + 0.5 - cy) * n[1]) / ln
        return 255 if dist <= 0.75 else 0
    return image(f)


def main():
    entries = ref.site_entries()
    cells = ref.glyph_cells()
    edge = ref.vocab_from_header("WC_ASCII_VOCAB")
    full = ref.vocab_from_header("WC_ASCII_ALL")
    sets = {}
    for name, vocab in (("edge", edge), ("full", full)):
        sets[name] = (ref.glyph_vectors(entries, cells, vocab), [ord(c) - 32 for c in vocab])

    def pick(img, which):
        gv, codes = sets[which]
        return chr(32 + ref.match_cell(lum_of(img), OX, OY, entries, gv, codes, 255))

    passed = failed = 0

    def check(name, got, ok, want):
        nonlocal passed, failed
        if ok:
            passed += 1
        else:
            failed += 1
        print("%s %-34s got %-8r want %s" % ("PASS" if ok else "FAIL", name, got, want))

    want_line = {0: "-_", 45: "/", 90: "|", 135: "\\"}
    for which in ("edge", "full"):
        for a in (0, 45, 90, 135):
            g = pick(line(a), which)
            check("line %3d deg (%s vocab)" % (a, which), g, g in want_line[a], " or ".join(want_line[a]))

    # gradient: flat fields of rising luma give glyphs of non-decreasing ink (the glyph vector's total)
    gv, codes = sets["full"]
    ink = {codes[i]: sum(v) for i, v in enumerate(gv)}
    seq = []
    for l in range(0, 256, 17):
        seq.append(pick(image(lambda x, y: l), "full"))
    totals = [ink[ord(c) - 32] for c in seq]
    mono = all(totals[i] <= totals[i + 1] for i in range(len(totals) - 1)) and totals[-1] > totals[0]
    check("gradient 0..255 (full vocab)", "".join(seq), mono, "non-decreasing ink, rising overall")

    # flat: one luma over the whole field gives one glyph at every cell position
    flat = image(lambda x, y: 140)
    gv, codes = sets["full"]
    picks = set()
    for oy in range(0, H - ref.CELL_H + 1, ref.CELL_H):
        for ox in range(0, W - ref.CELL_W + 1, ref.CELL_W):
            picks.add(chr(32 + ref.match_cell(lum_of(flat), ox, oy, entries, gv, codes, 255)))
    check("flat field, 25 cells (full vocab)", "".join(sorted(picks)), len(picks) == 1, "one glyph")

    # steps on black (the outline image the edge matcher sees has an exact-0 ground): ink goes where the light is
    g = pick(image(lambda x, y: 230 if x <= OX + 2 else 0), "edge")
    check("step, bright left (edge vocab)", g, g == "|", "'|'")
    g = pick(image(lambda x, y: 230 if y >= OY + 4 else 0), "edge")
    check("step, bright bottom (edge vocab)", g, g in "_.,", "'_' (or '.' ',')")
    # light in the last two rows that continues into the cell below belongs to that cell (directional contrast)
    g = pick(image(lambda x, y: 230 if y >= OY + 7 else 0), "edge")
    check("thin bright strip below (edge vocab)", g, g == " ", "' '")
    g = pick(image(lambda x, y: 230 if y <= OY + 4 else 0), "edge")
    check("step, bright top (edge vocab)", g, g in "-'`", "'-' (or quote marks)")
    dark = pick(image(lambda x, y: 0), "edge")
    check("black field (edge vocab)", dark, dark == " ", "' '")
    print("synthetic: %d passed, %d failed" % (passed, failed))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
