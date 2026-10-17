#!/usr/bin/env python3
"""Build the PLOS ONE submission from paper/paper.tex, the one source the author edits.

    .venv-fig/bin/python tools/paper/make_plos.py

Writes submission/plos/ (not in git):
    manuscript.pdf      the manuscript file: PLOS's template, captions in place, no images
    manuscript.tex      its source as ONE file with the references inlined, for after acceptance
    Fig1.tif ...        the figures, redrawn from results/ by make_figures.py in PLOS's format
    S1_Appendix.pdf     the appendices, as supporting information
    S2_Appendix.pdf

paper.tex is not changed. What differs from it is either a PLOS formatting rule (sentence-case headings,
"Fig", numbered references, no footnote, a caption after the first paragraph that cites it, appendices as
supporting information) or one of TEXT_CHANGES below, which exist only in this version and are the
author's to approve. The rules are PLOS's own: journals.plos.org/plosone/s/submission-guidelines,
/figures, /latex and /ethical-publishing-practice, read 2026-09-30. The checks at the end fail the build
when a rule is broken, and "cannot check" is a FAIL.
"""
from __future__ import annotations

import hashlib
import importlib.util
import io
import os
import re
import shutil
import subprocess
import sys
import tempfile
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
MS = ROOT / "paper"
TEX = MS / "paper.tex"
OUT = ROOT / "submission" / "plos"
CACHE = ROOT / "build" / "plos_template"
MAKE_FIGURES = Path(__file__).resolve().parent / "make_figures.py"
# PLOS's LaTeX template (version 3.8, April 2026), pinned: a new template is a change to review.
TEMPLATE_URL = "https://journals.plos.org/plosone/s/file?id=1457/PLOS_latex_template.zip"
TEMPLATE_SHA = "ea3a8a0fdbac77f95de47639541b09ef1583e059ace6783367490af9fa0b9a60"

TITLE = "Letting the tools do the math: A 10.9M-parameter language model on a graphing calculator"
AUTHOR = "Alexander Messerlian"
AFFILIATION = "Independent Researcher, Palo Alto, California, United States of America"
EMAIL = "alex.messerlian@icloud.com"

# PLOS: "Titles should be written in sentence case", and headings with them. Every heading in paper.tex
# must be listed: an unlisted one stops the build instead of passing through in title case.
SENTENCE = {
    "Introduction": "Introduction",
    "Related Work": "Related work",
    "System": "System",
    "The model never computes": "The model never computes",
    "Model and tokenizer": "Model and tokenizer",
    "Engine and loader": "Engine and loader",
    "Training Data": "Training data",
    "Evaluating on the Calculator's Own Code": "Evaluating on the calculator's own code",
    "How far the harness moves the result": "How far the harness moves the result",
    "A defect only the calculator's decoder could show": "A defect only the calculator's decoder could show",
    "Eight generation-and-tool turns agree between host and calculator":
        "Eight generation-and-tool turns agree between host and calculator",
    "With the Correct Relation Supplied": "With the correct relation supplied",
    "End to End: Choosing the Relation": "End to end: Choosing the relation",
    "What a Token Costs": "What a token costs",
    "Memory and Speed Limits": "Memory and speed limits",
    "Limitations": "Limitations",
    "Conclusion": "Conclusion",
    "Two Model Widths": "Two model widths",
    "The Calculator in Use": "The calculator in use",
}
# The appendices become supporting information files, cited by name as PLOS's template does.
SI = {"app:ladder": ("S1_Appendix", "S1 Appendix", "Two model widths",
                     "A 3.1M-parameter model trained exactly as the 10.9M-parameter one, scored on the "
                     "calculator's decoder."),
      "app:device": ("S2_Appendix", "S2 Appendix", "The calculator in use",
                     "Frames from a video of the calculator answering a question on battery, and screens "
                     "drawn by the application's own code.")}

# TEXT CHANGES: wording that exists only in the PLOS version, for the author to approve. Each is applied
# exactly once or the build stops. Whitespace in `old` matches any run of whitespace (line breaks too).
TEXT_CHANGES = [
    # The release happened (GitHub v1.0, 28 Sept 2026); paper.tex still has the review copy's tense.
    ("The store, the corpus and the weights will be released, so an institution",
     "The store, the corpus and the weights are released, so an institution"),
    ("The video will be released with the code.", "The video is released with the code."),
]
# PLOS's AI policy: a disclosure must name the tools, say which parts of the study, article and data they
# affected, and say "how the user evaluated the validity of the tool's outputs"; for authors it goes "in
# the body of the article ... in the Acknowledgement section". paper.tex's own "Use of AI tools" item is
# used word for word; this adds the parts PLOS asks for that it does not state. FOR THE AUTHOR TO CONFIRM.
AI_STATEMENT = (
    "Claude Opus~5 and Claude Opus~5.5 (Anthropic) were used to help develop the software, run host-side "
    "tests, train the model. The author conceived and directed the project "
    "and all of its agentic workflows, made every measurement on the calculator, checked every reported "
    "result against the logged data, and revised and edited the manuscript.")

results: list[tuple[str, bool, str]] = []


def check(name: str, ok: bool, detail: str = "") -> None:
    results.append((name, ok, detail))


def fail(msg: str) -> None:
    raise SystemExit(f"make_plos: {msg}")


def once(s: str, old: str, new: str, what: str) -> str:
    n = s.count(old)
    if n != 1:
        fail(f"expected exactly one {what}, found {n}")
    return s.replace(old, new)


def subn(pattern: str, repl, s: str, expected: int, what: str, flags: int = 0) -> str:
    out, n = re.subn(pattern, repl, s, flags=flags)
    if n != expected:
        fail(f"{what}: expected {expected} replacements, made {n}")
    return out


def group_end(s: str, i: int) -> int:
    """s[i] is '{': the index just past its matching '}', skipping escaped characters."""
    depth, j = 0, i
    while j < len(s):
        c = s[j]
        if c == "\\":
            j += 2
            continue
        if c == "{":
            depth += 1
        elif c == "}":
            depth -= 1
            if depth == 0:
                return j + 1
        j += 1
    fail("unbalanced braces")
    return -1


def arg(s: str, cmd: str, start: int = 0) -> tuple[int, int, str]:
    """The first `cmd{...}` at or after start: (start, end, argument)."""
    i = s.find(cmd + "{", start)
    if i < 0:
        return -1, -1, ""
    j = group_end(s, i + len(cmd))
    return i, j, s[i + len(cmd) + 1: j - 1]


def resolve_if(s: str, name: str, value: bool) -> str:
    """Replace every \\if<name> A \\else B \\fi with A (value True) or B, nested \\if counted."""
    token = "\\if" + name
    tok = re.compile(r"\\(if[a-zA-Z@]*|else|fi)(?![a-zA-Z@])")
    out, i = [], 0
    while (j := s.find(token, i)) >= 0:
        out.append(s[i:j])
        depth, els, fi = 0, None, None
        for m in tok.finditer(s, j + len(token)):
            w = m.group(1)
            if w.startswith("if"):
                depth += 1
            elif w == "else" and depth == 0:
                els = m
            elif w == "fi":
                if depth == 0:
                    fi = m
                    break
                depth -= 1
        if fi is None:
            fail(f"unterminated {token}")
        a = s[j + len(token): (els or fi).start()]
        b = s[els.end(): fi.start()] if els else ""
        out.append(a if value else b)
        i = fi.end()
    out.append(s[i:])
    return "".join(out)


def fuzzy_once(s: str, old: str, new: str) -> str:
    pat = r"\s+".join(re.escape(w) for w in old.split())
    return subn(pat, lambda m: new, s, 1, f"text change {old[:40]!r}")


def section_titles(tex: str) -> dict[str, str]:
    """label -> sentence-case title, for every \\section or \\subsection followed by its \\label."""
    return {lbl: SENTENCE[t] for t, lbl in
            re.findall(r"\\(?:sub)?section\{([^}]*)\}\s*\\label\{([^}]*)\}", tex)}


def headings(s: str) -> str:
    """Numbered headings become PLOS's starred, sentence-case ones; starred ones are already PLOS's."""
    def to(m):
        if m.group(2) not in SENTENCE:
            fail(f"heading not in SENTENCE: {m.group(2)!r}")
        return f"\\{m.group(1)}section*{{{SENTENCE[m.group(2)]}}}"
    return re.sub(r"\\((?:sub)*)section\{([^}]*)\}", to, s)


def section_refs(s: str, name) -> str:
    """"Section~\\ref{x}" -> "the section ``Title''" (sections are unnumbered in PLOS style), with
    "The" at the start of a sentence, a paragraph or an \\item."""
    s = re.sub(r"Sections~\\ref\{([^}]+)\}\s+and~\\ref\{([^}]+)\}",
               lambda m: f"the sections {name(m[1])} and {name(m[2])}", s)
    s = re.sub(r"Section~\\ref\{([^}]+)\}", lambda m: f"the section {name(m[1])}", s)
    return re.sub(r"((?:\A|\n[ \t]*\n)[ \t]*|[.!?]\}?\s+|\\item\s+)the section", r"\1The section", s)


def plos_caption(cap: str) -> str:
    """PLOS: "Bold the figure title." The first sentence is the title; the rest follows in roman."""
    for m in re.finditer(r"\.(\s+)(?=[A-Z(\\`])", cap):
        before = cap[max(0, m.start() - 4): m.start()]
        if before.endswith(("e.g", "i.e", " vs", " cf", "et al")):
            continue
        return f"\\textbf{{{cap[:m.start() + 1].strip()}}} {cap[m.end():].strip()}"
    return f"\\textbf{{{cap.strip()}}}"


def exchange_table(fl: str) -> str:
    """The logged generation-and-tool turn was a LaTeX-drawn box; PLOS takes no figures drawn in LaTeX
    and no typewriter font in figures, so the same lines become a table, one row per line."""
    p = fl.find("\\parbox{")
    if p < 0:
        fail("exchange box not found")
    w_end = group_end(fl, p + len("\\parbox"))          # \parbox{width}{content}
    content = fl[w_end + 1: group_end(fl, w_end) - 1]
    content = once(content, "\\small\\ttfamily\\raggedright", "", "the exchange box's font line")
    lines = [x.strip() for x in re.split(r"\\\\(?:\[[^\]]*\])?", content)]
    lines = [x for x in lines if x]
    rows = []
    for x in lines:
        m = re.match(r"\\textrm\{\\textit\{([a-z]+):\}\}~(.*)", x, re.S)
        rows.append((m.group(1), m.group(2)) if m else ("prompt", x))
    if [r for r, _ in rows] != ["prompt", "prompt", "model", "calculator", "model"]:
        fail(f"exchange rows changed: {[r for r, _ in rows]}")
    _, _, cap = arg(fl, "\\caption")
    body = "\n".join(f"{who} & {' '.join(text.split())} \\\\" for who, text in rows)
    return ("\\begin{table}[!ht]\n"
            f"\\caption{{{plos_caption(cap)}}}\n\\label{{tab:exchange}}\n"
            "\\begin{tabular}{@{}l>{\\raggedright\\arraybackslash\\ttfamily\\small}p{5.3in}@{}}\n"
            "\\toprule\nsource & text \\\\\n\\midrule\n"
            f"{body}\n\\bottomrule\n\\end{{tabular}}\n\\end{{table}}")


def float_blocks(fl: str) -> list[tuple[str, str]]:
    """One PLOS block per (label, caption) in a float: a figure is its caption only (the image is a
    separate file), a table keeps its content. Returns [(label, block)]."""
    if "\\fbox{\\parbox" in fl:
        return [("tab:exchange", exchange_table(fl))]
    kind = "figure" if fl.startswith("\\begin{figure}") else "table"
    out, pos = [], 0
    while True:
        i, j, cap = arg(fl, "\\caption", pos)
        if i < 0:
            break
        m = re.compile(r"\s*\\label\{([^}]+)\}").match(fl, j)
        if not m:
            fail(f"a {kind} caption without a label follows it: {cap[:40]!r}")
        out.append((m.group(1), cap, i, m.end()))
        pos = m.end()
    if kind == "figure":
        return [(lbl, f"\\begin{{figure}}[!h]\n\\caption{{{plos_caption(cap)}}}\n\\label{{{lbl}}}\n"
                      "\\end{figure}") for lbl, cap, _, _ in out]
    if len(out) != 1:
        fail(f"a table with {len(out)} captions")
    lbl, cap, i, j = out[0]
    inner = fl[len("\\begin{table}"): -len("\\end{table}")]
    inner = re.sub(r"^\[[^\]]*\]", "", inner)
    k = inner.find("\\caption{")
    inner = inner[:k] + f"\\caption{{{plos_caption(cap)}}}\n\\label{{{lbl}}}" + inner[k + (j - i):]
    inner = inner.replace("\\centering\\small", "\\small")
    return [(lbl, f"\\begin{{table}}[!ht]{inner}\\end{{table}}")]


def place_floats(text: str) -> tuple[str, list[str]]:
    """Take every float out, then put each caption block "immediately after the first paragraph in
    which the figure is cited" (PLOS). Returns the text and the labels in placement order."""
    floats = []
    pat = re.compile(r"\\begin\{(figure|table)\}.*?\\end\{\1\}", re.S)
    for m in pat.finditer(text):
        floats.extend(float_blocks(m.group(0)))
    text = pat.sub("", text)
    text = re.sub(r"\n{3,}", "\n\n", text)
    spots = []
    for lbl, block in floats:
        m = re.search(r"\\ref\{" + re.escape(lbl) + r"\}", text)
        if not m:
            fail(f"{lbl} is never cited in the text outside a caption")
        end = re.compile(r"\n[ \t]*\n").search(text, m.end())
        spots.append((end.start() if end else len(text), m.start(), lbl, block))
    for at, _, lbl, block in sorted(spots, key=lambda x: (x[0], x[1]), reverse=True):
        text = text[:at] + "\n\n" + block + text[at:]
    return text, [lbl for _, _, lbl, _ in sorted(spots, key=lambda x: (x[0], x[1]))]


def template() -> tuple[str, bytes]:
    z = CACHE / "PLOS_latex_template.zip"
    if not z.exists():
        CACHE.mkdir(parents=True, exist_ok=True)
        subprocess.run(["curl", "-s", "-L", "--max-time", "60", "-o", str(z), TEMPLATE_URL], check=True)
    data = z.read_bytes()
    if hashlib.sha256(data).hexdigest() != TEMPLATE_SHA:
        fail(f"PLOS's template changed ({z}); review it, then update TEMPLATE_SHA")
    zf = zipfile.ZipFile(io.BytesIO(data))
    # The template is written with CRLF line ends; normalized so the edits below match line by line.
    return zf.read("plos_latex_template.tex").decode().replace("\r\n", "\n"), zf.read("plos2025.bst")


def preamble(tpl: str, si: bool = False) -> str:
    p = tpl.split("\\begin{document}")[0]
    # tectonic runs XeTeX, which has no \DisableLigatures; PLOS's production runs pdfTeX, where it applies.
    p = once(p, "\\DisableLigatures[f]{encoding = *, family = * }",
             "\\usepackage{iftex}\n\\ifpdftex\\DisableLigatures[f]{encoding = *, family = * }\\fi",
             "\\DisableLigatures line")
    # "Manuscript text should be double-spaced" (submission guidelines); the template leaves it off.
    if not si:
        p = subn(r"%\\usepackage\{setspace\}\s*\n%\\doublespacing", "\\\\usepackage{setspace}\n\\\\doublespacing",
                 p, 1, "double spacing")
    p = once(p, "\\newcommand{\\lorem}{\\textbf{LOREM}}\n", "", "template \\lorem")
    p = once(p, "\\newcommand{\\ipsum}{\\textbf{IPSUM}}\n", "", "template \\ipsum")
    mine = ["\\usepackage{booktabs}", "\\usepackage{xurl}", "\\hypersetup{hidelinks}",
            "\\newcommand{\\tok}[1]{\\texttt{\\small #1}}"]
    if si:   # an appendix numbers its own tables and figures A, B, ...
        mine += ["\\renewcommand{\\thetable}{\\Alph{table}}", "\\renewcommand{\\thefigure}{\\Alph{figure}}",
                 "\\graphicspath{{figures/}}"]
    return once(p, "%% END MACROS SECTION", "\n".join(mine) + "\n%% END MACROS SECTION", "macros marker")


def front(abstract: str) -> str:
    return ("\\begin{document}\n\\vspace*{0.2in}\n\n\\begin{flushleft}\n{\\Large\n"
            f"\\textbf{{{TITLE}}}\n}}\n\\newline\n\\\\\n{AUTHOR}\\textsuperscript{{1*}}\n\\\\\n\\bigskip\n"
            f"\\textbf{{1}} {AFFILIATION}\n\\\\\n\\bigskip\n* {EMAIL}\n\\end{{flushleft}}\n\n"
            f"\\section*{{Abstract}}\n{abstract.strip()}\n\n"
            "\\clearpage\n\\newgeometry{top=0.85in,left=1in,right=1in,footskip=0.75in}\n\\linenumbers\n\n")


def common(s: str) -> str:
    """Rewrites every part shares: conditionals, author macros, citations, TMLR-only layout commands."""
    s = resolve_if(s, "deanonymized", True)
    s = re.sub(r"\\Theauthor(?:\{\})?(?![a-zA-Z])", "The author", s)
    s = re.sub(r"\\theauthor(?:\{\})?(?![a-zA-Z])", "the author", s)
    if re.search(r"\\cite(?!p\{)[a-z]*\{", s):
        fail("a citation other than \\citep: map it to numbered style by hand")
    s = s.replace("\\citep{", "\\cite{")
    s = re.sub(r"\\looseness=-1\s*", "", s)
    s = s.replace("\\label{endofmain}\n", "")
    return s


def build_main(tex: str) -> tuple[str, dict]:
    body = tex.split("\\begin{document}", 1)[1].rsplit("\\end{document}", 1)[0]
    i, j = body.find("\\begin{abstract}"), body.find("\\end{abstract}")
    abstract = body[i + len("\\begin{abstract}"): j]
    a, b, _ = arg(abstract, "\\footnote")
    if a < 0:
        fail("the abstract's AI footnote was not found")
    abstract = abstract[:a] + abstract[b:]           # PLOS: "Footnotes are not permitted"
    abstract = once(abstract, "\\preprintkeywords", "", "\\preprintkeywords")
    k = body.find("\\bibliography{references}")
    main, appendix = body[j + len("\\end{abstract}"): k], body.split("\\appendix", 1)[1]
    b0 = main.find("\\section*{Broader Impact Statement}")
    s0 = main.find("\\section*{Statements and Declarations}")
    if not 0 < b0 < s0:
        fail("Broader Impact / Statements sections not found in order")
    core, broader, statements = main[:b0], main[b0:s0], common(main[s0:])
    items = dict(re.findall(r"\\item \\textbf\{([^}]+)\.\}\s*(.*?)(?=\\item|\\end\{itemize\})", statements, re.S))
    for need in ("Use of AI tools", "Trademarks", "Funding", "Competing interests", "Data availability",
                 "Code availability", "Author contributions"):
        if need not in items:
            fail(f"statement {need!r} not found")

    text = common(core + broader.replace("\\section*{Broader Impact Statement}", "\\section*{Broader impact}"))
    for old, new in TEXT_CHANGES[:1]:
        text = fuzzy_once(text, old, new)
    text = headings(text)
    text = section_refs(text, lambda l: f"``\\nameref{{{l}}}''")
    n_app = len(re.findall(r"Appendix~\\ref\{app:", text))
    text = re.sub(r"Appendix~\\ref\{(app:[a-z]+)\}", lambda m: f"\\nameref{{{SI[m[1]][0]}}}", text)
    # Seven in the named text: the eighth is in a statement only the anonymous copy printed.
    check(f"appendix references cite S1/S2 Appendix by name ({n_app})", n_app > 0 and "app:" not in text)
    text = text.replace("Figure~\\ref{fig:exchange}", "Table~\\ref{tab:exchange}")
    text = text.replace("Figure~\\ref{", "Fig~\\ref{")          # PLOS: "use "Fig" instead of "Figure""
    text, order = place_floats(text)

    si = "\\section*{Supporting information}\n\n" + "\n\n".join(
        f"\\paragraph*{{{name}.}}\n\\label{{{lbl}}}\n\\textbf{{{title}.}} {legend}"
        for lbl, name, title, legend in SI.values())
    ack = f"\\section*{{Acknowledgments}}\n\n{AI_STATEMENT}\n\n{' '.join(items['Trademarks'].split())}"
    doc = (front(abstract) + text.strip() + "\n\n" + si + "\n\n" + ack +
           "\n\n\\nolinenumbers\n\n\\bibliography{references}\n\n\\end{document}\n")
    return doc, {"items": items, "order": order, "appendix": appendix, "abstract": abstract}


def build_si(appendix: str, labels: dict, titles: dict) -> dict[str, str]:
    """Each appendix as its own document. Its references into the main text become text, since a
    separate PDF cannot resolve them: "Table 3 of the main text", "the section ``...'' of the main text"."""
    parts = re.split(r"\\section\{", appendix)[1:]
    docs = {}
    for part in parts:
        title, rest = part.split("}", 1)
        lbl = re.match(r"\s*\\label\{([^}]+)\}", rest).group(1)
        name = SI[lbl][1]
        s = common(rest.split("\\label{" + lbl + "}", 1)[1])
        s = re.sub(r"\\clearpage\s*", "", s)
        s = re.sub(r"\\makeatletter\\setlength\{\\@fptop\}\{0pt\}\\makeatother[^\n]*\n", "", s)
        if lbl == "app:device":
            s = fuzzy_once(s, *TEXT_CHANGES[1])
        s = section_refs(s, lambda l: f"``{titles[l]}'' of the main text")
        own = set(re.findall(r"\\label\{((?:fig|tab):[^}]+)\}", s))

        def ref(m):
            kind, l = m.group(1), m.group(2)
            if l in own:
                return f"{'Fig' if kind.startswith('Fig') else 'Table'}~\\ref{{{l}}}"
            if l == "fig:exchange":
                l, kind = "tab:exchange", "Table"
            if l not in labels:
                fail(f"{name} cites {l}, which the main text does not number")
            return f"{'Fig' if kind.startswith('Fig') else 'Table'}~{labels[l]} of the main text"
        s = re.sub(r"(Figure|Table)~\\ref\{([^}]+)\}", ref, s)
        s = s.replace("Fig~\\ref{fig:device}c", "Fig~\\ref{fig:device}(c)")
        s = re.sub(r"Appendix~\\ref\{(app:[a-z]+)\}", lambda m: SI[m[1]][1], s)
        s = re.sub(r"\\begin\{(figure|table)\}\[[^\]]*\]", lambda m: f"\\begin{{{m[1]}}}[!ht]", s)
        s = bold_captions(s)
        docs[SI[lbl][0]] = (f"\\begin{{document}}\n\\begin{{flushleft}}\n{{\\Large\n\\textbf{{{name}. "
                            f"{SENTENCE[title]}.}}\n}}\n\\newline\n\\\\\nSupporting information for: "
                            f"\\textit{{{TITLE}}}. {AUTHOR}.\n\\end{{flushleft}}\n\n{s.strip()}\n\n\\end{{document}}\n")
    return docs


def bold_captions(s: str) -> str:
    out, pos = [], 0
    while True:
        i, j, cap = arg(s, "\\caption", pos)
        if i < 0:
            out.append(s[pos:])
            return "".join(out)
        out.append(s[pos:i] + f"\\caption{{{plos_caption(cap)}}}")
        pos = j


def compile_tex(folder: Path, name: str, env: dict) -> tuple[bool, str]:
    r = subprocess.run(["tectonic", "--keep-intermediates", name], cwd=folder, capture_output=True,
                       text=True, env=env)
    log = (r.stdout or "") + (r.stderr or "")
    return r.returncode == 0, log


def aux_labels(aux: Path) -> dict[str, str]:
    return {m[0]: m[1] for m in re.findall(r"\\newlabel\{([^}]+)\}\{\{([^}]*)\}", aux.read_text())}


def inline_bbl(doc: str, bbl: str) -> str:
    return once(doc, "\\bibliography{references}", bbl.strip(), "\\bibliography line")


def tidy_bbl(bbl: str) -> str:
    """plos2025.bst prints an arXiv entry's note ("ArXiv:2305.07759v2.") AND its eprint link, and a
    period after "et al." that it already ends with. One linked, versioned identifier and one period."""
    bbl, n = re.subn(r"\\newblock [Aa]rXiv:(\d{4}\.\d{4,5})(v\d+)\.\n"
                     r"\\newblock \\href \{http://arxiv\.org/abs/\1\} \{arXiv:\1\}\.",
                     r"\\newblock \\href{https://arxiv.org/abs/\1\2}{arXiv:\1\2}.", bbl)
    check(f"references: each arXiv identifier printed once ({n} merged)",
          not re.search(r"\\newblock [Aa]rXiv:\S+\n\\newblock \\href \{http://arxiv", bbl))
    return bbl.replace("et~al..", "et~al.")


def draw_figures(order: list[str], labels: dict) -> list[Path]:
    spec = importlib.util.spec_from_file_location("make_figures_plos", MAKE_FIGURES)
    mf = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mf)
    made = []
    with tempfile.TemporaryDirectory() as tmp:
        import contextlib
        with contextlib.redirect_stdout(io.StringIO()):
            mf.main(out=tmp, plos=True)
        for lbl in order:
            if not lbl.startswith("fig:"):
                continue
            src = Path(tmp) / f"fig_{lbl[4:]}.tif"
            if not src.exists():
                fail(f"make_figures.py --plos drew no {src.name}")
            dst = OUT / f"Fig{labels[lbl]}.tif"
            shutil.copyfile(src, dst)
            made.append(dst)
    return made


def plain(tex: str) -> str:
    s = re.sub(r"``|''", '"', tex)
    s = re.sub(r"\\[a-zA-Z]+\*?(\[[^\]]*\])?", " ", s)
    s = re.sub(r"[{}~$\\]", " ", s)
    return " ".join(s.split())


def main() -> int:
    tex = TEX.read_text()
    tpl, bst = template()
    doc, info = build_main(tex)
    doc = preamble(tpl) + doc
    titles = section_titles(tex)

    OUT.mkdir(parents=True, exist_ok=True)
    for old in [*OUT.glob("Fig*.tif"), *OUT.glob("S*_Appendix.pdf")]:
        old.unlink()
    env = dict(os.environ, SOURCE_DATE_EPOCH=str(int(TEX.stat().st_mtime)))
    with tempfile.TemporaryDirectory() as tmp:
        work = Path(tmp)
        (work / "plos2025.bst").write_bytes(bst)
        shutil.copyfile(MS / "references.bib", work / "references.bib")
        shutil.copytree(MS / "figures", work / "figures")
        # Pass 1 runs BibTeX with plos2025.bst; pass 2 compiles the ONE file PLOS asks for, with the
        # tidied references inlined, so the PDF and manuscript.tex carry the same references.
        (work / "draft.tex").write_text(doc)
        ok, log = compile_tex(work, "draft.tex", env)
        check("the manuscript compiles with tectonic (BibTeX pass)", ok,
              log.strip().splitlines()[-1] if log.strip() else "")
        if not ok:
            (OUT / "manuscript_failed.log").write_text(log)
            return report()
        doc = inline_bbl(doc, tidy_bbl((work / "draft.bbl").read_text()))
        (work / "manuscript.tex").write_text(doc)
        ok, log = compile_tex(work, "manuscript.tex", env)
        check("the single-file manuscript compiles with tectonic", ok,
              log.strip().splitlines()[-1] if log.strip() else "")
        if not ok:
            (OUT / "manuscript_failed.log").write_text(log)
            return report()
        undefined = re.findall(r"(?:Reference|Citation) `([^']+)' .*undefined", log)
        check("no undefined references or citations", not undefined, ", ".join(sorted(set(undefined))[:5]))
        labels = aux_labels(work / "manuscript.aux")
        shutil.copyfile(work / "manuscript.pdf", OUT / "manuscript.pdf")
        (OUT / "manuscript.tex").write_text(doc)

        docs = build_si(info["appendix"], labels, titles)
        info["si_docs"] = docs
        for name, body in docs.items():
            (work / f"{name}.tex").write_text(preamble(tpl, si=True) + body)
            ok, log = compile_tex(work, f"{name}.tex", env)
            check(f"{name} compiles", ok, log.strip().splitlines()[-1] if log.strip() else "")
            if ok:
                shutil.copyfile(work / f"{name}.pdf", OUT / f"{name}.pdf")
                und = re.findall(r"(?:Reference|Citation) `([^']+)' .*undefined", log)
                check(f"{name}: no undefined references", not und, ", ".join(und[:5]))

    figs = draw_figures(info["order"], labels)
    verify(doc, info, labels, figs)
    return report()


def verify(doc: str, info: dict, labels: dict, figs: list[Path]) -> None:
    from PIL import Image
    body = doc.split("\\begin{document}", 1)[1]
    check("no images in the manuscript (PLOS: figures are uploaded separately)", "\\includegraphics" not in body)
    # \footnotesize is a font size, not a footnote: the command itself is matched, not the substring.
    check("no footnotes (PLOS: 'Footnotes are not permitted')", not re.search(r"\\footnote(?![a-zA-Z])", body))
    check("one file: no \\input or \\include", not re.search(r"\\(input|include)\{", body))
    check(f"title within 250 characters ({len(TITLE)})", len(TITLE) <= 250)
    words = len(plain(info["abstract"]).split())
    check(f"abstract within 300 words (PLOS asks 'below 300'; counted {words})", words < 300)
    heads = re.findall(r"\\(sub)*section\*\{([^}]*)\}", body)
    check("every heading is starred and in sentence case",
          all(t in SENTENCE.values() or t in ("Abstract", "Broader impact", "Supporting information",
                                               "Acknowledgments") for _, t in heads))
    check("at most three heading levels", "\\subsubsubsection" not in body)
    check("line numbers on, double spacing on", "\\linenumbers" in body and "\\doublespacing" in doc)
    cited = [int(labels[l]) for l in info["order"] if l.startswith("fig:")]
    check(f"figures numbered in the order first cited ({cited})", cited == sorted(cited) == list(range(1, len(cited) + 1)))
    tabs = [int(labels[l]) for l in info["order"] if l.startswith("tab:")]
    check(f"tables numbered in the order first cited ({tabs})", tabs == list(range(1, len(tabs) + 1)))
    check("'Fig', never 'Figure', cites a figure", "Figure~\\ref" not in body)
    everything = [doc, *info.get("si_docs", {}).values()]
    check("no em dashes in the manuscript or the appendices", not any("\u2014" in d for d in everything))
    stale = [w for d in everything for w in re.findall(r"will\s+be\s+released|final\s+version\s+of\s+this\s+paper", d)]
    check("nothing still promised as 'will be released'", not stale, ", ".join(stale))
    check("both appendices built", set(info.get("si_docs", {})) == {v[0] for v in SI.values()})
    ack = body.split("\\section*{Acknowledgments}", 1)[1]
    check("the AI disclosure is in the Acknowledgments, naming the tools",
          "Claude Opus" in ack and "Anthropic" in ack)
    for f in figs:
        im = Image.open(f)
        dpi = float(im.info["dpi"][0])
        w_in, h_in = im.width / dpi, im.height / float(im.info["dpi"][1])
        ok = (im.format == "TIFF" and im.mode == "RGB" and im.info.get("compression") == "tiff_lzw"
              and 300 <= round(dpi) <= 600 and 2.63 <= w_in <= 7.5 and h_in <= 8.75
              and f.stat().st_size <= 10 * 2 ** 20)
        check(f"{f.name}: TIFF, RGB, LZW, {dpi:.0f} dpi, {w_in:.2f} x {h_in:.2f} in, "
              f"{f.stat().st_size // 1024} KiB", ok)
    for lbl in [l for l in info["order"] if l.startswith("fig:")]:
        check(f"Fig{labels[lbl]}.tif exists for {lbl}", (OUT / f"Fig{labels[lbl]}.tif").exists())


def report() -> int:
    width = max(len(n) for n, _, _ in results)
    for n, ok, d in results:
        print(f"  {'PASS' if ok else 'FAIL'}  {n:<{width}}  {'' if ok else d}".rstrip())
    bad = sum(not ok for _, ok, _ in results)
    print(f"\n{'ALL CHECKS PASS' if not bad else f'{bad} CHECK(S) FAIL'}; output in {OUT.relative_to(ROOT)}/")
    return 1 if bad else 0


if __name__ == "__main__":
    raise SystemExit(main())
