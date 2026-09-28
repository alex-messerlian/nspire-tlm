#!/usr/bin/env python3
"""The calculator screens in the paper and the README, drawn by the application's own code.

    .venv-fig/bin/python tools/paper/make_screens.py           # paper/figures/screen_*.png

Each screen is one finished exchange drawn by build/render_screen, which runs the shipped app.c: the
app chooses the relation itself (open_picker, the enter key's path), parses the values and builds
the line above the answer exactly as the calculator does. What the model says comes from the
calculator's decoder replayed on the host (build/autoasm for the prompt, then build/int8gen), which
matches the device token for token; it is recorded below so the screens can be redrawn without the
model. check_paper.py redraws every screen and compares bytes, and when the model is here it runs
the decoder again and compares its output with the record.

THEME: dark, because the calculator was in its dark theme when the author filmed it (the theme
follows the clock, dark from 18:00 to 06:00), so these screens look like the frames beside them.
LIGHT=1 draws the light theme into build/screens-light/ instead, for anything printed on white.
"""
import hashlib, os, subprocess, sys, tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
FIG = ROOT / "paper" / "figures"
MODEL = ROOT / "build" / "transfer" / "model4096.bin.tns"
TOK = ROOT / "build" / "tok4096.tok"
SCALE = 3

# name -> (question as typed, the decoder's emitted text, sha256 of the PNG written); question None
# is the start screen. The first question is the one filmed, typed as it was typed (no question mark).
SCREENS = {
    "screen_start.png": (
        None, None,
        "c530fdcd8d8d98a59d6093c8f93893d3db6663b104bbe9380652e5378afa8903"),
    "screen_emf.png": (
        "Take l = 0.057, v = 1.35, B = 0.196. What was the motionally induced emf",
        "<tool> eval<arg> (0.196)*(0.057)*(1.35)</tool><res> 0.0150822</res>"
        "<a> 0.01508 V. From epsilon=B*l*v.<end>",
        "de6aa93ce26e7b5c827ff93eb46a0cf36d28ae3a74ced9a0600ee0303d81ab59"),
    "screen_symbol.png": (
        "Given V = 12, I = 2, find R",
        "<tool> eval<arg> (12.0)/(2.0)</tool><res> 6</res><a> That gives 6 ohm. Directly from R=V/I.<end>",
        "5403ba6a2a0f0df93e37e527f0b5d0bb51a7a0ce42a7b368e76655549964e8d6"),
    "screen_decline.png": (
        "Who wrote Hamlet?",
        " I cannot answer that: no record matches this question.<end>",
        "d99b769f1b7f1216deb6e03d3c088297763f6a26910c8a03609040f430ccc77d"),
}


def sha256(p: Path) -> str:
    return hashlib.sha256(p.read_bytes()).hexdigest()


def build() -> None:
    r = subprocess.run(["make", "-s", "build/render_screen", "build/autoasm", "build/int8gen"], cwd=ROOT,
                       capture_output=True, text=True)
    if r.returncode:
        sys.exit(f"make failed: {r.stderr.strip()[-400:]}")


def decode(questions: list[str]) -> list[str]:
    """The calculator decoder's output for each question, through the app's own selection."""
    asm = subprocess.run([str(ROOT / "build/autoasm")], cwd=ROOT, input="\n".join(questions) + "\n",
                         capture_output=True, text=True, check=True).stdout.splitlines()
    prompts = [l.split("\t", 1)[1] for l in asm]
    gen = subprocess.run([str(ROOT / "build/int8gen"), str(MODEL), str(TOK)], cwd=ROOT,
                         input="\n".join(prompts) + "\n", capture_output=True, text=True, check=True)
    return [l.split("\t")[1] for l in gen.stdout.splitlines() if l.startswith("GEN")]


def draw(out: Path, theme: str = "dark") -> dict[str, str]:
    """Draw every SCREENS entry into `out` from its recorded output; return name -> sha256."""
    from PIL import Image
    out.mkdir(parents=True, exist_ok=True)
    got = {}
    with tempfile.TemporaryDirectory() as tmp:
        for name, (q, emitted, _) in SCREENS.items():
            ppm = Path(tmp) / "s.ppm"
            args = [str(ROOT / "build/render_screen"), str(ppm), theme] + ([q, emitted] if q else [])
            subprocess.run(args, cwd=ROOT, capture_output=True, text=True, check=True)
            im = Image.open(ppm)
            im = im.resize((im.width * SCALE, im.height * SCALE), Image.NEAREST)   # whole pixels, no blur
            im.save(out / name, "PNG", compress_level=9)
            got[name] = sha256(out / name)
    return got


def main() -> None:
    build()
    asked = [(n, q, e) for n, (q, e, _) in SCREENS.items() if q]
    if MODEL.exists():
        now = decode([q for _, q, _ in asked])
        for (name, _, recorded), fresh in zip(asked, now):
            print(f"  {name}: decoder output {'matches the record' if fresh == recorded else 'DIFFERS: ' + fresh}")
    else:
        print(f"  (the model is not here, {MODEL}: outputs taken from the record, not re-decoded)")
    light = os.environ.get("LIGHT") == "1"
    got = draw(ROOT / "build" / "screens-light" if light else FIG, "light" if light else "dark")
    for name, digest in got.items():
        if light:
            print(f"  build/screens-light/{name}")
        else:
            print(f"  paper/figures/{name}: {'matches its recorded hash' if digest == SCREENS[name][2] else 'NEW ' + digest}")


if __name__ == "__main__":
    main()
