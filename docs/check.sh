#!/bin/sh

set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
source_dir=$(CDPATH= cd -- "$script_dir/.." && pwd)

"$script_dir/check-links.sh"
KaSON_DOCS_BUILD_DIR=${KASON_DOCS_CHECK_BUILD_DIR:-"$source_dir/build/docs-check"} \
    "$script_dir/generate.sh"
