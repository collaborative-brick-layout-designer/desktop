#!/usr/bin/env bash
# Run CI's "Lint changed lines" check here, before you push: clang-tidy's
# bug-finding checks (blocking) and clang-format (advisory) on the lines
# your branch changes.
#
#   scripts/lint.sh [base ref, default origin/main] [build dir, default build-lint]
#
# clang-tidy needs a build folder configured with clang. The first run makes
# one (the "lint" preset in CMakePresets.json, in build-lint/) and builds
# the app's libraries in it, as CI's lint job does: clang-tidy reads the
# headers Qt generates during the build. That takes several minutes
# (ccache is used when it's installed). Later runs build only what changed.
#
# Needs clang, clang-tidy, clang-format and ninja. Environment:
#   LINT_JOBS   how many compilers and clang-tidy processes run at once
#               (default: all cores)
set -euo pipefail
base=${1:-origin/main}
cd "$(git rev-parse --show-toplevel)"
build=${2:-build-lint}

for tool in clang clang++ clang-tidy ninja; do
    command -v "$tool" >/dev/null || { echo "scripts/lint.sh needs $tool on PATH" >&2; exit 2; }
done
git rev-parse --verify --quiet "$base^{commit}" >/dev/null || {
    echo "No such base to compare with: $base (try: git fetch origin)" >&2; exit 2; }

jobs=${LINT_JOBS:-$(nproc)}
if [ ! -f "$build/CMakeCache.txt" ]; then
    echo "== configuring $build with clang (first run)"
    launcher=()
    if command -v ccache >/dev/null; then
        launcher=(-DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache)
    fi
    cmake --preset lint -B "$build" "${launcher[@]}" >"$build.configure.log" 2>&1 || {
        cat "$build.configure.log" >&2; exit 2; }
    rm -f "$build.configure.log"
elif ! grep -q '^CMAKE_CXX_COMPILER:[A-Z]*=.*clang' "$build/CMakeCache.txt"; then
    echo "$build was not configured with clang; clang-tidy can't read its compile commands." >&2
    echo "Pass another folder, or remove it and run this again." >&2
    exit 2
fi

# The headers Qt generates (moc, uic) for the app's own code: clang-tidy has
# to find them. Making them builds the libraries they come from; the tests
# aren't linted (scripts/ci/lint-changes.sh) and aren't built.
echo "== building $build"
mapfile -t autogen < <(ninja -C "$build" -t targets all | sed -n 's/^\(bld_[A-Za-z0-9_]*_autogen\): .*/\1/p' | grep -v '_tests_autogen$')
cmake --build "$build" --parallel "$jobs" --target "${autogen[@]}" >"$build/lint-build.log" 2>&1 || {
    tail -n 40 "$build/lint-build.log" >&2; exit 2; }

LINT_JOBS=$jobs exec scripts/ci/lint-changes.sh "$base" "$build"
