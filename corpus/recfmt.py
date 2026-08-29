"""The record span's field grammar, in one place.

THE SEPARATOR IS " | ", NOT "|". A formula may legitimately contain a bare pipe -- absolute value.
`f_beat=|f_2-f_1|` is one of the 164 shipped records, and on it `record.split("|")[0]` returns
`"f_beat="`, a truncated formula, while `split("|")[4]` lands on the *missing* field rather than
the fit field: every index after the formula shifts by two.

Nine consumers hand-rolled `split("|")` -- two gates that assert split disjointness, three
retrieval harnesses that key on the formula, and gate_fit_cue, which reads the fit field by index
and therefore read the wrong field for this record while reporting on it. The device is unaffected:
src/store/assemble.c only ever CONCATENATES with " | " and never parses a record back.

Ninth call site of one parse is a missing abstraction, not nine lapses. Everything that reads a
record span goes through here, and tools/eval/gate_bare_pipe.py fails any file that splits a record
on a bare pipe.
"""
SEP = " | "
FORMULA, UNITS, MISSING, CONDITION, FIT = range(5)


def fields(record):
    """The five fields, or fewer for the no-match span. Never splits inside a formula."""
    return [p.strip() for p in record.split(SEP)]


def formula(record):
    """The relation only. `f_beat=|f_2-f_1|` survives intact."""
    return fields(record)[FORMULA]


def fit(record):
    """The fit label. Indexing by position is what the bare-pipe split got wrong."""
    f = fields(record)
    return f[FIT] if len(f) > FIT else None
