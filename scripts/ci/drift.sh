#!/usr/bin/env bash
# What has moved on since it was pinned, and what the Rust advisory database
# says about the crates the app links. Writes a Markdown report to stdout;
# exits 0 when everything is current, 1 when something needs a look.
# The weekly Drift workflow (.github/workflows/drift.yml) runs it and keeps
# one issue open while there is something to report.
#
#   GH_TOKEN=... scripts/ci/drift.sh [work dir]
set -uo pipefail
cd "$(git rev-parse --show-toplevel)"
work=${1:-$(mktemp -d)}
mkdir -p "$work"
sync=src/sync/CMakeLists.txt
items=()

pinned() { sed -nE "s#.*$1#\\1#p" "$sync" | head -1; }
newer() { [ "$1" != "$2" ] && [ "$(printf '%s\n%s\n' "$1" "$2" | sort -V | tail -1)" = "$1" ]; }

# yffi (the crate): crates.io's newest stable version.
yffi=$(pinned 'crates/yffi/yffi-([0-9.]+)\.crate')
yffi_latest=$(curl -fsSL -A 'cbld-drift (github.com/collaborative-brick-layout-designer/desktop)' \
    https://crates.io/api/v1/crates/yffi | jq -r '.crate.max_stable_version')
newer "$yffi_latest" "$yffi" && items+=("**yffi** $yffi is pinned; $yffi_latest is out (bump the URL, hash and third_party/yrs/libyrs.h together).")

# GitHub releases: QtKeychain and Corrosion.
for spec in "qtkeychain|frankosterfeld/qtkeychain|qtkeychain/archive/refs/tags/([0-9.]+)\.tar\.gz" \
            "Corrosion|corrosion-rs/corrosion|corrosion/archive/refs/tags/v([0-9.]+)\.tar\.gz"; do
    IFS='|' read -r name repo pattern <<< "$spec"
    have=$(pinned "$pattern")
    latest=$(gh api "repos/$repo/releases/latest" --jq .tag_name 2>/dev/null | sed 's/^v//')
    [ -n "$latest" ] && newer "$latest" "$have" && items+=("**$name** $have is pinned; $latest is out.")
done

# The BlueBrickParts submodule against its upstream.
sub_sha=$(git ls-tree HEAD parts/BlueBrickParts | awk '{print $3}')
up_sha=$(git ls-remote https://github.com/Lswbanban/BlueBrickParts HEAD | awk '{print $1}')
if [ -n "$up_sha" ] && [ "$sub_sha" != "$up_sha" ]; then
    behind=$(gh api "repos/Lswbanban/BlueBrickParts/compare/$sub_sha...$up_sha" --jq .ahead_by 2>/dev/null || echo '?')
    items+=("**BlueBrickParts** is ${behind} upstream commit(s) behind (${sub_sha:0:8} here, ${up_sha:0:8} upstream). The web's \`BLUEBRICK_PARTS_SHA\` should move with it.")
fi

# The help texts: the web commit pinned here against the web's main.
pin=$( { tr -d '[:space:]' < src/ui/help/web-help-texts.commit; } 2>/dev/null)
if [ -n "$pin" ]; then
    path=apps/web/src/help/help-texts.json
    at_pin=$(gh api "repos/collaborative-brick-layout-designer/web/contents/$path?ref=$pin" --jq .sha 2>/dev/null)
    at_main=$(gh api "repos/collaborative-brick-layout-designer/web/contents/$path?ref=main" --jq .sha 2>/dev/null)
    [ -n "$at_main" ] && [ "$at_pin" != "$at_main" ] && items+=("**Help texts**: the web's \`help-texts.json\` changed after the pinned commit ${pin:0:8}. Copy it to \`src/ui/help/help-texts.json\`, run \`scripts/gen-help-strings.py\` and bump \`src/ui/help/web-help-texts.commit\`.")
fi

# cargo audit on the Cargo.lock yffi ships (the crates the app links).
url=$(sed -nE 's#.*URL +(https://static.crates.io/crates/yffi/[^ ]+\.crate).*#\1#p' "$sync" | head -1)
audit=""
if curl -fsSL -o "$work/yffi.tar.gz" "$url" && tar -xzf "$work/yffi.tar.gz" -C "$work"; then
    lock=$(ls "$work"/yffi-*/Cargo.lock | head -1)
    if ! audit=$(cargo audit --file "$lock" --deny warnings 2>&1); then
        items+=("**cargo audit** reports advisories for yffi $yffi's Cargo.lock:"$'\n\n```\n'"$(echo "$audit" | grep -E '^(Crate|Version|Title|ID|Warning|Error|Solution):|^error|^warning' | head -60)"$'\n```')
    fi
else
    items+=("**cargo audit** didn't run: yffi's crate couldn't be downloaded from $url.")
fi

echo "Checked $(date -u +%Y-%m-%d): yffi $yffi, QtKeychain $(pinned 'qtkeychain/archive/refs/tags/([0-9.]+)\.tar\.gz'), BlueBrickParts ${sub_sha:0:8}, help texts at web ${pin:0:8}. Qt (\`6.12.*\`) and Rust (\`stable\`) float on purpose and aren't checked."
echo
if [ ${#items[@]} -eq 0 ]; then
    echo "Everything is current, and cargo audit found nothing."
    exit 0
fi
for i in "${items[@]}"; do echo "- $i"; echo; done
exit 1
