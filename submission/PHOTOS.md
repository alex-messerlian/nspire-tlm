# Photographs and video of the calculator

**Purpose.** To show a reader that the assistant really runs on an unmodified calculator, by itself, on battery. The
paper's numbers do not depend on them; they are evidence and illustration.

## Where they go

- **In the paper:** one or two photographs, as a short Appendix B ("The assistant on the calculator"). The main text is
  full at 12 pages; appendix pages do not count towards TMLR's length concern.
- **Next to the paper:** the video, as optional supplementary material for TMLR (a ZIP, up to 100 MB, anonymized), and
  later with the public code release.
- **In the folder:** originals in `media/` at the repository root (kept out of git: photos and video are large). The
  photographs used in the paper go, cropped and compressed (JPEG, about 300 KB each), into `paper/figures/`.

## Before you start

- **Battery, cable out.** Unplug the USB cable before running anything, and keep it out of the whole shot. With the
  cable in, the calculator runs at 288 MHz instead of 396, and every time shown would be wrong for the paper.
- **Nothing identifying.** The review copy is anonymous. No name, school or label on the calculator or in the
  background, no face or voice, no location.
- Good even light, no glare on the screen (tilt it slightly), plain background, phone held steady or propped.

## The shots

The expected outputs below come from running the calculator's own code on the computer, which reproduces the
calculator token for token.

1. **The whole calculator** on a plain surface, no cable, screen showing a finished answer from shot 2.
2. **The paper's Figure 1 question**, close enough to read the screen. Type:

       Take l = 0.057, v = 1.35, B = 0.196. What was the motionally induced emf?

   Expected answer: `0.01508 V. From epsilon=B*l*v.` It is the same turn the paper prints as Figure 1, so the photo
   and the figure match. About 30 s to the first output and 52 s to the end.
3. **A symbol-only question**, showing the selection rule of Section 7. Type:

       Given V = 12, I = 2, find R

   Expected answer: `That gives 6 ohm. Directly from R=V/I.`
4. **A decline.** Type:

       Who wrote Hamlet?

   Expected answer: `I cannot answer that: no record matches this question.`

Avoid `Given m = 2, a = 3, find F` for the photographs: its number is right, but the model writes the unit as `1/s`
instead of N.

## The video

One continuous take of shot 2, from typing the question to the finished answer, with the unplugged calculator in
view. Real time, no cuts: the wait is part of what it shows. Record it muted, since the review copy must stay
anonymous.
