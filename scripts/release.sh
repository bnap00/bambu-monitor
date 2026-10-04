#!/bin/sh
# Local release (no CI): builds, tags, creates the GitHub release and publishes the
# browser flasher to the gh-pages branch.
#   scripts/release.sh 0.1.0
set -e
VER=${1:?usage: scripts/release.sh <version, e.g. 0.1.0>}
TAG=v$VER
cd "$(dirname "$0")/.."

[ -z "$(git status --porcelain)" ] || { echo "working tree not clean"; exit 1; }
git rev-parse "$TAG" >/dev/null 2>&1 && { echo "$TAG already exists"; exit 1; }
REPO=$(gh repo view --json nameWithOwner -q .nameWithOwner)
OWNER=${REPO%/*}
NAME=${REPO#*/}

PIO=$(command -v pio || echo "$HOME/.local/bin/pio")

echo "== build $VER"
FW_VERSION=$VER "$PIO" run -e t-encoder-pro
test/parse/run.sh
rm -rf dist && mkdir -p dist
./scripts/merge_factory.sh t-encoder-pro dist/bambu-monitor-$VER-factory.bin
cp .pio/build/t-encoder-pro/firmware.bin dist/bambu-monitor-$VER-ota.bin
(cd dist && sha256sum *.bin > SHA256SUMS)

echo "== tag + release"
git tag -a "$TAG" -m "Bambu Monitor $VER"
git push origin "$TAG"
# release notes = this version's section of CHANGELOG.md
awk -v v="$VER" '$0 ~ "^## \\["v"\\]" {p=1; next} /^## \[/ {p=0} p' CHANGELOG.md > dist/NOTES.md
printf '\n**Install:** https://%s.github.io/%s/ (browser flasher)  \n**Update:** upload `bambu-monitor-%s-ota.bin` at `http://<device>/update`\n' \
  "$OWNER" "$NAME" "$VER" >> dist/NOTES.md
gh release create "$TAG" dist/*.bin dist/SHA256SUMS --title "$TAG" --notes-file dist/NOTES.md

echo "== web flasher (gh-pages)"
SITE=$(mktemp -d)
if git ls-remote --exit-code --heads origin gh-pages >/dev/null 2>&1; then
  git worktree add "$SITE" origin/gh-pages --detach
else
  git worktree add --detach "$SITE"
  (cd "$SITE" && git checkout --orphan gh-pages && git rm -rfq .)
fi
cp docs/flasher/index.html "$SITE/"
cp dist/bambu-monitor-$VER-factory.bin "$SITE/firmware-factory.bin"
sed "s/@VERSION@/$VER/" docs/flasher/manifest.template.json > "$SITE/manifest.json"
touch "$SITE/.nojekyll"
(cd "$SITE" && git add -A && git commit -qm "Flasher for $TAG" && git push origin HEAD:gh-pages)
git worktree remove --force "$SITE"

echo "done: https://github.com/$REPO/releases/tag/$TAG  ·  https://$OWNER.github.io/$NAME/"
