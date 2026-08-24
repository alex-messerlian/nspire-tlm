"""Atomic artefact writes.

A crash after a previous success leaves the OLD file in place, and every downstream consumer
accepts it -- a step that did not run becomes indistinguishable from one that ran cleanly. The
corpus generator crashed four times while composition figures were read from a stale
synth_sample.jsonl written by an earlier run.

write_json/write_text write to <path>.tmp and rename only on success. A crash leaves the .tmp
behind and the real path absent, so the next consumer fails loudly instead of reading last week's
data."""
import json, os, pathlib

def write_json(path, obj, **kw):
    p = pathlib.Path(path); t = p.with_suffix(p.suffix + ".tmp")
    with open(t, "w") as f: json.dump(obj, f, **kw)
    os.replace(t, p)
    return p

def write_text(path, s):
    p = pathlib.Path(path); t = p.with_suffix(p.suffix + ".tmp")
    t.write_text(s); os.replace(t, p)
    return p

def write_lines(path, lines):
    return write_text(path, "\n".join(lines))
