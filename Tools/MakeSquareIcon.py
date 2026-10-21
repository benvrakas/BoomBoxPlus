"""Extends the 16:9 mod icon render to a square without distorting it: the background above and below is grown
from the colours along the top and bottom edges, darkened like the render's vignette, blurred, and blended into a
narrow band of the original.

    python Tools/MakeSquareIcon.py <16:9 render.png> <square output.png>

The output was resized to Docs/Images/icon-512.png and Resources/Icon128.png (Docs/Images/icon-1920.png is the
full size). Needs numpy, scipy and Pillow.
"""
import sys
import numpy as np
from PIL import Image
from scipy.ndimage import gaussian_filter, gaussian_filter1d

src, out = sys.argv[1], sys.argv[2]
im = np.asarray(Image.open(src).convert('RGB')).astype(np.float64)
h, w, _ = im.shape
pad = (w - h) // 2
band = 24          # rows averaged at each edge
feather = 48       # rows of the original blended into the fill

def edge(rows):
    color = rows.mean(axis=0)                       # per-column colour
    return gaussian_filter1d(color, 25, axis=0, mode='nearest')

top_now, top_in = edge(im[:band]), edge(im[band*3:band*4])
bot_now, bot_in = edge(im[h-band:]), edge(im[h-band*4:h-band*3])
# Vertical trend per column (towards the edge), measured over the edge band and damped as it extends.
top_slope = (top_now - top_in) / (band * 3)
bot_slope = (bot_now - bot_in) / (band * 3)
# Only background columns give a usable bottom trend; the boom box's feet sit near the bottom middle.
# The rows above the bottom edge include the boom box's underside, so its trend can't be measured there; the
# bottom gets an even darkening that matches the top's average instead.
bot_slope = np.broadcast_to(np.clip(top_slope, -0.08, 0.08).mean(axis=0), bot_slope.shape)
top_slope = np.clip(top_slope, -0.08, 0.08)

canvas = np.zeros((w, w, 3))
canvas[pad:pad+h] = im
d = np.arange(1, pad + 1)[:, None, None]
reach = 160.0 * (1 - np.exp(-d / 160.0))           # trend that levels off instead of running away
canvas[:pad] = (top_now[None] + top_slope[None] * reach)[::-1]
canvas[pad+h:] = bot_now[None] + bot_slope[None] * reach

fill = gaussian_filter(canvas, sigma=(30, 30, 0), mode='nearest')
mask = np.zeros((w, 1, 1))
mask[:pad] = 1
mask[pad+h:] = 1
for i in range(feather):
    t = 1 - (i + 1) / (feather + 1)
    mask[pad + i] = t * t
    mask[pad + h - 1 - i] = t * t
result = canvas * (1 - mask) + fill * mask
result += np.random.default_rng(1).uniform(-0.6, 0.6, result.shape) * mask   # breaks up banding in the fill
Image.fromarray(np.clip(result + 0.5, 0, 255).astype(np.uint8)).save(out)
print(w, w)
