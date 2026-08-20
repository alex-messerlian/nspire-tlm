#!/usr/bin/env python3
"""Extract records from OpenStax Key Equations tables. Presentation MathML -> evaluator syntax,
then gated through evalcli. The yield is the point: report what survives, not what was attempted."""
import re, pathlib, subprocess, json, collections, unicodedata

GREEK = {"α":"alpha","β":"beta","γ":"gamma","δ":"delta","ε":"epsilon","θ":"theta","λ":"lambda",
 "μ":"mu","ν":"nu","π":"pi","ρ":"rho","σ":"sigma","τ":"tau","φ":"phi","ω":"omega","Ω":"Omega",
 "Δ":"Delta","Σ":"Sigma","Φ":"Phi","Γ":"Gamma","Θ":"Theta","Λ":"Lambda"}
STRIPNS = re.compile(r"</?m:")
def txt(n): return "".join(n.itertext())

import xml.etree.ElementTree as ET
M = "{http://www.w3.org/1998/Math/MathML}"
def conv(e):
    t = e.tag.replace(M, "")
    k = list(e)
    if t in ("math","mrow","mstyle","mpadded","semantics"):
        # Presentation MathML writes B*l*v_d as three adjacent <mi> with NO operator between them.
        # Concatenating them produced the identifier "Blv_d" -- a single symbol where three
        # variables were meant. Insert the implicit multiply that the markup omits.
        # Function names arrive as <mtext>tan</mtext> or <mi>sin</mi> followed by their argument
        # with no application syntax. Left alone they concatenate into identifiers -- "tantheta",
        # "gsintheta" -- which parse, gate clean, and are wrong. Found by hand-reading 30 documents.
        FN = ("sin","cos","tan","asin","acos","atan","sinh","cosh","tanh","ln","log","exp","sqrt")
        pieces = []
        for c in k:
            ct = c.tag.replace(M, "")
            if ct == "mspace": continue
            piece = conv(c)
            if piece: pieces.append((ct, piece))
        parts, prev_atom, i = [], False, 0
        while i < len(pieces):
            ct, piece = pieces[i]
            if piece in FN and i + 1 < len(pieces):          # apply to the next atom
                arg = pieces[i+1][1]
                if not (arg.startswith("(") and arg.endswith(")")): arg = "(" + arg + ")"
                if prev_atom: parts.append("*")
                parts.append(piece + arg); prev_atom = True; i += 2; continue
            atom = ct in ("mi","mn","mtext","msub","msup","mfrac","msqrt","mfenced","mrow")
            if prev_atom and atom and piece[0] not in "+-*/^=)":
                parts.append("*")
            parts.append(piece)
            prev_atom = atom and piece[-1] not in "+-*/^=("
            i += 1
        return "".join(parts)
    if t in ("mi","mn","mtext"):
        s = (e.text or "").strip()
        return GREEK.get(s, s)
    if t == "mspace": return ""
    if t == "mo":
        s = (e.text or "").strip()
        return {"−":"-","×":"*","·":"*","⋅":"*","∕":"/","=":"=","+":"+","(":"(",")":")"}.get(s, s)
    if t == "msub":
        return conv(k[0]) + "_" + re.sub(r"[^A-Za-z0-9]", "", conv(k[1]))
    if t == "msup":
        return "(" + conv(k[0]) + ")^(" + conv(k[1]) + ")"
    if t == "mfrac":
        return "((" + conv(k[0]) + ")/(" + conv(k[1]) + "))"
    if t == "msqrt":
        return "sqrt(" + "".join(conv(c) for c in k) + ")"
    if t == "mfenced":
        return "(" + "".join(conv(c) for c in k) + ")"
    return "".join(conv(c) for c in k)

rows, seen = [], set()
for mod in pathlib.Path("corpus/raw").rglob("index.cnxml"):
    s = mod.read_text(errors="ignore")
    if "Key Equations" not in s: continue
    for seg in re.findall(r"Key Equations.*?</table>", s, re.S):
        for r in re.findall(r"<row[^>]*>(.*?)</row>", seg, re.S):
            ents = re.findall(r"<entry[^>]*>(.*?)</entry>", r, re.S)
            if len(ents) < 2: continue
            name = re.sub(r"\s+"," ", re.sub(r"<[^>]+>","",ents[0])).strip()
            mm = re.search(r"<m:math.*?</m:math>", ents[1], re.S)
            if not (name and mm): continue
            xml = mm.group(0).replace("m:", "").replace("<math", '<math xmlns="http://www.w3.org/1998/Math/MathML"')
            try: expr = conv(ET.fromstring(xml))
            except Exception: continue
            expr = re.sub(r"\s+", "", expr)
            # Delta is a PREFIX on the next symbol, never a multiplicand: Delta*T is "change in T",
            # one quantity carrying T's unit. Verified across all 37 affected records -- 33 have
            # every Delta followed by a symbol and the remaining 4 are malformed anyway. Folding it
            # here is one rewrite instead of 37 annotation decisions, and it removes the artifact
            # from the corpus at the same time.
            expr = re.sub(r"\bDelta\*([A-Za-z][A-Za-z0-9_]*)", r"Delta_\1", expr)
            if expr.count("=") != 1: continue
            if not expr or expr in seen: continue
            seen.add(expr); rows.append({"name": name, "f": expr,
                "scope": mod.parent.name, "title": mod.parts[2]})

print(f"extracted {len(rows)} unique (name, equation) pairs from Key Equations tables")
# GATE A: parses and is solvable for its own LHS variable
calls = [f"<tool>solve<arg>{r['f']}<arg>{re.split(r'[^A-Za-z0-9_]', r['f'])[0]}</tool>" for r in rows]
out = re.findall(r"<res>(.*?)</res>", subprocess.run(["tools/eval/evalcli","-"],
      input="\n".join(calls)+"\n", capture_output=True, text=True).stdout, re.S)
ok = [r for r, o in zip(rows, out) if not o.startswith("!")]
err = collections.Counter(o.split()[0] for o in out if o.startswith("!"))
print(f"GATE A (parses + solvable for LHS): {len(ok)}/{len(rows)} = {100*len(ok)/max(1,len(rows)):.0f}%")
print("  rejects:", dict(err))
json.dump(ok, open("corpus/records_raw.json","w"), indent=1)
print("\nsample survivors:")
for r in ok[:12]: print(f"  {r['f']:<34} {r['name'][:52]}")
