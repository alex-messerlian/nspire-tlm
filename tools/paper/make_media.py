#!/usr/bin/env python3
"""The calculator photographs in the paper and the supplementary video, all cut from ONE recording.

    .venv-fig/bin/python tools/paper/make_media.py            # paper/figures/photo_*.jpg
    .venv-fig/bin/python tools/paper/make_media.py --video    # also submission/supplementary/ and its ZIP

THE SOURCE is media/video/IMG_7083.mov (gitignored): the author's phone recording of 2026-09-27, one
continuous take from the reset button to power-off, calculator on battery with no cable attached,
unedited and muted (it has no audio track). MEDIA=<dir> points at another checkout's media/ folder.

THE FIGURE FRAMES are cut from the video at fixed times, cropped to the calculator and scaled; nothing
is retouched. Frame extraction and JPEG encoding are deterministic, so check_paper.py checks the
committed JPEGs against the hashes below and, when the video is present, re-derives them and compares
bytes. THE VIDEOS are re-encoded (H.264, 1080x1920, 30 fps) with no metadata at all: the phone's file
records where it was filmed and on what phone, and TMLR's supplementary material is anonymous and at
most 100 MB. A clip is one continuous span of the recording; nothing inside a span is cut or retimed.
"""
import hashlib, os, subprocess, sys, tempfile, zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
MEDIA = Path(os.environ.get("MEDIA", ROOT / "media"))
VIDEO = MEDIA / "video" / "IMG_7083.mov"
VIDEO_SHA = "4b057fe0595c94d49c898556e36003c21810378b6de4d709f8bb3fa5ff90bad4"
SWIFT = Path(__file__).resolve().parent / "video.swift"
FIG = ROOT / "paper" / "figures"
SUPP = ROOT / "submission" / "supplementary"
SUPP_ZIP = ROOT / "submission" / "supplementary.zip"

# name -> (time in the recording, crop box (left, top, right, bottom) on the upright 2160x3840
# frame, output width, sha256 of the JPEG written)
PHOTOS = {
    "photo_ready.jpg": (15.5, (170, 470, 1730, 3710), 720,
                        "3db49a3ba77aa1920365b512694df1aba1ddf5d76237691a0291ab40f2d831a4"),
    "photo_answer.jpg": (181.5, (200, 160, 1890, 3650), 720,
                         "11b2eedb9364788f6d6b1103a2a6e0a62fa521c633434a744a56710ba2f855d7"),
    # the screen of the frame above, enlarged so its text can be read in print
    "photo_screen.jpg": (181.5, (440, 490, 1680, 1395), 1000,
                         "45d5bb5a48a4bb2f28c1d458a0fc710ba43007d523e322a1d3fc8bfd74588f07"),
}
# name -> (start, end) in seconds of the recording; None is the whole take
CLIPS = {
    "calculator_full.mp4": None,
    "calculator_answer.mp4": (116.0, 183.0),    # the question entered at 121.2 s, answer done at 180.3 s
}


def sha256(p: Path) -> str:
    h = hashlib.sha256()
    with open(p, "rb") as f:
        for b in iter(lambda: f.read(1 << 20), b""):
            h.update(b)
    return h.hexdigest()


def swift(*args: str) -> str:
    r = subprocess.run(["swift", "-suppress-warnings", str(SWIFT), *args], capture_output=True, text=True)
    if r.returncode:
        sys.exit(f"video.swift {args[0]} failed: {r.stderr.strip()[-400:]}")
    return r.stdout


def source() -> Path:
    if not VIDEO.exists():
        sys.exit(f"{VIDEO} is missing (it is not in git; set MEDIA=<dir> to use another copy)")
    if sha256(VIDEO) != VIDEO_SHA:
        sys.exit(f"{VIDEO} is not the recording these figures were cut from (sha256 differs)")
    return VIDEO


def draw_photos(out: Path, video: Path) -> dict[str, str]:
    """Write every PHOTOS entry into `out`; return name -> sha256 of what was written."""
    from PIL import Image
    out.mkdir(parents=True, exist_ok=True)
    got = {}
    with tempfile.TemporaryDirectory() as tmp:
        for name, (t, box, width, _) in PHOTOS.items():
            frame = Path(tmp) / f"{name}.png"
            swift("frame", str(video), str(frame), str(t))
            im = Image.open(frame).convert("RGB").crop(box)
            im = im.resize((width, round(im.height * width / im.width)), Image.LANCZOS)
            im.save(out / name, "JPEG", quality=88, optimize=True)    # PIL writes no EXIF unless given one
            got[name] = sha256(out / name)
    return got


def make_videos(video: Path) -> None:
    SUPP.mkdir(parents=True, exist_ok=True)
    for name, span in CLIPS.items():
        dst = SUPP / name
        swift("transcode", str(video), str(dst), *([] if span is None else [str(span[0]), str(span[1])]))
        probe = swift("probe", str(dst))
        tracks = [l for l in probe.splitlines() if l.startswith("track")]
        if "metadata_items 0" not in probe or len(tracks) != 1 or "vide" not in tracks[0]:
            sys.exit(f"{name}: expected one video track and no metadata, got:\n{probe}")
        print(f"  {name}: {dst.stat().st_size / 1e6:.1f} MB, {probe.splitlines()[0]}, no audio, no metadata")
    readme = SUPP / "README.txt"
    with zipfile.ZipFile(SUPP_ZIP, "w", zipfile.ZIP_STORED) as z:     # H.264 does not deflate
        for name in ["README.txt", "calculator_full.mp4"]:
            z.write(SUPP / name if name != "README.txt" else readme, name)
    mb = SUPP_ZIP.stat().st_size / 1e6
    print(f"  {SUPP_ZIP.relative_to(ROOT)}: {mb:.1f} MB (TMLR allows 100)")
    if mb > 100:
        sys.exit("the supplementary ZIP is over TMLR's 100 MB")


def main() -> None:
    video = source()
    got = draw_photos(FIG, video)
    for name, digest in got.items():
        want = PHOTOS[name][3]
        print(f"  paper/figures/{name}: {'matches its recorded hash' if digest == want else 'NEW ' + digest}")
    if "--video" in sys.argv:
        make_videos(video)


if __name__ == "__main__":
    main()
