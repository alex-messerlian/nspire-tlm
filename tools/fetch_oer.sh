#!/usr/bin/env bash
# Fetch the twelve OpenStax books that corpus/raw holds.
#
# WHY THIS EXISTS. corpus/raw was 5.1 GB of manually-cloned repositories with no script recording
# what they were or where they came from. When the directory was destroyed, that absence is what
# turned a two-command fix into an unrecoverable loss: the inventory existed only as a table in
# docs/CORPUS_MEASURED.md and three hardcoded names in corpus/tokenizer_study.py.
#
# A bulk input with no fetch script is a single point of failure regardless of how it was obtained.
#
# --depth 1: the miners read working-tree .cnxml files and never the history. The original 5.1 GB
# was mostly git history nothing in this project reads.
#
# Two directory names deliberately differ from the upstream repo name, because code already refers
# to them: corpus/tokenizer_study.py names `osbooks-college-physics`, and the directory layout is
# what corpus/*.py rglob over. The mapping is explicit here rather than implied by a rename.
set -euo pipefail
cd "$(git rev-parse --show-toplevel)"
DEST=corpus/raw
mkdir -p "$DEST"

# title (as recorded in docs/CORPUS_MEASURED.md) | upstream repo | directory name
BOOKS=(
  "University Physics 1-3|osbooks-university-physics-bundle|osbooks-university-physics-bundle"
  "College Physics 2e|osbooks-college-physics-bundle|osbooks-college-physics"
  "Prealgebra / Elementary Algebra|osbooks-prealgebra-bundle|osbooks-prealgebra-bundle"
  "Astronomy|osbooks-astronomy|osbooks-astronomy"
  "Introductory Statistics|osbooks-introductory-statistics-bundle|osbooks-introductory-statistics-bundle"
  "Chemistry|osbooks-chemistry-bundle|osbooks-chemistry-bundle"
  "College Algebra|osbooks-college-algebra-bundle|osbooks-college-algebra-bundle"
  "Calculus 1-3|osbooks-calculus-bundle|osbooks-calculus-bundle"
  "Precalculo (es)|osbooks-precalculo|osbooks-precalculo"
  "Algebra 1|osbooks-algebra-1|osbooks-algebra-1"
  "Physics (HS)|osbooks-physics|osbooks-physics"
  "Contemporary Mathematics|osbooks-contemporary-mathematics|osbooks-contemporary-mathematics"
)

for entry in "${BOOKS[@]}"; do
  IFS='|' read -r title repo dir <<< "$entry"
  if [ -d "$DEST/$dir/.git" ]; then
    echo "  present  $dir"
    continue
  fi
  echo "  cloning  $dir   ($title)"
  git clone --depth 1 -q "https://github.com/openstax/$repo.git" "$DEST/$dir" \
    || { echo "  FAILED   $repo -- continuing so one dead repo does not block the rest"; continue; }
done

echo
n=$(find "$DEST" -name '*.cnxml' | wc -l | tr -d ' ')
echo "  modules (.cnxml): $n     (docs/CORPUS_MEASURED.md records 2,987)"
du -sh "$DEST" | awk '{print "  size on disk:",$1}'
