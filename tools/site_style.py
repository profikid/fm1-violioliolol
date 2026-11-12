#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Adapted from CHOPPAH's tools/site_style.py (same author).
"""fm1-violioliolol's look for the site fm1-base's tools/make_site.py builds (run it after make_site.py):

  tools/site_style.py SITE_DIR

- install/index.html (fm1-base's installer): app/site/site.css and install.css in place of its style, and the site's
  header above it; its markup and script stay as they are;
- emu/index.html (fm1-base's emulator): site.css and emu.css in place of its style, the site's header and an
  introduction in place of its title, and a "where to start" row under the panel;
- index.html and manual/index.html: site.css (with its fonts) in place of fm1-base's style, a class on <body> for each
  page; the manual's contents follow the reading (the section on screen marked).

The pages keep everything else make_site.py gave them: their text, links, images, tables and the contents."""
import re
import sys
from pathlib import Path

APP = Path(__file__).resolve().parents[1] / "app"
FONTS = ('<link rel="preconnect" href="https://fonts.googleapis.com">\n'
         '<link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>\n'
         '<link rel="stylesheet" href="https://fonts.googleapis.com/css2?family=Fraunces:opsz,wght@9..144,500;9..144,600;9..144,700'
         '&family=IBM+Plex+Mono:wght@400;500;600&family=IBM+Plex+Sans:wght@400;500;600&display=swap">\n')
SPY = """<script>
(function () {                         /* the contents follow the reading: the section on screen marked */
  var links = {}, nav = document.querySelector("nav.contents");
  if (!nav || !("IntersectionObserver" in window)) return;
  nav.querySelectorAll("a[href^='#']").forEach(function (a) { links[a.getAttribute("href").slice(1)] = a; });
  var seen = new Set();
  var io = new IntersectionObserver(function (es) {
    es.forEach(function (e) { if (e.isIntersecting) seen.add(e.target.id); else seen.delete(e.target.id); });
    var first = Object.keys(links).find(function (id) { return seen.has(id); });
    if (!first) return;
    Object.keys(links).forEach(function (id) {
      if (id === first) links[id].setAttribute("aria-current", "true"); else links[id].removeAttribute("aria-current");
    });
  }, { rootMargin: "-80px 0px -55% 0px" });
  Object.keys(links).forEach(function (id) { var h = document.getElementById(id); if (h) io.observe(h); });
})();
</script>
"""


def restyle(path, cls, extra=""):
    s = path.read_text(encoding="utf-8")
    css = (APP / "site" / "site.css").read_text(encoding="utf-8") + extra
    s, n = re.subn(r"<style>.*?</style>", lambda m: FONTS + "<style>" + css + "</style>", s, count=1, flags=re.S)
    if n != 1:
        raise SystemExit(f"{path}: no <style> to replace")
    return s.replace("<body>", f'<body class="{cls}">', 1)


def header(current):
    conf = (APP / "fm1.toml").read_text(encoding="utf-8")
    name = re.search(r'^name\s*=\s*"([^"]+)"', conf, re.M)
    repo = re.search(r'^repo\s*=\s*"([^"]+)"', conf, re.M)
    links = [("../emu/", "Emulator"), ("../install/", "Install"), ("../manual/", "Manual")]
    if repo:
        links.append((repo.group(1), "Source"))
    return ('<div class="page"><header class="site"><a class="home" href="../">' + (name.group(1) if name else "") +
            "</a><nav>" + "".join(f'<a href="{h}"' + (' aria-current="page"' if t == current else "") + f">{t}</a>"
                                  for h, t in links) + "</nav></header></div>\n")


EMU_START = """<div class="start-here" aria-label="Where to start">
  <div><b>Play a key</b><p>It starts on the nylon guitar. Every note is a string computed as it sounds: hold a key and it rings out, let go and it is damped.</p></div>
  <div><b>Change instrument</b><p><span class="k">ALGORITHM</span> picks the family (bass, guitar, bowed, folk), <span class="k">PRESET</span> the instrument. Try the cello: the bow sustains while you hold.</p></div>
  <div><b>Shape the string</b><p>K1–K4 are the instrument's four parameters; <span class="k">EDIT</span> shows four more. Everything is in the <a href="../manual/">manual</a>.</p></div>
</div>
"""


def emu(site):
    page = site / "emu" / "index.html"
    if not page.exists():
        return
    s = restyle(page, "emu", (APP / "site" / "emu.css").read_text(encoding="utf-8"))
    m = re.search(r"<header>\s*<h1>([^<]*)</h1>\s*</header>\s*<main>", s)
    if not m:
        raise SystemExit(f"{page}: no <header><h1> then <main> to replace")
    s = (s[:m.start()] + header("Emulator") + '<main>\n<div class="intro"><h1>' + m.group(1) + " in the browser</h1><p>The "
         "same code the FM-1 runs, with sound. Play it with the mouse, a touch screen or the keyboard.</p></div>" + s[m.end():])
    if '<details class="speak" id="speak">' in s:
        s = s.replace('<details class="speak" id="speak">', EMU_START + '<details class="speak" id="speak">', 1)
    else:
        s = s.replace("</main>", EMU_START + "</main>", 1)
    page.write_text(s, encoding="utf-8")


def install(site):
    page = site / "install" / "index.html"
    if not page.exists():
        return
    s = restyle(page, "install", (APP / "site" / "install.css").read_text(encoding="utf-8"))
    s = s.replace('<body class="install">', '<body class="install">\n' + header("Install"), 1)
    page.write_text(s, encoding="utf-8")


def main(site):
    site = Path(site)
    install(site)
    emu(site)
    home = site / "index.html"
    if home.exists():
        home.write_text(restyle(home, "home"), encoding="utf-8")
    man = site / "manual" / "index.html"
    if man.exists():
        s = restyle(man, "manual").replace("</body>", SPY + "</body>", 1)
        man.write_text(s, encoding="utf-8")
    print(f"site_style: {site}: restyled")


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit(__doc__)
    main(sys.argv[1])
