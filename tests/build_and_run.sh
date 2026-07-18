#!/bin/sh
set -eu

script_dir="$(CDPATH= cd "$(dirname "$0")" && pwd)"
project_dir="$(dirname "$script_dir")"
build_dir="${KASON_TEST_BUILD_DIR:-$project_dir/build/test}"
config="${KASON_TEST_CONFIG:-Debug}"

cmake -S "$project_dir" -B "$build_dir" \
	-DCMAKE_BUILD_TYPE="$config" \
	-DBUILD_TESTING=ON \
	-DKASON_BUILD_TESTS=ON \
	-DKASON_BUILD_EXAMPLES=OFF \
	-DKASON_BUILD_BENCHMARK=OFF \
	-DKASON_BUILD_FUZZER=OFF \
	"$@"
cmake --build "$build_dir" --config "$config" --parallel
exec ctest --test-dir "$build_dir" -C "$config" --output-on-failure
