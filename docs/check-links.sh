#!/bin/sh

set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
source_dir=$(CDPATH= cd -- "$script_dir/.." && pwd)
# Pipeline loops run in subshells on POSIX shells, so collect failures in a
# report rather than trying to update a status variable inside the loop.
report=${TMPDIR:-/tmp}/kason-doc-links.$$
trap 'rm -f "$report"' EXIT HUP INT TERM
: > "$report"
find "$source_dir" -maxdepth 2 -type f -name '*.md' -print | while IFS= read -r file; do
    grep -Eo '\]\([^)]+' "$file" 2>/dev/null | sed 's/^](//' | while IFS= read -r target; do
        case "$target" in
            ''|'#'*|http://*|https://*|mailto:*) continue ;;
        esac
        target_path=${target%%#*}
        if [ ! -e "$(dirname "$file")/$target_path" ]; then
            echo "broken link in ${file#"$source_dir/"}: $target" >> "$report"
        fi
    done
done

if [ -s "$report" ]; then
    cat "$report" >&2
    exit 1
fi

echo "Markdown links are valid"
