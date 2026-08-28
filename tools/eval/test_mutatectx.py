#!/usr/bin/env python3
"""The guarantee mutatectx exists to make, asserted on every exit path.

Each case is a NEGATIVE CONTROL in its own right: it fails against the pre-helper behaviour
(write the bytes back, leave the mtime), which is the defect that shipped three times.
"""
import os, pathlib, subprocess, sys, tempfile
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from mutatectx import mutating

FAILS = []
def check(name, cond, detail=""):
    print(f"  {'ok  ' if cond else 'FAIL'} {name}{'' if cond else '  -- ' + detail}")
    if not cond: FAILS.append(name)

def fixture(text="original\n", age=10_000):
    fd, p = tempfile.mkstemp(suffix=".c"); os.close(fd)
    p = pathlib.Path(p); p.write_text(text)
    old = p.stat().st_mtime - age            # make it plausibly old, like a real source
    os.utime(p, (old, old))
    return p, p.stat().st_mtime

# 1. normal exit
p, m0 = fixture()
with mutating(p) as m:
    m.write("mutated\n")
    seen_bumped = p.stat().st_mtime > m0
    seen_mutated = p.read_text() == "mutated\n"
check("mutation is visible inside the block", seen_mutated)
check("mtime is bumped inside the block (make must see it)", seen_bumped)
check("normal exit restores bytes", p.read_text() == "original\n")
check("normal exit restores mtime", p.stat().st_mtime == m0,
      f"{p.stat().st_mtime} != {m0}")
p.unlink()

# 2. exception
p, m0 = fixture()
try:
    with mutating(p) as m:
        m.write("mutated\n"); raise RuntimeError("boom")
except RuntimeError:
    pass
check("exception restores bytes", p.read_text() == "original\n")
check("exception restores mtime", p.stat().st_mtime == m0)
p.unlink()

# 3. rebuild hook runs while the bump is still in place
p, m0 = fixture(); observed = {}
with mutating(p, rebuild=lambda: observed.setdefault("mtime_at_rebuild", p.stat().st_mtime)) as m:
    m.write("mutated\n")
check("rebuild hook ran", "mtime_at_rebuild" in observed)
check("rebuild saw the bumped mtime, not the pristine one",
      observed.get("mtime_at_rebuild", m0) > m0)
check("pristine mtime restored after the rebuild", p.stat().st_mtime == m0)
p.unlink()

# 4. SIGTERM mid-mutation -- the path that produced the shipped defect
p, m0 = fixture()
prog = f'''
import os, sys, time, pathlib
sys.path.insert(0, {str(pathlib.Path("tools/eval").resolve())!r})
from mutatectx import mutating
with mutating(pathlib.Path({str(p)!r})) as m:
    m.write("mutated\\n")
    print("ready", flush=True)
    time.sleep(30)
'''
proc = subprocess.Popen([sys.executable, "-c", prog], stdout=subprocess.PIPE, text=True)
assert proc.stdout.readline().strip() == "ready"
proc.terminate(); proc.wait(timeout=10)
check("SIGTERM restores bytes", p.read_text() == "original\n", repr(p.read_text()))
check("SIGTERM restores mtime", p.stat().st_mtime == m0,
      f"{p.stat().st_mtime} != {m0}  (this is the exact defect that shipped)")
p.unlink()

print(f"\n  {'PASS' if not FAILS else 'FAIL: ' + ', '.join(FAILS)}")
sys.exit(1 if FAILS else 0)
