#!/usr/bin/env python3
"""The one way to temporarily mutate a source file and put it back.

WHY THIS EXISTS. The same defect -- restore the bytes, forget the timestamp -- appeared in THREE
independent implementations of this operation:

  1. gate_controls.py, per-control path .... fixed, with a comment naming the exact hazard
  2. gate_controls.py, interrupt path ...... same bug, same file, found the same day
  3. shape_mutation.py .................... same bug again, and the worst of the three: it needs
                                            no interrupt, so EVERY ordinary green gate run
                                            advanced src/store/shapecheck.c and silently
                                            invalidated the cross-compiled device build

Measured on (3): one run_gates.sh advanced the file by 897 s, and push-all.sh then refused the
whole device transfer for a file whose content had never changed. The failure is invisible to
`git status` and to `git diff` -- the bytes are right -- so the only symptom is a downstream tool
refusing to run, which reads as that tool being broken.

The first fix carried a comment describing this hazard precisely. It did not prevent the second or
the third, because a comment does not propagate to the next implementation. Only code does. Hence
a helper rather than a fourth fix, and gate_mutate_helper.py to keep a fifth from being written.

THE GUARANTEE. On exit -- normal, exception, or SIGTERM/SIGINT/SIGHUP -- the file has its original
bytes AND its original mtime. Any rebuild you pass runs while the mtime is still bumped, so make
sees the restore; the pristine time is put back afterwards. The binary therefore ends up newer
than the source, which is the truth.

    from mutatectx import mutating

    with mutating(SRC, rebuild=lambda: subprocess.run(BUILD)) as m:
        m.write(original.replace(old, new, 1))
        verdict = run_the_check()
    # bytes and mtime both restored here
"""
import atexit
import os
import pathlib
import signal
import sys

# path -> (original_text, pristine_mtime), for the interrupt path. The unwind path is the one that
# runs when something has already gone wrong, so it is where a leak is most likely and least
# likely to be looked for -- it gets the same guarantee as the happy path, from the same code.
_LIVE = {}


def _restore_all():
    for path, (text, mtime) in list(_LIVE.items()):
        try:
            p = pathlib.Path(path)
            if p.read_text() != text:
                p.write_text(text)
                print(f"  mutatectx: restored {path} (interrupted mid-mutation)", file=sys.stderr)
            # Unconditional. write() may have bumped the mtime even where the bytes are already
            # correct, and that bump ALONE invalidates every downstream artefact.
            os.utime(path, (mtime, mtime))
        except Exception:
            pass
    _LIVE.clear()


atexit.register(_restore_all)
for _sig in (signal.SIGTERM, signal.SIGINT, signal.SIGHUP):
    try:
        signal.signal(_sig, lambda *_a: (_restore_all(), sys.exit(130)))
    except (ValueError, OSError):
        pass          # not the main thread, or the platform lacks it


class mutating:
    """Context manager owning one file's temporary mutation."""

    def __init__(self, path, rebuild=None, stamp_ahead=2.0):
        self.path = pathlib.Path(path)
        self.rebuild = rebuild
        # STAMP AHEAD, DO NOT REBUILD EVERYTHING. make decides by mtime at sub-second granularity,
        # so a write and the binary it should invalidate can land in the same tick. Two seconds of
        # future gives make an unambiguous ordering and lets it rebuild exactly the dependents.
        # The alternative tried first was `make -B`, which cost 6.65 s a call to rebuild
        # twenty-five binaries in order to invalidate one.
        self.stamp_ahead = stamp_ahead
        self.original = None
        self.mtime0 = None

    def __enter__(self):
        self.original = self.path.read_text()
        self.mtime0 = self.path.stat().st_mtime
        _LIVE[str(self.path)] = (self.original, self.mtime0)
        return self

    def write(self, text):
        """Write mutated content, stamped forward so make cannot miss the change."""
        self.path.write_text(text)
        t = self.path.stat().st_mtime + self.stamp_ahead
        os.utime(self.path, (t, t))

    def __exit__(self, *exc):
        try:
            self.path.write_text(self.original)
            t = self.path.stat().st_mtime + self.stamp_ahead
            os.utime(self.path, (t, t))
            if self.rebuild is not None:
                self.rebuild()          # runs WITH the bump, so make sees the restore
        finally:
            # ...and only then the pristine time, so nothing downstream looks stale.
            os.utime(self.path, (self.mtime0, self.mtime0))
            _LIVE.pop(str(self.path), None)
        return False
