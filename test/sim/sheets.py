# Turns out/*.ppm into round-masked PNGs and contact sheets (out/sheetN.png)
from PIL import Image, ImageDraw
import glob
ims = []
for f in sorted(glob.glob('out/*.ppm')):
    im = Image.open(f).convert('RGB')
    mask = Image.new('L', im.size, 0)
    ImageDraw.Draw(mask).ellipse((0, 0, 389, 389), fill=255)
    bg = Image.new('RGB', im.size, (40, 40, 46))
    bg.paste(im, (0, 0), mask)
    bg.save(f.replace('.ppm', '.png'))
    ims.append((f, bg))
for s in range(0, len(ims), 6):
    sheet = Image.new('RGB', (1200, 840), (25, 25, 28))
    d = ImageDraw.Draw(sheet)
    for i, (f, im) in enumerate(ims[s:s + 6]):
        x, y = (i % 3) * 400, (i // 3) * 420
        sheet.paste(im, (x + 5, y + 25))
        d.text((x + 10, y + 5), f.split('/')[-1][:-4], fill=(200, 200, 200))
    sheet.save(f'out/sheet{s // 6 + 1}.png')
