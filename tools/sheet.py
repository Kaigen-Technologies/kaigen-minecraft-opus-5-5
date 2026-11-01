# labelled contact sheet of images; usage: python -I tools/sheet.py <out.jpg> <cols> <thumb width> <image>...
import sys, os
from PIL import Image, ImageDraw, ImageFont

out, cols, tw = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
files = sys.argv[4:]
font = ImageFont.truetype("C:/Windows/Fonts/arialbd.ttf", 20)
ims = []
for f in files:
    im = Image.open(f).convert("RGB")
    th = round(im.height * tw / im.width)
    im = im.resize((tw, th), Image.LANCZOS)
    d = ImageDraw.Draw(im)
    label = os.path.splitext(os.path.basename(f))[0]
    d.rectangle([0, 0, 12 * len(label) + 8, 26], fill=(0, 0, 0))
    d.text((4, 2), label, fill=(255, 255, 0), font=font)
    ims.append(im)
th = max(i.height for i in ims)
rows = (len(ims) + cols - 1) // cols
sheet = Image.new("RGB", (cols * tw + (cols - 1) * 4, rows * th + (rows - 1) * 4), (255, 0, 255))
for i, im in enumerate(ims):
    sheet.paste(im, ((i % cols) * (tw + 4), (i // cols) * (th + 4)))
sheet.save(out, quality=88)
print(out, sheet.size)
