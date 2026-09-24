"""
Build the Ammonite manual PDF.

  1. draws the vector art (logo, color wheel, clock diagram)
  2. runs capture.py unless --no-capture (screen images, recipes.json)
  3. fills the recipe cards into ammonite.html
  4. prints it twice with headless Edge: the first pass finds the page each
     chapter starts on, the second prints the contents with those numbers

Run:  python build.py [--no-capture]
"""
import html
import json
import math
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
IMG = os.path.join(HERE, "img")
EDGE = r"C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe"
NOTES = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]


def hue_rgb(h):
    """synthui::HueColor: full-saturation hue, as the screen draws it."""
    x = h * 6.0
    s, f = int(x) % 6, x - int(x)
    r, g, b = [(1, f, 0), (1 - f, 1, 0), (0, 1, f), (0, 1 - f, 1), (f, 0, 1), (1, 0, 1 - f)][s]
    return "#%02x%02x%02x" % (round(r * 255), round(g * 255), round(b * 255))


def note_color(pc):
    return hue_rgb(((pc * 7) % 12) / 12.0)


def write_logo(name, color):
    """An ammonite: a logarithmic spiral shell with its chamber walls."""
    b = math.log(1.9) / (2 * math.pi)      # the shell widens 1.9x per turn
    turns, a = 3.2, 1.0
    rmax = a * math.exp(b * turns * 2 * math.pi)
    k = 46.0 / rmax
    cx, cy = 50.0, 50.0

    def pt(t, scale=1.0):
        r = a * math.exp(b * t) * k * scale
        return cx + r * math.cos(t - math.pi / 2), cy + r * math.sin(t - math.pi / 2)

    n = 400
    t_end = turns * 2 * math.pi
    outer = " ".join("%.2f,%.2f" % pt(t_end * i / n) for i in range(n + 1))
    # septa: chamber walls between one whorl and the next, gently curved
    walls = []
    t = 2 * math.pi
    while t < t_end - 0.05:
        x0, y0 = pt(t - 2 * math.pi)
        x1, y1 = pt(t)
        mx, my = pt(t - math.pi * 0.93, 1.0)
        qx, qy = (x0 + x1) / 2 + (mx - (x0 + x1) / 2) * 0.12, (y0 + y1) / 2 + (my - (y0 + y1) / 2) * 0.12
        walls.append(f'<path d="M{x0:.2f},{y0:.2f} Q{qx:.2f},{qy:.2f} {x1:.2f},{y1:.2f}"/>')
        t += 2 * math.pi / (14 + 3 * (t / (2 * math.pi)))
    # the aperture: the shell's open end, closed against the whorl before it
    xa, ya = pt(t_end)
    xb, yb = pt(t_end - 2 * math.pi)
    walls.append(f'<path d="M{xa:.2f},{ya:.2f} L{xb:.2f},{yb:.2f}" stroke-width="1.6"/>')
    # fit the viewBox to the shell so it sits centred wherever it is placed
    xs = [pt(t_end * i / n)[0] for i in range(n + 1)]
    ys = [pt(t_end * i / n)[1] for i in range(n + 1)]
    pad = 1.5
    x0, y0 = min(xs) - pad, min(ys) - pad
    w, h = max(xs) - min(xs) + 2 * pad, max(ys) - min(ys) + 2 * pad
    svg = f'''<svg xmlns="http://www.w3.org/2000/svg" viewBox="{x0:.2f} {y0:.2f} {w:.2f} {h:.2f}">
<g fill="none" stroke="{color}" stroke-linecap="round" stroke-linejoin="round">
<polyline points="{outer}" stroke-width="1.6"/>
<g stroke-width="0.7">{"".join(walls)}</g>
</g></svg>'''
    open(os.path.join(IMG, name), "w", encoding="utf-8").write(svg)


def write_fifths_wheel():
    """The 12 notes in circle-of-fifths order, C at the top, in their screen
    colors on a dark disc (bright hues are unreadable on white paper)."""
    items = []
    for pos in range(12):
        pc = (pos * 7) % 12
        ang = pos / 12 * 2 * math.pi
        x, y = 100 + 72 * math.sin(ang), 100 - 72 * math.cos(ang)
        col = note_color(pc)
        r, g, b = (int(col[i:i + 2], 16) for i in (1, 3, 5))
        ink = "#111" if 0.299 * r + 0.587 * g + 0.114 * b > 110 else "#fff"
        items.append(f'<circle cx="{x:.1f}" cy="{y:.1f}" r="16" fill="{col}"/>')
        items.append(f'<text x="{x:.1f}" y="{y + 0.5:.1f}" fill="{ink}">{NOTES[pc]}</text>')
    svg = f'''<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 200 200">
<circle cx="100" cy="100" r="99" fill="#0b0b0c"/>
<circle cx="100" cy="100" r="72" fill="none" stroke="#2c2e33" stroke-width="1"/>
<g font-family="Bahnschrift, Segoe UI, sans-serif" font-size="14" font-weight="600"
   text-anchor="middle" dominant-baseline="central">{"".join(items)}</g>
</svg>'''
    open(os.path.join(IMG, "fifths_wheel.svg"), "w", encoding="utf-8").write(svg)


def write_clock():
    """Two bars of the power-on patterns on the master clock: three grids
    from one beat, OSC 3's LENGTH 6 wrapping across the bar lines, and its
    DENSITY 4/6 rests (the core's Euclidean rule)."""
    x0, x1, beats = 190, 790, 8
    bw = (x1 - x0) / beats
    rows = [("OSC 1", "1/4 · LENGTH 8", 1, 8, 8),
            ("OSC 2", "1/8 · LENGTH 8", 2, 8, 8),
            ("OSC 3", "1/16 · LENGTH 6 · DENSITY 4/6", 4, 6, 4)]
    out = []
    for b in range(beats + 1):
        x = x0 + b * bw
        bar = b % 4 == 0
        out.append(f'<line x1="{x:.1f}" y1="34" x2="{x:.1f}" y2="206" '
                   f'stroke="{"#1c1d1f" if bar else "#d6d2ca"}" stroke-width="{1.6 if bar else 0.8}"/>')
        if b < beats:
            out.append(f'<text x="{x + bw / 2:.1f}" y="26" class="beat">{b % 4 + 1}</text>')
    out.append(f'<text x="{x0 + 2 * bw:.1f}" y="10" class="bar">BAR 1</text>')
    out.append(f'<text x="{x0 + 6 * bw:.1f}" y="10" class="bar">BAR 2</text>')
    for r, (osc, desc, per_beat, length, hits) in enumerate(rows):
        y = 58 + r * 56
        out.append(f'<text x="0" y="{y + 6}" class="osc">{osc}</text>')
        out.append(f'<text x="0" y="{y + 22}" class="desc">{desc}</text>')
        sw = bw / per_beat
        for i in range(beats * per_beat):
            pos = i % length
            plays = (pos * hits) % length < hits   # the core's DENSITY rule
            x = x0 + i * sw + 1.6
            w = sw - 3.2
            if plays:
                fill = "#a4501d" if pos == 0 else "#1c1d1f"
                out.append(f'<rect x="{x:.1f}" y="{y - 8}" width="{w:.1f}" height="24" rx="3" fill="{fill}"/>')
            else:
                out.append(f'<rect x="{x + 0.6:.1f}" y="{y - 7.4}" width="{w - 1.2:.1f}" height="22.8" '
                           f'rx="3" fill="#fff" stroke="#9a9ea5" stroke-width="1.2"/>')
    style = """text { font-family: Bahnschrift, "Segoe UI", sans-serif; fill: #1c1d1f; }
.beat { font-size: 11px; fill: #9a9ea5; text-anchor: middle; }
.bar { font-size: 11px; letter-spacing: 2px; fill: #62666d; text-anchor: middle; }
.osc { font-size: 14px; font-weight: 600; letter-spacing: 1px; }
.desc { font-size: 11px; fill: #62666d; }"""
    svg = ('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 800 212">\n'
           f'<style>{style}</style>\n{"".join(out)}\n</svg>')
    open(os.path.join(IMG, "clock.svg"), "w", encoding="utf-8").write(svg)


PAGE_NUM = {"MAIN": 1, "OSC": 2, "ARP": 3, "ENVELOPE": 4, "FILTER": 5, "LFO": 6,
            "DELAY": 7, "MIX": 8, "KEY": 9}
DASH = "–"


def recipe_cards():
    """recipes.py (titles, words) + recipes.json (what the screen showed)."""
    sys.path.insert(0, HERE)
    from recipes import RECIPES
    with open(os.path.join(HERE, "recipes.json"), encoding="utf-8") as f:
        data = json.load(f)
    cards = {}
    for r in RECIPES:
        groups = []   # [(page, section, name), {osc: text}] in first-seen order
        for row in data[r["name"]]["rows"]:
            key = (row["page"], row["section"], row["name"])
            for g in groups:
                if g[0] == key:
                    g[1][row["osc"]] = row["text"]
                    break
            else:
                groups.append((key, {row["osc"]: row["text"]}))
        trs = []
        last_page = last_sec = None
        for (pg, sec, name), vals in groups:
            if 0 in vals:   # a whole-instrument setting spans the three columns
                val = f'<td class="v" colspan="3">{html.escape(vals[0])}</td>'
            else:
                val = "".join(f'<td class="{"v" if o in vals else "v off"}">'
                              f'{html.escape(vals.get(o, DASH))}</td>' for o in (1, 2, 3))
            new_page = pg != last_page
            where = f"{PAGE_NUM[pg]} {pg}" if new_page else ""
            section = sec if new_page or sec != last_sec else ""
            last_page, last_sec = pg, sec
            trs.append(f'<tr{" class=first" if new_page else ""}><td class="pg">{where}</td>'
                       f'<td class="sec">{html.escape(section)}</td>'
                       f'<td><span class="fn">{html.escape(name)}</span></td>{val}</tr>')
        title = html.escape(r["title"])
        cards[r["name"]] = (
            f'<div class="recipe">\n  <div class="recipe-head">\n'
            f'    <img class="disc" src="img/screen_recipe_{r["name"]}.png" alt="{title} on the screen">\n'
            f'    <div><h2>{title}</h2><p>{html.escape(r["blurb"])}</p></div>\n  </div>\n'
            f'  <table class="settings">\n    <tr><th>Page</th><th>Section</th><th>Setting</th>'
            f'<th>OSC 1</th><th>OSC 2</th><th>OSC 3</th></tr>\n    ' + "\n    ".join(trs) +
            "\n  </table>\n</div>")
    return cards


def edge_print(html_path, pdf_path):
    """msedge.exe returns before the PDF is written, so the old file is
    removed first and the new one waited for until its size settles."""
    import time
    if os.path.exists(pdf_path):
        os.remove(pdf_path)
    url = "file:///" + html_path.replace("\\", "/")
    subprocess.run([EDGE, "--headless", "--disable-gpu", "--no-pdf-header-footer",
                    "--virtual-time-budget=5000", f"--print-to-pdf={pdf_path}", url],
                   check=True, capture_output=True)
    last = -1
    for _ in range(240):
        time.sleep(0.5)
        size = os.path.getsize(pdf_path) if os.path.exists(pdf_path) else -1
        if size > 0 and size == last:
            return
        last = size
    raise SystemExit(f"Edge did not write {pdf_path}")


def print_pdf():
    """Two passes: markers find each chapter's page, then the contents and
    the cross-references get their numbers."""
    import pymupdf
    with open(os.path.join(HERE, "ammonite.html"), encoding="utf-8") as f:
        src = f.read()
    for name, card in recipe_cards().items():
        src = src.replace(f"<!--RECIPE:{name}-->", card)
    ids = re.findall(r'<section class="chapter[^"]*" id="([\w-]+)"', src)
    tmp_html = os.path.join(HERE, "_print.html")
    tmp_pdf = os.path.join(HERE, "_pass1.pdf")
    out = os.path.join(HERE, "Ammonite_Manual.pdf")

    marked = src
    for i in ids:
        marked = marked.replace(f'id="{i}">', f'id="{i}"><span class="mark">@@{i}@@</span>', 1)
    with open(tmp_html, "w", encoding="utf-8") as f:
        f.write(marked)
    edge_print(tmp_html, tmp_pdf)
    pages = {}
    with pymupdf.open(tmp_pdf) as doc:
        for n, pg in enumerate(doc):
            for m in re.findall(r"@@([\w-]+)@@", pg.get_text()):
                pages.setdefault(m, n + 1)
    missing = [i for i in ids if i not in pages]
    if missing:
        raise SystemExit(f"chapter markers not found: {missing}")

    final = re.sub(r'<span class="pg" data-ref="([\w-]+)">[^<]*</span>',
                   lambda m: f'<span class="pg">{pages[m.group(1)]}</span>', src)
    with open(tmp_html, "w", encoding="utf-8") as f:
        f.write(final)
    edge_print(tmp_html, out)
    os.remove(tmp_html)
    os.remove(tmp_pdf)
    with pymupdf.open(out) as doc:
        print(f"wrote {out} ({doc.page_count} pages)")


def main():
    os.makedirs(IMG, exist_ok=True)
    write_logo("ammonite.svg", "#a4501d")
    write_fifths_wheel()
    write_clock()
    if "--no-capture" not in sys.argv:
        subprocess.run([sys.executable, os.path.join(HERE, "capture.py")], check=True)
    print_pdf()


if __name__ == "__main__":
    main()
