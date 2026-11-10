#!/usr/bin/env bash
# Regenerate every render golden from the current build. Use this
# when you intentionally changed something that shifts the render
# (new feature, updated font, renderer refactor) and want to lock
# in the new look.
#
# WARNING: this OVERWRITES the existing goldens. If you're not sure
# whether the change was intentional, leave the goldens alone, run
# `ctest`, inspect the diffs, and only call this script after a
# human has confirmed the new output is correct.
#
# Usage: scripts/capture-render-goldens.sh [build-dir]
#   build-dir defaults to ./build.

set -euo pipefail

BUILD_DIR="${1:-build}"
REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CORPUS="${REPO_ROOT}/fixtures/bbm-corpus"
GOLDENS="${REPO_ROOT}/fixtures/render-goldens"
PARTS="${REPO_ROOT}/parts/BlueBrickParts/parts"

TESTS_BIN="${BUILD_DIR}/tests/rendering/bld_rendering_tests"
if [[ ! -x "${TESTS_BIN}" ]]; then
    echo "bld_rendering_tests not built at ${TESTS_BIN}. Run: cmake --build ${BUILD_DIR} --target bld_rendering_tests" >&2
    exit 1
fi

# The packs the corpus uses beyond the bundled library (TrixBrix in
# tight-corner, BrickTracks in Fordyce), as CI downloads them:
# BLD_RENDER_GOLDENS_PARTS=<TrixBrix dir>:<BrickTracks dir>. The test
# refuses to capture a map with missing parts.
if [[ -z "${BLD_RENDER_GOLDENS_PARTS:-}" ]]; then
    echo "Set BLD_RENDER_GOLDENS_PARTS to the TrixBrix and BrickTracks folders (see .github/workflows/build.yml)." >&2
    exit 1
fi

# The same render the test compares, written over the goldens.
QT_QPA_PLATFORM=offscreen BLD_ENABLE_RENDER_GOLDENS=1 BLD_RENDER_GOLDENS_CAPTURE=1 \
    "${TESTS_BIN}" --gtest_filter='RenderGoldens.*'
echo
echo "Captured the goldens under ${GOLDENS}. Look at each one, then commit them."
