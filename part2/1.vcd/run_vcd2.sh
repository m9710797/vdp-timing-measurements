#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
out_dir="${script_dir}/rw"

mkdir -p "${out_dir}"

shopt -s nullglob
for infile in "${script_dir}"/*.vcd; do
    base_name="$(basename "$infile")"
    out_name="${base_name%.vcd}.txt"
    output_file="${out_dir}/${out_name}"
    echo "Processing ${base_name} -> ${out_name}"
    "${script_dir}/vcd2" "$infile" > "$output_file"
done
