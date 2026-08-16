#!/usr/bin/env bash
set -euo pipefail

if [ "$#" -eq 0 ]; then
    echo "Usage: $0 <tmp_files>"
    echo "Example: $0 *.txt.tmp"
    exit 1
fi

commit_count=0

for tmp_file in "$@"; do
    [[ -f "$tmp_file" ]] || continue

    # Strip the .tmp suffix to retrieve the original filename
    orig_file="${tmp_file%.tmp}"

    mv -f "$tmp_file" "$orig_file"
    commit_count=$((commit_count + 1))
done

echo "Successfully overwrote $commit_count original file(s)."
