#!/bin/sh
set -eu

iterations="${1:-100}"
cases="${2:-flat nested lookup struct struct64 struct64-sparse}"
script_dir="$(CDPATH= cd "$(dirname "$0")" && pwd)"
project_dir="$(dirname "$script_dir")"
build_dir="${KASON_BENCH_BUILD_DIR:-$project_dir/build/bench}"

cmake -S "$project_dir" -B "$build_dir" \
	-DKaSON_BUILD_BENCHMARK=ON \
	-DKaSON_BUILD_EXAMPLES=OFF \
	-DCMAKE_BUILD_TYPE=Release
cmake --build "$build_dir" --target bench_kason --parallel

exec "$script_dir/run_bench.sh" \
	"$iterations" \
	"$build_dir/bench_kason" \
	"$cases"
