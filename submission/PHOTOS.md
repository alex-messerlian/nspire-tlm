# Photographs and video of the calculator

What was captured on 27 September 2026, where each file is, and what each may be used for. The originals are in
`media/` at the repository root, which is not in git.

## The video: the record

`media/video/IMG_7083.mov`: 2,074,287,547 bytes, SHA-256 `4b057fe0595c94d49c898556e36003c21810378b6de4d709f8bb3fa5ff90bad4`.
Filmed by the author on a phone: 4K HEVC at 60 frames per second, 199.9 s, no audio track (muted when filmed). One
continuous take, unedited, from just after a reset to power-off, with the calculator on battery and no cable attached
(the battery symbol shows at 0:15, and no cable is in frame).

**Never share the original.** It records where it was filmed (`com.apple.quicktime.location.ISO6709`) and the phone
model. Share only the re-encoded copies below, which carry no metadata at all.

What it shows, with times read from its frames:

| time | on the screen |
|---|---|
| 0:00 | the home screen |
| 0:05 | the chattlm folder in My Documents |
| 0:10 | ChatTLM Setup; it installs the loader and closes |
| 0:15 | the home screen with "ChatTLM is ready" |
| 0:20 to 0:26 | ChatTLM opened from the chattlm folder |
| 0:27 to 2:01 | the question of the paper's Figure 1 typed on the keypad |
| 2:01.2 | the question entered (the screen changes between 121.0 and 121.5 s) |
| 2:01 to 2:36 | "Reading Motionally induced emf", then a percentage: the app chose the relation and processes the prompt |
| 2:37 | "Thinking": the model writes its tool call (between 156 and 157 s) |
| 2:47 | "Got 0.0150822": the call has run (between 166 and 167 s) |
| 2:51 | "Writing the answer": the answer appears as it is generated (between 170 and 171 s) |
| 3:00.3 | the finished answer (between 180.0 and 180.5 s) |
| 3:15 to 3:19 | back to My Documents and the home screen, then switched off |

So the answer was complete **about 59 s after the question was entered** (between 58.5 and 59.5 s). That time includes
choosing the relation, building the prompt and tokenizing it, which the paper's Figure 1 (52.0 s, a logged turn with
the relation supplied) does not. The answer, `0.01508 V. From epsilon=B*l*v.` (shown with an ε), is exactly what the
host replay of the calculator's code gives for the same question (`build/autoasm`, then `build/int8gen`).

**Derived files** (`tools/paper/make_media.py`):

- `paper/figures/photo_ready.jpg`, `photo_answer.jpg`, `photo_screen.jpg`: the frames at 15.5 s and 181.5 s, and the
  screen of the second enlarged; cropped and scaled, not retouched. They are the paper's Appendix B (Figure 6).
  `check_paper.py` checks each against its recorded hash, and cuts them again from the recording when it is present.
- `submission/supplementary/calculator_full.mp4` (62.8 MB, the whole take) and `calculator_answer.mp4` (21.2 MB,
  116.0 to 183.0 s, one continuous span from the end of typing to the finished answer): H.264, 1080 x 1920, 30 frames
  per second, no audio, no metadata. Not in git; rebuild with `make_media.py --video`.
- `submission/supplementary.zip` (62.8 MB): the full video and `submission/supplementary/README.txt`, the upload for
  TMLR's supplementary material.

## If you film again

- **Battery, cable out.** With the cable in, the calculator runs at 288 MHz instead of 396, and every time shown would
  be wrong for the paper.
- **Nothing identifying.** The review copy is anonymous: no name, school or label on the calculator or in the
  background, no face or voice. Phones record the place in the file; `make_media.py` leaves all metadata out.
- **Do not enhance.** An enhanced image is not a record. If a frame needs to look better, film again.
- Questions whose answers were checked on the host, with what the calculator prints:

      Take l = 0.057, v = 1.35, B = 0.196. What was the motionally induced emf?
          0.01508 V. From epsilon=B*l*v.
      Given V = 12, I = 2, find R
          That gives 6 ohm. Directly from R=V/I.
      Who wrote Hamlet?
          I cannot answer that: no record matches this question.

  Avoid `Given m = 2, a = 3, find F`: its number is right, but the model writes the unit as `1/s` instead of N.
