#!/usr/bin/env python3
# Copyright (c) 2026 Martial Systems LLC. All rights reserved.
# Builds the PDF manual from the Markdown next to this script:
#   python3 docs/manual/build_pdf.py docs/manual/BUSHIDO_Manual.md
# Needs Python-Markdown (BSD licence, pip install markdown) and a Chrome, Chromium or Edge binary for the headless
# print to PDF. Neither is part of this repository. Images resolve relative to the .md.
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

import markdown

CSS = """
@page { size: A4; margin: 16mm 15mm 18mm 15mm; }
body { font-family: 'DejaVu Sans', 'Helvetica Neue', Arial, sans-serif; font-size: 9.6pt; line-height: 1.42; color: #16161a; }
h1 { font-size: 22pt; margin: 0 0 4pt; letter-spacing: 0.04em; }
h2 { font-size: 14.5pt; margin: 18pt 0 6pt; padding-bottom: 3pt; border-bottom: 1.5px solid #c9a227; page-break-after: avoid; }
h3 { font-size: 11.5pt; margin: 13pt 0 4pt; page-break-after: avoid; }
h4 { font-size: 10.5pt; margin: 10pt 0 3pt; page-break-after: avoid; }
p, li { orphans: 3; widows: 3; }
table { border-collapse: collapse; width: 100%; margin: 5pt 0 9pt; font-size: 8.6pt; page-break-inside: auto; }
tr { page-break-inside: avoid; }
th, td { border: 0.6pt solid #b9b9c2; padding: 2.6pt 4.5pt; vertical-align: top; text-align: left; }
th { background: #ececf1; }
code { font-family: 'DejaVu Sans Mono', monospace; font-size: 8.4pt; background: #f2f2f5; padding: 0 2pt; }
img { max-width: 100%; display: block; margin: 6pt auto 10pt; border: 0.6pt solid #888; page-break-inside: avoid; }
hr { border: 0; border-top: 0.6pt solid #ccc; margin: 12pt 0; }
"""


def find_chrome():
    for name in ("google-chrome", "google-chrome-stable", "chromium", "chromium-browser", "chrome", "msedge"):
        path = shutil.which(name)
        if path:
            return path
    candidates = ["/Applications/Google Chrome.app/Contents/MacOS/Google Chrome",
                  "/Applications/Chromium.app/Contents/MacOS/Chromium"]
    for env in ("PROGRAMFILES", "PROGRAMFILES(X86)", "LOCALAPPDATA"):
        root = os.environ.get(env)
        if root:
            candidates += [os.path.join(root, "Google", "Chrome", "Application", "chrome.exe"),
                           os.path.join(root, "Microsoft", "Edge", "Application", "msedge.exe")]
    for path in candidates:
        if os.path.exists(path):
            return path
    return None


def main():
    if len(sys.argv) != 2:
        sys.exit("usage: build_pdf.py <manual.md>")
    src = os.path.abspath(sys.argv[1])
    out = os.path.splitext(src)[0] + ".pdf"
    with open(src, encoding="utf-8") as f:
        text = f.read()
    title = text.splitlines()[0].lstrip("# ").strip()
    body = markdown.markdown(text, extensions=["tables", "fenced_code", "sane_lists"])
    html = f"<!doctype html><html><head><meta charset='utf-8'><title>{title}</title><style>{CSS}</style></head><body>{body}</body></html>"
    chrome = find_chrome()
    if chrome is None:
        sys.exit("no Chrome, Chromium or Edge found")
    with tempfile.NamedTemporaryFile("w", suffix=".html", dir=os.path.dirname(src), delete=False, encoding="utf-8") as f:
        f.write(html)
        tmp = f.name
    try:
        subprocess.run([chrome, "--headless", "--disable-gpu", "--no-sandbox", "--no-pdf-header-footer",
                        f"--print-to-pdf={out}", Path(tmp).as_uri()],
                       check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    finally:
        os.unlink(tmp)
    print(out)


if __name__ == "__main__":
    main()
