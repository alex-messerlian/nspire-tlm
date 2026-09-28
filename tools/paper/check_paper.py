#!/usr/bin/env python3
"""Pre-submission checks: the desk-rejection and preprint-screening risks, as code rather than a list.

    .venv-fig/bin/python tools/paper/check_paper.py              # while drafting
    .venv-fig/bin/python tools/paper/check_paper.py --final      # before the TMLR or preprint upload
    .venv-fig/bin/python tools/paper/check_paper.py --preprint   # also build + check the named preprint

It rebuilds paper/paper.pdf (the anonymous copy); --preprint also rebuilds paper/paper-preprint.pdf
and writes submission/preprint-source.zip, the LaTeX upload for preprints.org. LaTeX's working files
are removed afterwards, so paper/ holds only the manuscript.

Each check names the rule it enforces. "Cannot check" is reported as a FAIL, never as a pass: a
check that silently skips on missing input reads exactly like a check that found nothing.

Drafting mode tolerates the skeleton's guidance comments. --final does not: the preprint server
receives the LaTeX SOURCE, comments included, and drafting notes or placeholder instructions must
not reach it.
"""
from __future__ import annotations

import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
# THE PAPER FOLDER BUILDS ON ITS OWN: paper.tex, references.bib, figures/ and the TMLR style files
# together, laid out as the author's other paper keeps them. `cd paper && tectonic paper.tex`.
MS = ROOT / "paper"
TEX = MS / "paper.tex"
BIB = MS / "references.bib"
LOG = ROOT / "docs" / "paper" / "CITATION_LOG.md"
FIG = MS / "figures"
MAKE_FIGURES = Path(__file__).resolve().parent / "make_figures.py"
MAKE_MEDIA = Path(__file__).resolve().parent / "make_media.py"     # the photographs, cut from the video
MAKE_SCREENS = Path(__file__).resolve().parent / "make_screens.py"  # the screens, drawn by app.c
RES = ROOT / "results"
PREPRINT_ZIP = ROOT / "submission" / "preprint-source.zip"
# What the LaTeX upload needs besides the source: the style files and the bibliography. preprints.org
# asks for every file needed to recreate the PDF, the .bib included; the .bbl goes in too, so the source
# builds without running bibtex.
PREPRINT_FILES = ("references.bib", "tmlr.sty", "tmlr.bst", "fancyhdr.sty", "TMLR_STYLE_LICENSE")
# LaTeX's working files, removed after each build so the folder shows only the manuscript.
WORK = (".aux", ".bbl", ".blg", ".log", ".out")
# TMLR has NO page limit. Its author guide: "a paper's length should be justified by its content and
# papers that are unusually long (not counting any Appendices) are likely to result in reviewing
# delays" (jmlr.org/tmlr/author-guide.html, read 2026-09-25). 12 is OUR target, not a rule of theirs.
MAX_PAGES = 12
LM_FONT = ("LMRoman", "LMSans", "LMMono", "LMMath")   # tmlr.sty loads lmodern with T1

# Everything the author block now carries (name, email, city, ORCID) plus the repository name: any
# of them outside the author block, or in the submission PDF, breaks double-blind review.
IDENTIFYING = ["Messerlian", "alex-messerlian", "alex.messerlian", "nspire-tlm", "Palo Alto",
               "0009-0003-4933-6832", "icloud"]
# CASE-SENSITIVE for the markers: under re.I, "GAP" matched the English word "gap" and failed a
# correct draft ("the gap narrows to 9 points"). The markers are written in capitals by convention;
# only the lorem-ipsum filler is matched in any case.
PLACEHOLDER = re.compile(r"\b(TODO|FIXME|XXX|TBD|GAP)\b|\[GAP|\?\?|(?i:lorem ipsum)")
GUIDANCE = re.compile(r"(skeleton|budget|~\d[\d.]* page|written last|author's call|"
                      r"FACTS\.md|WRITING_PLAN|PRIOR_ART|DATA_PROVENANCE|assistant|LLM)", re.I)

results: list[tuple[str, bool, str]] = []


def check(name: str, ok: bool, detail: str = "") -> None:
    results.append((name, ok, detail))


def strip_author(tex: str) -> str:
    """tex with the \\author{...} argument removed, braces matched (names and email live there)."""
    i = tex.find("\\author{")
    if i < 0:
        return tex
    depth, j = 0, i + len("\\author")
    for j in range(j, len(tex)):
        depth += {"{": 1, "}": -1}.get(tex[j], 0)
        if depth == 0:
            return tex[:i] + tex[j + 1:]
    return tex


def page_fonts(pdf: Path) -> set[str]:
    """BaseFont names in each page's own /Font resources (figures' fonts live in XObjects)."""
    from pypdf import PdfReader
    out = set()
    for pg in PdfReader(str(pdf)).pages:
        for f in ((pg.get("/Resources") or {}).get("/Font") or {}).values():
            out.add(str(f.get_object().get("/BaseFont", "")))
    return out


def changed_at(p: Path) -> float:
    """When p's CONTENT last changed. A tracked file with no edits since the last commit: that
    commit's time, because a checkout writes files in its own order and sets every mtime to the
    moment it wrote them (the fast-forward of ~/nspire-tlm left all four figures 'older' than
    inputs nobody had touched). A file edited since, untracked, or outside git: its mtime."""
    try:
        rel = str(Path(p).resolve().relative_to(ROOT))
    except ValueError:
        return Path(p).stat().st_mtime
    git = ["git", "-C", str(ROOT)]
    dirty = subprocess.run(git + ["status", "--porcelain", "--", rel], capture_output=True, text=True)
    if dirty.returncode == 0 and not dirty.stdout.strip():
        t = subprocess.run(git + ["log", "-1", "--format=%ct", "--", rel],
                           capture_output=True, text=True).stdout.strip()
        if t:
            return float(t)
    return Path(p).stat().st_mtime


def clean_work(stem: str) -> None:
    """Remove LaTeX's working files for one build (paper or paper-preprint); they are regenerated."""
    for ext in WORK:
        MS.joinpath(stem + ext).unlink(missing_ok=True)


def write_preprint_zip(source: str, bbl: Path) -> list[str]:
    """submission/preprint-source.zip: the [preprint] source as paper.tex, its .bbl as paper.bbl (the
    name that matches it), the style files, the .bib and the figures. Fixed timestamps, so the same
    source always gives the same bytes. Returns the names written."""
    import zipfile
    names = []
    PREPRINT_ZIP.parent.mkdir(exist_ok=True)
    with zipfile.ZipFile(PREPRINT_ZIP, "w", zipfile.ZIP_DEFLATED) as z:
        def put(name: str, data: bytes) -> None:
            z.writestr(zipfile.ZipInfo(name, date_time=(2026, 1, 1, 0, 0, 0)), data)
            names.append(name)
        put("paper.tex", source.encode())
        put("paper.bbl", bbl.read_bytes())
        for f in PREPRINT_FILES:
            put(f, MS.joinpath(f).read_bytes())
        for f in sorted([*FIG.glob("*.pdf"), *FIG.glob("*.jpg"), *FIG.glob("*.png")]):   # drawn, filmed, screens
            put(f"figures/{f.name}", f.read_bytes())
    return names


def body_lines(tex: str) -> list[tuple[int, str]]:
    """Lines with comments stripped (an unescaped % starts a comment)."""
    out = []
    for i, line in enumerate(tex.splitlines(), 1):
        out.append((i, re.split(r"(?<!\\)%", line, maxsplit=1)[0]))
    return out


def main() -> int:
    final = "--final" in sys.argv
    tex = TEX.read_text()

    # 1. Placeholders in rendered text -- always a failure.
    hits = [f"L{i}: {t.strip()[:60]}" for i, t in body_lines(tex) if PLACEHOLDER.search(t)]
    check("no placeholders in the rendered text", not hits, "; ".join(hits[:3]))

    # 2. Guidance comments -- tolerated while drafting, fatal for upload (the source is uploaded).
    comments = [f"L{i}" for i, line in enumerate(tex.splitlines(), 1)
                if re.search(r"(?<!\\)%.*", line) and GUIDANCE.search(line)]
    if final:
        check("no drafting guidance left in the SOURCE (it is uploaded)", not comments,
              f"{len(comments)} guidance comment lines, e.g. {', '.join(comments[:5])}. "
              "Strip comments before upload (e.g. arxiv_latex_cleaner).")
    else:
        results.append((f"guidance comments present: {len(comments)} (allowed while drafting; FAIL under --final)", True, ""))

    # 3. Anonymity: the submission build must use plain \usepackage{tmlr}; [accepted] and [preprint]
    # both print the author. No identifying strings anywhere outside the \author{...} block.
    body = "\n".join(t for _, t in body_lines(tex))
    named = re.search(r"\\usepackage\[[^\]]*(accepted|preprint)[^\]]*\]\{tmlr\}", body)
    check("submission mode (plain \\usepackage{tmlr}) -- authors print as Anonymous", not named,
          f"[{named.group(1)}] prints the author; the preprint is built by --preprint" if named else "")
    rest = strip_author(tex)
    check("\\author{...} block found (so the leak check below is not vacuous)", rest != tex)
    leaks = [s_ for s_ in IDENTIFYING if s_.lower() in "\n".join(t for _, t in body_lines(rest)).lower()]
    check("no identifying strings outside the author block", not leaks, ", ".join(leaks))

    # 4. Citations: every \cite key exists in refs.bib AND has a CITATION_LOG row.
    bib_keys = set(re.findall(r"^@\w+\{(\w+),", BIB.read_text(), re.M))
    log_keys = set(re.findall(r"\| `(\w+)` \|", LOG.read_text()))
    cited = set()
    for _, t in body_lines(tex):
        for grp in re.findall(r"\\(?:cite|citep|citet|citeauthor|citeyear|nocite)\*?\{([^}]*)\}", t):
            cited |= {k.strip() for k in grp.split(",") if k.strip()}
    missing = sorted(cited - bib_keys); unlogged = sorted((cited & bib_keys) - log_keys)
    check("every cited key exists in refs.bib", not missing, ", ".join(missing))
    check("every cited key has a CITATION_LOG row (verified at source)", not unlogged, ", ".join(unlogged))
    check("every refs.bib entry has a CITATION_LOG row", not (bib_keys - log_keys), ", ".join(sorted(bib_keys - log_keys)))

    # 5. Figures exist and are newer than the data and the script that make them.
    figs = re.findall(r"\\includegraphics(?:\[[^\]]*\])?\{([^}]+)\}", "\n".join(t for _, t in body_lines(tex)))
    # \graphicspath directories are searched too, as LaTeX does; bare names resolved only against
    # latex/ reported every figure missing once the draft moved to \graphicspath{{../figures/}}.
    dirs = [TEX.parent] + [TEX.parent / d for d in re.findall(r"\{([^{}]+/)\}", "".join(
        re.findall(r"\\graphicspath\{(.*?)\}\s*$", tex, re.M)))]
    gone = [f for f in figs if not any((d / f).exists() or (d / (f + ".pdf")).exists() for d in dirs)]
    check("every \\includegraphics file exists", not gone, ", ".join(gone))
    # EACH FIGURE MUST BE EXACTLY WHAT make_figures.py DRAWS FROM results/ NOW. Redraw into a scratch
    # folder and compare bytes; the figures carry no creation date, so an unchanged figure redraws
    # to the same bytes. Comparing timestamps or commit times failed twice: a checkout reorders
    # mtimes, and a script change that leaves a figure unchanged made it read as stale forever.
    import contextlib, importlib.util, io, tempfile
    # Not every figure is drawn from results/: make_media.py cuts the photographs from the author's
    # recording (5b below) and make_screens.py draws the calculator screens with the app's code (5c).
    photos = [f for f in figs if Path(f).suffix.lower() != ".pdf"]
    spec = importlib.util.spec_from_file_location("make_figures", MAKE_FIGURES)
    mf = importlib.util.module_from_spec(spec)
    stale, absent = [], []
    try:
        spec.loader.exec_module(mf)
        with tempfile.TemporaryDirectory() as tmp, contextlib.redirect_stdout(io.StringIO()):
            mf.main(out=tmp)
            for f in (f for f in figs if f not in photos):
                name = Path(f).stem + ".pdf"
                drawn, used = Path(tmp) / name, FIG / name
                if not drawn.exists():
                    absent.append(f"{name} (make_figures.py does not draw it)")
                elif not used.exists() or drawn.read_bytes() != used.read_bytes():
                    stale.append(Path(f).stem)
    except Exception as e:                      # a missing input or a broken script: cannot check
        absent.append(f"make_figures.py failed: {e}")
    check("every included figure is exactly what make_figures.py draws from results/",
          not stale and not absent,
          f"regenerate: {', '.join(stale)}" if stale else f"cannot check: {'; '.join(absent)}")

    # 5b. EACH PHOTOGRAPH IS A FRAME OF THE AUTHOR'S RECORDING, UNRETOUCHED: the JPEG in figures/ must
    # be the one make_media.py records, and when the recording is here (it is not in git), cutting it
    # again must give the same bytes. An AI-regenerated still can never pass this.
    spec = importlib.util.spec_from_file_location("make_media", MAKE_MEDIA)
    mm = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mm)
    spec = importlib.util.spec_from_file_location("make_screens", MAKE_SCREENS)
    ms = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(ms)
    unknown = [Path(f).name for f in photos if Path(f).name not in mm.PHOTOS and Path(f).name not in ms.SCREENS]
    check("every image that is not a drawn figure is a video frame or an app screen, and nothing else",
          not unknown, f"made by neither make_media.py nor make_screens.py: {', '.join(unknown)}")
    names = [Path(f).name for f in photos if Path(f).name in mm.PHOTOS]
    if names:
        wrong = [n for n in names if (FIG / n).exists() and mm.sha256(FIG / n) != mm.PHOTOS[n][3]]
        check("every photograph is a frame make_media.py cuts from the recording, at its recorded hash",
              not wrong, f"differs from its recorded hash: {', '.join(wrong)}")
        cut = "the photographs cut again from the recording give the same bytes"
        if not mm.VIDEO.exists():
            if final:
                check(cut, False, f"cannot check: {mm.VIDEO} is missing (MEDIA=<dir> names another copy)")
            else:
                results.append(("photographs not cut again: the recording is not here (FAIL under --final)", True, ""))
        else:
            try:
                with tempfile.TemporaryDirectory() as tmp:
                    got = mm.draw_photos(Path(tmp), mm.source())
                differ = [n for n in names if n in mm.PHOTOS and got.get(n) != mm.PHOTOS[n][3]]
                check(cut, not differ, ", ".join(differ))
            except SystemExit as e:                 # make_media refuses a recording with another hash
                check(cut, False, str(e))

    # 5c. EACH CALCULATOR SCREEN IS WHAT THE APP'S OWN CODE DRAWS FROM THE RECORDED OUTPUT: redraw it
    # with build/render_screen (the shipped app.c) and compare bytes; and when the model is here, the
    # calculator's decoder must still give the output the screen was drawn from.
    screens = [Path(f).name for f in photos if Path(f).name in ms.SCREENS]
    if screens:
        try:
            with contextlib.redirect_stdout(io.StringIO()):
                ms.build()
            with tempfile.TemporaryDirectory() as tmp:
                got = ms.draw(Path(tmp))
            wrong = [n for n in screens if not (FIG / n).exists() or ms.sha256(FIG / n) != ms.SCREENS[n][2]]
            differ = [n for n in screens if got.get(n) != ms.SCREENS[n][2]]
            check("every calculator screen is what the app's code draws from the recorded output",
                  not wrong and not differ,
                  f"differs from its recorded hash: {', '.join(wrong)}" if wrong else f"redraws differently: {', '.join(differ)}")
        except (SystemExit, subprocess.CalledProcessError) as e:
            check("every calculator screen is what the app's code draws from the recorded output", False, f"cannot check: {e}")
        said = "the calculator's decoder still gives the output each screen shows"
        asked = [(n, ms.SCREENS[n][0], ms.SCREENS[n][1]) for n in screens if ms.SCREENS[n][0]]
        if not ms.MODEL.exists():
            if final:
                check(said, False, f"cannot check: {ms.MODEL} is missing")
            else:
                results.append(("screens not re-decoded: the model is not here (FAIL under --final)", True, ""))
        elif asked:
            try:
                now = ms.decode([q for _, q, _ in asked])
                off = [n for (n, _, rec), fresh in zip(asked, now) if fresh != rec]
                check(said, len(now) == len(asked) and not off, ", ".join(off))
            except subprocess.CalledProcessError as e:
                check(said, False, f"cannot check: {e}")

    # 6. Compile, then read the page the end-of-main-text marker landed on.
    # The PDF's build date is pinned to when the source last changed (SOURCE_DATE_EPOCH, which tectonic
    # honours), so rebuilding an unchanged source gives the same bytes and git shows no false change.
    import os
    env = dict(os.environ, SOURCE_DATE_EPOCH=str(int(changed_at(TEX))))
    r = subprocess.run(["tectonic", "--keep-intermediates", TEX.name], cwd=TEX.parent,
                       capture_output=True, text=True, env=env)
    check("compiles with tectonic", r.returncode == 0, r.stderr.strip().splitlines()[-1] if r.returncode else "")

    # 6b. The template's font is what is ON THE PAGE, read from the fonts EMBEDDED in the PDF (a log
    # warning is a proxy: under MLSys the log kept warning about fonts that were not on the page).
    # tmlr.sty loads lmodern, so every text font on a page must be Latin Modern.
    pdf_f = TEX.with_suffix(".pdf")
    if pdf_f.exists():
        fonts = page_fonts(pdf_f)
        other = sorted(f for f in fonts if not any(k in f for k in LM_FONT))
        check("the page is set in Latin Modern, the template font", bool(fonts) and not other,
              f"non-template fonts: {', '.join(other[:3])}" if other else "no fonts found -- a FAIL")
    else:
        check("the page is set in Latin Modern, the template font", False, "no PDF to inspect -- a FAIL")

    aux = TEX.with_suffix(".aux")
    m = re.search(r"\\newlabel\{endofmain\}\{\{[^}]*\}\{(\d+)\}", aux.read_text()) if aux.exists() else None
    clean_work(TEX.stem)
    if m:
        pg = int(m.group(1))
        check(f"main text within {MAX_PAGES} pages (ends on page {pg})", pg <= MAX_PAGES)
    else:
        check("page-limit marker found (\\label{endofmain} before \\bibliography)", False,
              "cannot check the page limit -- that is a FAIL, not a pass")

    # 7. The PDF itself: submission text and metadata anonymous, and the style's own marker present.
    if pdf_f.exists():
        from pypdf import PdfReader
        rd = PdfReader(str(pdf_f))
        meta = " ".join(str(v) for v in (rd.metadata or {}).values())
        text = " ".join((pg.extract_text() or "") for pg in rd.pages)
        leak = [s_ for s_ in IDENTIFYING if s_.lower() in (meta + " " + text).lower()]
        check("compiled PDF text and metadata are anonymous", not leak, ", ".join(leak))
        check("PDF prints 'Anonymous authors' and the TMLR review header",
              "Anonymous authors" in text and "Under review as submission to TMLR" in text)
        # TMLR's FAQ: authors "mention explicitly in their submission that they have used this tool, as a
        # footnote on the first page". Checked on the text of page 1 of the copy TMLR reviews.
        first = " ".join((rd.pages[0].extract_text() or "").split())
        check("page 1 carries the AI-use footnote TMLR's FAQ requires", "AI tools were used in this work" in first)

    # 8. The named preprint, built from THIS source with [preprint]: named, and no TMLR review header.
    if "--preprint" in sys.argv:
        pre = TEX.with_name("paper-preprint.tex")
        # ^ and re.M: the first textual "\\usepackage{tmlr}" is in a COMMENT above the real line, and
        # count=1 without the anchor rewrote the comment and built a second anonymous copy.
        new, n = re.subn(r"^\\usepackage\{tmlr\}", r"\\usepackage[preprint]{tmlr}", tex, count=1, flags=re.M)
        check("preprint: exactly one \\usepackage{tmlr} line rewritten", n == 1)
        pre.write_text(new)
        r = subprocess.run(["tectonic", "--keep-intermediates", pre.name], cwd=TEX.parent,
                           capture_output=True, text=True, env=env)
        check("preprint variant compiles", r.returncode == 0,
              r.stderr.strip().splitlines()[-1] if r.returncode else "")
        pdf_p = pre.with_suffix(".pdf")
        if r.returncode == 0 and pdf_p.exists():
            from pypdf import PdfReader
            pages = PdfReader(str(pdf_p)).pages
            t = " ".join((pg.extract_text() or "") for pg in pages)
            check("preprint names the author", "Messerlian" in t)
            check("preprint carries no TMLR review header or anonymity line",
                  "Under review" not in t and "Anonymous authors" not in t)
            # preprints.org: the first page must give title, authors, abstract, KEYWORDS, contact and
            # affiliations. The TMLR template has no keywords, so the preprint adds them.
            first = " ".join((pages[0].extract_text() or "").split())
            check("preprint page 1 has keywords and the contact address (preprints.org)",
                  "Keywords:" in first and "alex.messerlian@icloud.com" in first)
            bbl = pre.with_suffix(".bbl")
            if bbl.exists():
                names = write_preprint_zip(new, bbl)
                check(f"preprint upload written: {PREPRINT_ZIP.relative_to(ROOT)} ({len(names)} files)", True)
                # The upload must build with nothing but its own contents, as it will for the server.
                import tempfile, zipfile
                with tempfile.TemporaryDirectory() as tmp:
                    zipfile.ZipFile(PREPRINT_ZIP).extractall(tmp)
                    rz = subprocess.run(["tectonic", "paper.tex"], cwd=tmp, capture_output=True, text=True)
                    check("the preprint upload compiles on its own, from an empty folder", rz.returncode == 0,
                          rz.stderr.strip().splitlines()[-1] if rz.returncode else "")
            else:
                check("preprint upload written", False, "no .bbl from the preprint build -- a FAIL")
        clean_work(pre.stem)
        pre.unlink(missing_ok=True)

    width = max(len(n) for n, _, _ in results)
    for n, ok, d in results:
        print(f"  {'PASS' if ok else 'FAIL'}  {n:<{width}}  {'' if ok else d}".rstrip())
    bad = sum(not ok for _, ok, _ in results)
    print(f"\n{'ALL CHECKS PASS' if not bad else f'{bad} CHECK(S) FAIL'}{' (final mode)' if final else ' (drafting mode)'}")
    return 1 if bad else 0


if __name__ == "__main__":
    raise SystemExit(main())
