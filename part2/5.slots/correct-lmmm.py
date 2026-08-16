import argparse
import re
import sys


def find_column_index(char_pos, column_clusters, max_distance=3):
    """Finds which column cluster a character position belongs to."""
    for idx, cluster_pos in enumerate(column_clusters):
        if abs(char_pos - cluster_pos) <= max_distance:
            return idx
    return None


def transform_file(input_path, output_path):
    with open(input_path, "r", encoding="utf-8") as f:
        buffer = f.read()

    lines = buffer.splitlines(keepends=True)

    # Regex matching: 3-char operation tag (e.g. R.., W.d), space, 0x + 5 hex digits
    pattern = re.compile(r"([RW][\w\.]{2})\s+(0x[0-9a-fA-F]{5})")

    tokens = []
    line_start_buffer_offset = 0

    # 1. Parse all data tokens and their exact character positions in the buffer
    for line_idx, line in enumerate(lines):
        for m in pattern.finditer(line):
            op_code = m.group(1)
            addr = m.group(2)
            start_in_line = m.start(1)
            buffer_offset = line_start_buffer_offset + start_in_line

            # Extract timestamp prefix for descriptive error messages
            ts_match = re.match(r"\s*(\d+):", line)
            timestamp = ts_match.group(1) if ts_match else f"line {line_idx + 1}"

            tokens.append(
                {
                    "op": op_code,
                    "addr": addr,
                    "line_idx": line_idx,
                    "start_in_line": start_in_line,
                    "buffer_offset": buffer_offset,
                    "timestamp": timestamp,
                }
            )
        line_start_buffer_offset += len(line)

    if not tokens:
        print("No valid data tokens found in file.")
        return

    # 2. Automatically cluster horizontal character positions into data columns
    raw_positions = sorted(list(set(t["start_in_line"] for t in tokens)))
    clusters = []
    for pos in raw_positions:
        matched = False
        for c in clusters:
            if abs(pos - c[0]) <= 3:
                c.append(pos)
                matched = True
                break
        if not matched:
            clusters.append([pos])

    column_clusters = sorted([sum(c) / len(c) for c in clusters])

    for t in tokens:
        t["col_idx"] = find_column_index(t["start_in_line"], column_clusters)

    # 3. Sort tokens by global reading order: Col 1 (top-to-bottom), Col 2 (top-to-bottom), etc.
    tokens_sorted = sorted(tokens, key=lambda x: (x["col_idx"], x["line_idx"]))

    # 4. Validate W.d intervals and map buffer replacements
    w_indices = [i for i, t in enumerate(tokens_sorted) if t["op"].startswith("W")]
    replacements = {}

    if w_indices:
        # Boundary 1: Tokens before the first W.d
        first_w = w_indices[0]
        num_prefix = first_w
        if num_prefix > 2:
            t_w = tokens_sorted[first_w]
            raise ValueError(
                f"Error: Found {num_prefix} 'R' tokens before the first 'W.d' marker at "
                f"timestamp {t_w['timestamp']} (col {t_w['col_idx'] + 1}). Max allowed is 2."
            )
        elif num_prefix == 2:
            replacements[tokens_sorted[0]["buffer_offset"]] = "R.s"
            replacements[tokens_sorted[1]["buffer_offset"]] = "R.d"
        elif num_prefix == 1:
            replacements[tokens_sorted[0]["buffer_offset"]] = "R.d"

        # Main Loop: Between consecutive W.d markers
        for i in range(len(w_indices) - 1):
            w_curr = w_indices[i]
            w_next = w_indices[i + 1]
            between_r_indices = list(range(w_curr + 1, w_next))
            num_between = len(between_r_indices)

            if num_between != 2:
                t_curr = tokens_sorted[w_curr]
                t_next = tokens_sorted[w_next]
                raise ValueError(
                    f"Error: Expected exactly 2 'R' tokens between 'W.d' at timestamp {t_curr['timestamp']} "
                    f"(col {t_curr['col_idx'] + 1}) and 'W.d' at timestamp {t_next['timestamp']} "
                    f"(col {t_next['col_idx'] + 1}), but found {num_between}."
                )

            replacements[tokens_sorted[between_r_indices[0]]["buffer_offset"]] = "R.s"
            replacements[tokens_sorted[between_r_indices[1]]["buffer_offset"]] = "R.d"

        # Boundary 2: Tokens after the last W.d
        last_w = w_indices[-1]
        suffix_r_indices = list(range(last_w + 1, len(tokens_sorted)))
        num_suffix = len(suffix_r_indices)
        if num_suffix > 2:
            t_w = tokens_sorted[last_w]
            raise ValueError(
                f"Error: Found {num_suffix} 'R' tokens after the last 'W.d' marker at "
                f"timestamp {t_w['timestamp']} (col {t_w['col_idx'] + 1}). Max allowed is 2."
            )
        elif num_suffix == 2:
            replacements[tokens_sorted[suffix_r_indices[0]]["buffer_offset"]] = (
                "R.s"
            )
            replacements[tokens_sorted[suffix_r_indices[1]]["buffer_offset"]] = (
                "R.d"
            )
        elif num_suffix == 1:
            replacements[tokens_sorted[suffix_r_indices[0]]["buffer_offset"]] = (
                "R.s"
            )
    else:
        # File with no W.d markers
        num_r = len(tokens_sorted)
        if num_r > 2:
            raise ValueError(
                f"Error: No 'W.d' markers found and sequence contains {num_r} 'R' tokens (max allowed without W.d is 2)."
            )
        elif num_r == 2:
            replacements[tokens_sorted[0]["buffer_offset"]] = "R.s"
            replacements[tokens_sorted[1]["buffer_offset"]] = "R.d"
        elif num_r == 1:
            replacements[tokens_sorted[0]["buffer_offset"]] = "R.s"

    # 5. Direct slice overwriting in the buffer (buffer size strictly preserved)
    buffer_list = list(buffer)
    for offset, new_tag in replacements.items():
        buffer_list[offset : offset + 3] = list(new_tag)

    output_buffer = "".join(buffer_list)

    assert len(output_buffer) == len(
        buffer
    ), "Buffer size mismatch safety check failed!"

    with open(output_path, "w", encoding="utf-8") as f:
        f.write(output_buffer)

    print(
        f"Success: Processed {len(tokens)} tokens across {len(column_clusters)} columns."
    )
    print(
        f"Updated {len(replacements)} read markers. Output saved to '{output_path}'."
    )


if __name__ == "__main__":
    parser = argparse.ArgumentParser(
        description="Fix R.s / R.d operational tags in trace text files."
    )
    parser.add_argument(
        "input", nargs="?", help="Path to input file", default="input.txt"
    )
    parser.add_argument(
        "output", nargs="?", help="Path to output file", default="output.txt"
    )

    args = parser.parse_args()
    try:
        transform_file(args.input, args.output)
    except Exception as e:
        print(f"\n{e}", file=sys.stderr)
        sys.exit(1)
