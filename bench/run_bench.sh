#!/bin/sh
set -eu

iterations="${1:-100}"
binary="${2:-./build/bench/bench_kason}"
cases="${3:-flat nested lookup struct struct64 struct64-sparse}"
binary_dir="$(dirname "$binary")"
first=1
all_parsers="kason-range kason-selected kason-schema kason-schema-fast kason-stream simdjson cjson json-c jansson yyjson jsmn parson json-glib"
available_builtin="$("$binary" --list-parsers)"

parser_binary()
{
	case "$1" in
		json-c) printf '%s/bench_json_c\n' "$binary_dir" ;;
		jansson) printf '%s/bench_jansson\n' "$binary_dir" ;;
		yyjson) printf '%s/bench_yyjson\n' "$binary_dir" ;;
		jsmn) printf '%s/bench_jsmn\n' "$binary_dir" ;;
		parson) printf '%s/bench_parson\n' "$binary_dir" ;;
		*) printf '%s\n' "$binary" ;;
	esac
}

parser_available()
{
	parser="$1"
	case " $available_builtin " in
		*" $parser "*) return 0 ;;
	esac
	return 1
}

for parser in $all_parsers; do
	if ! parser_available "$parser"; then
		printf 'benchmark skipped: %s (library not available)\n' "$parser" >&2
	fi
done

for case_name in $cases; do
	if [ "$case_name" = lookup ]; then
		parsers="kason-range kason-selected simdjson cjson json-c jansson yyjson jsmn parson json-glib"
	elif [ "$case_name" = struct ]; then
		parsers="kason-schema kason-schema-fast simdjson cjson json-c jansson yyjson jsmn parson json-glib"
	elif [ "$case_name" = struct64 ] || [ "$case_name" = struct64-sparse ]; then
		parsers="kason-schema kason-schema-fast jsmn"
	else
		parsers="kason-range kason-stream simdjson cjson json-c jansson yyjson jsmn parson json-glib"
	fi
	for parser in $parsers; do
		if ! parser_available "$parser"; then
			continue
		fi
		parser_binary="$(parser_binary "$parser")"
		output="$("$parser_binary" "$parser" "$case_name" "$iterations")"
		if [ "$first" -eq 1 ]; then
			printf "%s\n" "$output"
			first=0
		else
			printf "%s\n" "$output" | sed '1d'
		fi
	done
done
