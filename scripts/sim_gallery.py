"""Turns the simulator's output into a web page of screenshots and short animations.

    python scripts/sim_gallery.py sim-out    (after: sim/build.sh sim-out --frames 40)

Writes sim-out/index.html and one GIF per scenario that has animation frames, then deletes the frames.
"""
import glob
import html
import os
import sys

from PIL import Image

out = sys.argv[1] if len(sys.argv) > 1 else "sim-out"
version = sys.argv[2] if len(sys.argv) > 2 else ""

stills = sorted(p for p in glob.glob(os.path.join(out, "*.png")) if "-f" not in os.path.basename(p)[-9:])
cards = []
for still in stills:
    name = os.path.basename(still)[:-4]
    frames = sorted(glob.glob(os.path.join(out, name + "-f[0-9][0-9][0-9].png")))
    Image.open(still).save(still, optimize=True)   # the simulator writes uncompressed PNGs
    shown = name + ".png"
    if frames:
        imgs = [Image.open(f).convert("RGB") for f in frames]
        pal = imgs[0].quantize(colors=255, method=Image.Quantize.MEDIANCUT)
        seq = [im.quantize(palette=pal, dither=Image.Dither.NONE) for im in imgs]
        seq[0].save(os.path.join(out, name + ".gif"), save_all=True, append_images=seq[1:], duration=50, loop=0,
                    optimize=True)
        for f in frames:
            os.remove(f)
        shown = name + ".gif"
    title = name.split("-", 1)[1].replace("-", " ") if "-" in name else name
    cards.append(f'<figure><img src="{shown}" width="480" height="272" alt="{html.escape(title)}">'
                 f'<figcaption>{html.escape(title.capitalize())}</figcaption></figure>')

page = f"""<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Grid Mix simulator</title>
<style>
:root{{--bg:#0e1116;--card:#161b22;--text:#e9eef2;--sub:#9aa5b1}}
body{{margin:0;background:var(--bg);color:var(--text);font:15px/1.5 system-ui,sans-serif}}
main{{max-width:1040px;margin:0 auto;padding:24px 16px}}
h1{{font-size:22px;margin:0 0 4px}} p{{color:var(--sub);margin:0 0 20px}}
.grid{{display:grid;grid-template-columns:repeat(auto-fill,minmax(min(480px,100%),1fr));gap:20px}}
figure{{margin:0;background:var(--card);border-radius:10px;padding:10px}}
img{{width:100%;height:auto;border-radius:4px;display:block;image-rendering:auto}}
figcaption{{color:var(--sub);font-size:13px;padding-top:6px}}
a{{color:#4fb3d9}}
</style></head><body><main>
<h1>Grid Mix simulator</h1>
<p>Rendered on GitHub from the same screen code the display runs{(' (firmware ' + html.escape(version) + ')') if version else ''}.
Scene 05 uses real data captured from Open Electricity.
<a href="../">Try the live simulator</a> · <a href="../../">Back to Grid Mix</a></p>
<div class="grid">{''.join(cards)}</div>
</main></body></html>
"""
with open(os.path.join(out, "index.html"), "w") as f:
    f.write(page)
print(f"{len(cards)} scenes -> {out}/index.html")
