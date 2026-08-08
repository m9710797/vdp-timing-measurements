#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")"

for path in *.txt; do
    if [[ ! -f "$path" ]]; then
        continue
    fi
    if ! ./process "$path" >/tmp/process.out 2>/tmp/process.err; then
        echo "FAILED: $path" >&2
        cat /tmp/process.err >&2
        echo "--- stdout ---" >&2
        cat /tmp/process.out >&2
        exit 1
    fi
done

echo "all .txt files processed successfully"
