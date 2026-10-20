# atlas test: decodes the 95 glyphs of the atlas debug view from a full-resolution screenshot and compares every
# device pixel against Monocraft's characters.json
# usage: python -I tools/ascii/atlas_test.py <screenshot.png> k=<k> ox=<ox> oy=<oy> cols=<cols>
import json
import os
import sys

from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
CHARS = os.path.join(ROOT, "tools", "ascii", "monocraft", "characters.json")


def expected_cell(ch):
    # 6x9 cell: glyph rows start at row 1, column 5 stays empty
    cell = [[0] * 6 for _ in range(9)]
    for r, line in enumerate(ch["pixels"]):
        for c, v in enumerate(line):
            cell[1 + r][c] = 1 if v else 0
    return cell


def main():
    path = sys.argv[1]
    kv = dict(a.split("=") for a in sys.argv[2:])
    k, ox, oy, cols = int(kv["k"]), int(kv["ox"]), int(kv["oy"]), int(kv["cols"])
    data = json.load(open(CHARS, encoding="utf-8"))
    by = {c["codepoint"]: c for c in data if "codepoint" in c}
    img = Image.open(path).convert("RGB")
    px = img.load()
    glyph_fail = 0
    pixel_fail = 0
    checked = 0
    for g in range(95):
        cell = expected_cell(by[32 + g])
        cx, cy = ox + (g % cols) * 6 * k, oy + (g // cols) * 9 * k
        bad = 0
        for y in range(9):
            for x in range(6):
                want = (255, 255, 255) if cell[y][x] else (0, 0, 0)
                for j in range(k):
                    for i in range(k):
                        checked += 1
                        if px[cx + x * k + i, cy + y * k + j] != want:
                            bad += 1
        if bad:
            glyph_fail += 1
            pixel_fail += bad
            print("FAIL glyph %r: %d device px differ" % (chr(32 + g), bad))
    print("atlas test: %d/95 glyphs exact, %d device px checked, %d differ" % (95 - glyph_fail, checked, pixel_fail))
    return 1 if glyph_fail else 0


if __name__ == "__main__":
    sys.exit(main())
