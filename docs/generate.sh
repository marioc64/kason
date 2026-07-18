#!/bin/sh

set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
source_dir=$(CDPATH= cd -- "$script_dir/.." && pwd)
build_dir=${KaSON_DOCS_BUILD_DIR:-"$source_dir/build-docs"}

if ! command -v cmake >/dev/null 2>&1; then
    echo "error: CMake is required to generate the documentation" >&2
    exit 1
fi

if ! command -v doxygen >/dev/null 2>&1; then
    echo "error: Doxygen is required to generate the documentation" >&2
    echo "macOS:  brew install doxygen" >&2
    echo "Ubuntu: sudo apt install doxygen" >&2
    exit 1
fi

cmake -S "$source_dir" -B "$build_dir" \
    -DKaSON_BUILD_DOCS=ON \
    -DKaSON_BUILD_TESTS=OFF \
    -DKaSON_BUILD_EXAMPLES=OFF
cmake --build "$build_dir" --target docs

index="$build_dir/docs/html/index.html"
if [ ! -f "$index" ]; then
    echo "error: documentation build completed without producing $index" >&2
    exit 1
fi

echo "Documentation generated at: $index"
