#!/usr/bin/env python3
"""Extract the KNOWLEDGE TIER from OpenStax physics: term/meaning pairs and section explanations.

WHY A SECOND TIER. The store's 164 records carry formula, units and condition, which is everything
needed to COMPUTE and nothing needed to EXPLAIN. Asked "what is Hooke's law" the model could only
restate the formula, because that is all the record holds. A knowledge entry needs no units, no
declared ranges and no preconditions, so it scales to thousands at a fraction of the annotation cost
a compute record carries.

SOURCE. 740 physics modules, 33.4M chars, already on disk: osbooks-physics (high school),
osbooks-college-physics, osbooks-university-physics-bundle. OpenStax marks definitions structurally
with <definition><term>..</term><meaning>..</meaning></definition>, so the highest-quality entries
need no parsing heuristics at all.
"""
import json, pathlib, re, sys, collections

BOOKS = ["osbooks-physics", "osbooks-college-physics", "osbooks-university-physics-bundle"]
LEVEL = {"osbooks-physics": "hs", "osbooks-college-physics": "college",
         "osbooks-university-physics-bundle": "university"}

# Sections that are apparatus, not physics. Measured: these titles carry learning objectives,
# teacher notes, standards boilerplate and exercise sets, none of which explains a concept.
SKIP_TITLE = re.compile(
    r"^\s*(learning objectives?|teacher support|section summary|check your understanding|"
    r"conceptual questions|problems?( & | and )?exercises?|problems?|exercises?|glossary|"
    r"key (terms|equations|concepts)|summary|references?|further reading|test prep|"
    r"performance task|links? to physics|making connections|real-world connections?)\s*$", re.I)

def strip(x):
    x = re.sub(r"<m:math.*?</m:math>", " ", x, flags=re.S)   # MathML renders as noise in prose
    x = re.sub(r"<[^>]+>", " ", x)
    x = x.replace("&#x2019;", "'").replace("&#8217;", "'").replace("&amp;", "&")
    x = x.replace("&#x2014;", ", ").replace("&#8212;", ", ")   # em dash -> comma
    return re.sub(r"\s+", " ", x).strip()

def main():
    defs, secs = [], []
    seen_term = {}
    for b in BOOKS:
        for f in sorted(pathlib.Path("corpus/raw", b).rglob("index.cnxml")):
            t = f.read_text(encoding="utf-8", errors="ignore")
            mod = f.parent.name
            mtitle = strip((re.search(r"<title>(.*?)</title>", t, re.S) or [None, ""])[1]) \
                     if re.search(r"<title>(.*?)</title>", t, re.S) else ""
            # 1. DEFINITIONS -- authored as term+meaning, no heuristic needed
            for d in re.findall(r"<definition[^>]*>(.*?)</definition>", t, re.S):
                tm = re.search(r"<term[^>]*>(.*?)</term>", d, re.S)
                mn = re.search(r"<meaning[^>]*>(.*?)</meaning>", d, re.S)
                if not (tm and mn): continue
                term, mean = strip(tm.group(1)), strip(mn.group(1))
                if len(term) < 2 or len(mean) < 20: continue
                key = term.lower()
                # the same term is defined in several books; keep the LONGEST meaning
                if key in seen_term and len(seen_term[key]["meaning"]) >= len(mean): continue
                seen_term[key] = {"term": term, "meaning": mean, "module": mod,
                                  "level": LEVEL[b], "section": mtitle}
            # 2. SECTIONS -- title plus the explanatory paragraphs under it
            for sm in re.finditer(r"<section[^>]*>(.*?)</section>", t, re.S):
                body = sm.group(1)
                st = re.search(r"<title>(.*?)</title>", body, re.S)
                if not st: continue
                title = strip(st.group(1))
                if not title or SKIP_TITLE.match(title): continue
                paras = [strip(p) for p in re.findall(r"<para[^>]*>(.*?)</para>", body, re.S)]
                paras = [p for p in paras if len(p) > 80 and not p.lower().startswith("figure")]
                if not paras: continue
                text = " ".join(paras)[:1400]
                if len(text) < 140: continue
                secs.append({"title": title, "text": text, "module": mod,
                             "level": LEVEL[b], "book_section": mtitle})
    defs = sorted(seen_term.values(), key=lambda d: d["term"].lower())
    pathlib.Path("corpus/knowledge").mkdir(parents=True, exist_ok=True)
    json.dump(defs, open("corpus/knowledge/definitions.json", "w"), indent=1)
    json.dump(secs, open("corpus/knowledge/sections.json", "w"), indent=1)
    lv = collections.Counter(d["level"] for d in defs)
    lv2 = collections.Counter(s["level"] for s in secs)
    print(f"  definitions: {len(defs):,} unique terms   {dict(lv)}")
    print(f"  sections   : {len(secs):,}                {dict(lv2)}")
    print(f"  definition chars: {sum(len(d['meaning']) for d in defs):,}")
    print(f"  section chars   : {sum(len(s['text']) for s in secs):,}")
    return 0

if __name__ == "__main__":
    sys.exit(main())
