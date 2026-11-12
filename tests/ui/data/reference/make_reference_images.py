# Makes the reference-image test pictures (tests/ui/reference_images.sfui): a plain head seen from the front and
# from the side, drawn here from ellipses (original, no photo), small PNGs. Run: python3 make_reference_images.py
from PIL import Image, ImageDraw
import os

here = os.path.dirname(os.path.abspath(__file__))
W, H = 240, 300
ink, paper, guide = (40, 40, 48, 255), (236, 228, 214, 255), (200, 80, 60, 255)


def sheet(draw_head, name):
    img = Image.new("RGBA", (W, H), paper)
    d = ImageDraw.Draw(img)
    draw_head(d)
    for y in (H * 0.25, H * 0.5, H * 0.75):  # proportion guides
        d.line([(0, y), (W, y)], fill=guide, width=1)
    img.save(os.path.join(here, name), optimize=True)


def front(d):
    d.ellipse([60, 40, 180, 200], outline=ink, width=4)            # skull
    d.rectangle([100, 195, 140, 260], outline=ink, width=4)        # neck
    d.ellipse([88, 110, 108, 122], outline=ink, width=3)           # eyes
    d.ellipse([132, 110, 152, 122], outline=ink, width=3)
    d.line([(120, 120), (114, 150), (126, 150)], fill=ink, width=3)  # nose
    d.line([(104, 170), (136, 170)], fill=ink, width=3)            # mouth


def side(d):
    d.ellipse([50, 40, 180, 200], outline=ink, width=4)            # skull
    d.polygon([(175, 115), (196, 150), (172, 152)], outline=ink, width=3)  # nose
    d.rectangle([95, 190, 135, 260], outline=ink, width=4)         # neck
    d.ellipse([150, 108, 166, 120], outline=ink, width=3)          # eye
    d.ellipse([92, 100, 114, 140], outline=ink, width=3)           # ear


sheet(front, "front.png")
sheet(side, "side.png")
