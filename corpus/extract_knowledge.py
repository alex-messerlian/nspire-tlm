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

--------------------------------------------------------------------------------------------------
REVISION 2. The first version shipped five defects, four of which an audit of its OUTPUT found and
one of which only a length histogram could see. Each fix below is structural -- it keys on a field
the source actually declares -- because every heuristic this repo has reached for in this position
has misdispatched. The counts are printed on every run: a filter that stops matching is a filter
that has silently stopped working.

  F1  NO TRUNCATION. `text = " ".join(paras)[:1400]` was a hard byte slice with no boundary search.
      1,056 of 1,682 entries (62.8%) came out at exactly 1400 chars and 843 (50.1%) ended mid-word
      -- "rely heavily on physic", "fundamental aspects of our universe. Th". Nothing downstream
      could see it: every structural gate passes a truncated string. Sections now keep their full
      text and the compression to answer length happens later, deliberately, where it is visible.

  F2  EXERCISE AND TEST-PREP SECTIONS DROPPED BY TITLE. 491 of 1,682 (29.2%) were bare question
      stems with the answers stripped -- "Test Prep for AP Courses" (138), "Critical Thinking" (47),
      "Test Prep Short Answer" (47). An 18-entry random sample was 18/18 non-explanatory.

  F3  FRONT MATTER DROPPED BY ITS OWN FIELD. 65 entries had book_section == "Preface": licensing,
      errata, author bios and marketing ("OpenStax is part of Rice University, which is a 501(c)(3)
      nonprofit charitable corporation"). 43/43 hand-read were non-explanation.

  F4  TEACHER NOTES DROPPED BY STRUCTURE, NOT BY MARKER. 90 entries (all hs, 20% of that tier)
      carried instructor-edition text: "Ask students to recall", "Have the students determine".
      They arrive inside <note class="os-teacher">, so the whole note is removed before paragraphs
      are read. Stripping the [BL]/[OL]/[AL]/[EL] markers instead would have left the instructions
      and kept the teacher's voice in the corpus; dropping the paragraph would have cut real physics
      in the 55 cases where a note sits mid-section and explanation resumes after it.
      NOTE the source contains `class="os-teacher"` AND one `class="os&#x2010;teacher"` with a
      Unicode hyphen, which a literal match misses. NOTE_DROP normalises before comparing.

  F5  MATHML RENDERED, NOT DELETED. The old strip() removed every <m:math> block on the grounds that
      it "renders as noise in prose", which deleted the content with it: an alpha-particle became
      "-particle", "SI unit for current; 1 A = 1 C/s" became "SI unit for current;". 185 of 1,589
      definitions (11.6%) were under 45 chars, mostly from this. corpus/mathlin.py linearises the
      MathML into the notation store_clean.json already uses, so the knowledge tier and the compute
      tier share one alphabet. Measured over all 44,669 physics math blocks: 1 parse failure.
--------------------------------------------------------------------------------------------------
"""
import json, pathlib, re, sys, collections

sys.path.insert(0, str(pathlib.Path(__file__).parent))
import mathlin

BOOKS = ["osbooks-physics", "osbooks-college-physics", "osbooks-university-physics-bundle"]
LEVEL = {"osbooks-physics": "hs", "osbooks-college-physics": "college",
         "osbooks-university-physics-bundle": "university"}

# Sections that are apparatus, not physics. The first group was written from inspection; the second
# was measured -- these are the exact titles of the 491 exercise/test-prep entries, which carry
# question stems with the answers stripped and explain nothing.
SKIP_TITLE = re.compile(
    r"^\s*(learning objectives?|teacher support|section summary|check your understanding|"
    r"conceptual questions|problems?( & | and )?exercises?|problems?|exercises?|glossary|"
    r"key (terms|equations|concepts)|summary|references?|further reading|test prep.*|"
    r"performance task|links? to physics|making connections|real-world connections?|"
    r"critical thinking|problem exercises|additional problems|challenge problems|"
    r"practice problems|concept items|extended response|short answer|multiple choice|"
    r"about openstax|errata|answers? to questions.*|section key terms|additional resources|"
    r"contributing authors|reviewers|customization|art attribution|"
    r"the .*openstax.*|our .*partners|senior contributing authors)\s*$", re.I)

# book_section values that are front matter rather than a chapter. Structural: the field is the
# book's own, not a guess from the title.
SKIP_BOOK_SECTION = {"preface", "about openstax", "acknowledgements", "acknowledgments"}

# <note class="..."> blocks removed before paragraphs are read. Teacher notes address the
# instructor; check-understanding and learning-objectives are exercise apparatus. Everything else
# -- worked examples, misconceptions, tips, "links to physics" -- is explanatory prose and is KEPT,
# because it is some of the best writing in the books for the purpose this corpus has.
NOTE_DROP = {"os-teacher", "os-teacher-demonstration", "teacher-demonstration",
             "check-understanding", "learning-objectives"}

TEACHER_MARK = re.compile(r"\[(BL|OL|AL|EL)\]")


def strip(x):
    """cnxml fragment -> plain text in the store's ASCII notation, with math linearised."""
    def _m(m):
        s, ok = mathlin.render(m.group(0))
        STATS["math_blocks"] += 1
        if not ok:
            STATS["math_unparsed"] += 1
        # NOT padded. The source's own whitespace is already right on both sides: a symbol
        # glued to a word ("<m:math>alpha</m:math>-particle") must stay glued, and a display
        # expression already has spaces around it. Padding produced "alpha -particle".
        return s
    x = re.sub(r"<m:math.*?</m:math>", _m, x, flags=re.S)
    x = re.sub(r"<[^>]+>", " ", x)
    x = re.sub(r"&#x([0-9a-fA-F]+);", lambda m: chr(int(m.group(1), 16)), x)
    x = re.sub(r"&#(\d+);", lambda m: chr(int(m.group(1))), x)
    x = x.replace("&amp;", "&").replace("&lt;", "<").replace("&gt;", ">").replace("&quot;", '"')
    for ch in mathlin.unmapped(x):
        STATS["unmapped"][ch] += 1
    x = mathlin.fold(x)
    return re.sub(r"\s+", " ", x).strip()


def drop_notes(body):
    """Remove <note> blocks whose class is instructor-directed or exercise apparatus.

    Hand-written rather than regex-nested because cnxml notes do nest (a worked example inside a
    teacher note). Returns the body with those spans deleted.
    """
    out, i = [], 0
    for m in re.finditer(r"<note\b([^>]*)>", body):
        if m.start() < i:
            continue
        cls = re.search(r'class="([^"]*)"', m.group(1))
        # the source carries one class="os&#x2010;teacher" with a Unicode hyphen
        name = re.sub(r"[‐-―]", "-", cls.group(1)).strip().lower() if cls else ""
        if not any(p in NOTE_DROP for p in name.split()):
            continue
        # walk to the matching </note>, counting nested <note>
        j, depth = m.end(), 1
        for t in re.finditer(r"</?note\b", body[m.end():]):
            depth += 1 if t.group(0) == "<note" else -1
            if depth == 0:
                j = m.end() + t.end()
                break
        else:
            j = len(body)
        out.append(body[i:m.start()])
        STATS["notes_dropped"][name] += 1
        i = j
    out.append(body[i:])
    return "".join(out)


STATS = collections.Counter()


def main():
    global STATS
    STATS = collections.Counter()
    STATS["unmapped"] = collections.Counter()
    STATS["notes_dropped"] = collections.Counter()
    STATS["skip_title"] = collections.Counter()
    secs, seen_term = [], {}
    for b in BOOKS:
        for f in sorted(pathlib.Path("corpus/raw", b).rglob("index.cnxml")):
            t = f.read_text(encoding="utf-8", errors="ignore")
            mod = f.parent.name
            m0 = re.search(r"<title>(.*?)</title>", t, re.S)
            mtitle = strip(m0.group(1)) if m0 else ""
            # 1. DEFINITIONS -- authored as term+meaning, no heuristic needed
            for d in re.findall(r"<definition[^>]*>(.*?)</definition>", t, re.S):
                tm = re.search(r"<term[^>]*>(.*?)</term>", d, re.S)
                mn = re.search(r"<meaning[^>]*>(.*?)</meaning>", d, re.S)
                if not (tm and mn):
                    continue
                term, mean = strip(tm.group(1)), strip(mn.group(1))
                if len(term) < 2 or len(mean) < 20:
                    STATS["def_too_short"] += 1
                    continue
                key = term.lower()
                # the same term is defined in several books; keep the LONGEST meaning
                if key in seen_term and len(seen_term[key]["meaning"]) >= len(mean):
                    continue
                seen_term[key] = {"term": term, "meaning": mean, "module": mod,
                                  "level": LEVEL[b], "section": mtitle}
            # 2. SECTIONS -- title plus the explanatory paragraphs under it
            if mtitle.strip().lower() in SKIP_BOOK_SECTION:
                STATS["drop_front_matter"] += 1
                continue
            for sm in re.finditer(r"<section[^>]*>(.*?)</section>", t, re.S):
                body = sm.group(1)
                st = re.search(r"<title>(.*?)</title>", body, re.S)
                if not st:
                    continue
                title = strip(st.group(1))
                if not title:
                    continue
                if SKIP_TITLE.match(title):
                    STATS["skip_title"][title] += 1
                    continue
                body = drop_notes(body)
                paras = [strip(p) for p in re.findall(r"<para[^>]*>(.*?)</para>", body, re.S)]
                paras = [p for p in paras if len(p) > 80 and not p.lower().startswith("figure")]
                if not paras:
                    STATS["no_paras"] += 1
                    continue
                text = " ".join(paras)          # F1: no cap. Compression happens downstream.
                if len(text) < 140:
                    STATS["too_short"] += 1
                    continue
                if TEACHER_MARK.search(text):
                    STATS["teacher_leak"] += 1
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
    print(f"  section chars (max/median): {max(len(s['text']) for s in secs):,} / "
          f"{sorted(len(s['text']) for s in secs)[len(secs)//2]:,}")
    print()
    print(f"  math blocks rendered : {STATS['math_blocks']:,}  "
          f"UNPARSED {STATS['math_unparsed']}")
    print(f"  notes dropped        : {sum(STATS['notes_dropped'].values()):,}  "
          f"{dict(STATS['notes_dropped'])}")
    print(f"  sections cut by title: {sum(STATS['skip_title'].values()):,}  "
          f"top {STATS['skip_title'].most_common(5)}")
    print(f"  front-matter modules : {STATS['drop_front_matter']}")
    print(f"  teacher markers STILL present after the note drop: {STATS['teacher_leak']}")
    print(f"  chars ascii_fold could not map: {sum(STATS['unmapped'].values())} "
          f"over {len(STATS['unmapped'])} distinct")
    return 0


if __name__ == "__main__":
    sys.exit(main())
