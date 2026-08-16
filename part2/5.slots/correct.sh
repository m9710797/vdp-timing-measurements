#!/usr/bin/env bash
set -euo pipefail

PYTHON_SCRIPT="correct-lmmm.py"

if [ "$#" -eq 0 ]; then
    echo "Usage: $0 <files_or_glob>"
    echo "Example: $0 *.txt"
    exit 1
fi

success_count=0
fail_count=0

for file in "$@"; do
    # Skip if file doesn't exist or is already a .tmp file
    [[ -f "$file" ]] || continue
    [[ "$file" == *.tmp ]] && continue

    out_file="${file}.tmp"

    if python3 "$PYTHON_SCRIPT" "$file" "$out_file"; then
        success_count=$((success_count + 1))
    else
        echo "[ERROR] Failed processing '$file'" >&2
        fail_count=$((fail_count + 1))
    fi
done

echo "----------------------------------------"
echo "Completed: $success_count succeeded, $fail_count failed."
