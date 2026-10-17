#!/usr/bin/env python3
"""Negative controls for make_plos.py's checks: each breaks one PLOS rule, and the check named for
that rule must FAIL. A check no broken input has failed is not yet a check.

    .venv-fig/bin/python tools/paper/make_plos.py        # first: the controls read its figures
    .venv-fig/bin/python tools/paper/plos_controls.py
"""
import importlib.util, shutil, sys, tempfile
from pathlib import Path
ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("mp", ROOT / "tools/paper/make_plos.py")
mp = importlib.util.module_from_spec(spec); spec.loader.exec_module(mp)
tpl, _ = mp.template()
doc, info = mp.build_main(mp.TEX.read_text())
doc = mp.preamble(tpl) + doc
labels = {l: str(i) for kind in ("fig:", "tab:") for i, l in
          enumerate([x for x in info["order"] if x.startswith(kind)], 1)}
info["si_docs"] = {v[0]: "" for v in mp.SI.values()}
figs = sorted((ROOT / "submission/plos").glob("Fig*.tif"))

def run(d=doc, inf=info, lab=labels, fg=figs):
    mp.results.clear(); mp.verify(d, inf, lab, fg)
    return {n: ok for n, ok, _ in mp.results}

base = run()
assert all(base.values()), [n for n, ok in base.items() if not ok]
bad_tif = Path(tempfile.mkdtemp()) / "Fig1.tif"
from PIL import Image
Image.open(figs[0]).save(bad_tif, compression="raw")    # Pillow keeps the source's LZW unless told otherwise
import subprocess; assert "LZW" not in subprocess.run(["tiffinfo", str(bad_tif)], capture_output=True, text=True).stdout
cases = [
    ("footnote", "no footnotes", dict(d=doc.replace("\\section*{Related work}", "\\section*{Related work}\\footnote{x}"))),
    ("image", "no images", dict(d=doc.replace("\\section*{Related work}", "\\section*{Related work}\\includegraphics{a}"))),
    ("input", "one file", dict(d=doc.replace("\\section*{Related work}", "\\section*{Related work}\\input{a}"))),
    ("em dash", "no em dashes", dict(d=doc + "\u2014")),
    ("future tense", "nothing still promised", dict(d=doc + " will be released ")),
    ("title case heading", "sentence case", dict(d=doc.replace("\\section*{Related work}", "\\section*{Related Work}"))),
    ("Figure not Fig", "'Fig', never 'Figure'", dict(d=doc.replace("Fig~\\ref{fig:position}", "Figure~\\ref{fig:position}", 1))),
    ("figure order", "figures numbered in the order", dict(lab={**labels, "fig:position": "2", "fig:stages": "1"})),
    ("table order", "tables numbered in the order", dict(lab={**labels, "tab:prior": "2", "tab:exchange": "1"})),
    ("AI disclosure", "AI disclosure", dict(d=doc.replace("(Anthropic)", ""))),
    ("abstract length", "abstract within 300", dict(inf={**info, "abstract": info["abstract"] + " word" * 20})),
    ("TIFF not LZW", "TIFF, RGB, LZW", dict(fg=[bad_tif])),
    ("missing appendix", "both appendices", dict(inf={**info, "si_docs": {"S1_Appendix": ""}})),
]
bad = 0
for name, check_prefix, kw in cases:
    got = run(**kw)
    hit = [n for n, ok in got.items() if check_prefix in n and not ok]
    print(f"  {'FIRED   ' if hit else 'SURVIVED'}  {name}")
    bad += not hit
print("all controls fire" if not bad else f"{bad} control(s) SURVIVED")
sys.exit(1 if bad else 0)
