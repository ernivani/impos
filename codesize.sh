#!/bin/bash
# codesize.sh — Generates a recursive line-count report for all .c/.h files
# Usage: ./codesize.sh > report.out

ROOT="$(cd "$(dirname "$0")" && pwd)"
DIRS=("$ROOT/kernel" "$ROOT/libc")

echo "=============================================="
echo "  ImposOS Code Size Report"
echo "  Generated: $(date '+%Y-%m-%d %H:%M:%S')"
echo "=============================================="
echo ""

# --- Grand total ---
total=$(find "${DIRS[@]}" \( -name "*.c" -o -name "*.h" \) -print0 | xargs -0 wc -l | tail -1 | awk '{print $1}')
nfiles=$(find "${DIRS[@]}" \( -name "*.c" -o -name "*.h" \) | wc -l)
printf "  TOTAL: %'d lines across %d files\n\n" "$total" "$nfiles"

# --- All files sorted descending ---
echo "=============================================="
echo "  ALL FILES (descending by line count)"
echo "=============================================="
printf "  %-7s  %s\n" "LINES" "FILE"
echo "  ------- -----------------------------------------------"

find "${DIRS[@]}" \( -name "*.c" -o -name "*.h" \) -print0 \
  | xargs -0 wc -l \
  | grep -v ' total$' \
  | sort -rn \
  | while read -r lines path; do
      rel="${path#$ROOT/}"
      printf "  %7d  %s\n" "$lines" "$rel"
    done

echo ""

# --- Per-directory summary ---
echo "=============================================="
echo "  PER-DIRECTORY SUMMARY (descending)"
echo "=============================================="
printf "  %-7s  %-5s  %s\n" "LINES" "FILES" "DIRECTORY"
echo "  ------- -----  -----------------------------------------------"

find "${DIRS[@]}" \( -name "*.c" -o -name "*.h" \) -printf '%h\n' \
  | sort -u \
  | while read -r dir; do
      dlines=$(find "$dir" -maxdepth 1 \( -name "*.c" -o -name "*.h" \) -print0 | xargs -0 wc -l 2>/dev/null | tail -1 | awk '{print $1}')
      dfiles=$(find "$dir" -maxdepth 1 \( -name "*.c" -o -name "*.h" \) | wc -l)
      rel="${dir#$ROOT/}"
      [ "$dfiles" -gt 0 ] && printf "%7d %5d  %s\n" "$dlines" "$dfiles" "$rel"
    done \
  | sort -rn

echo ""
echo "=============================================="
echo "  .c vs .h BREAKDOWN"
echo "=============================================="
c_lines=$(find "${DIRS[@]}" -name "*.c" -print0 | xargs -0 wc -l | tail -1 | awk '{print $1}')
h_lines=$(find "${DIRS[@]}" -name "*.h" -print0 | xargs -0 wc -l | tail -1 | awk '{print $1}')
c_files=$(find "${DIRS[@]}" -name "*.c" | wc -l)
h_files=$(find "${DIRS[@]}" -name "*.h" | wc -l)
printf "  .c : %'7d lines  (%d files)\n" "$c_lines" "$c_files"
printf "  .h : %'7d lines  (%d files)\n" "$h_lines" "$h_files"
echo ""
echo "=== END ==="
