#!/usr/bin/env bash
set -euo pipefail

repo_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
result_dir="${repo_dir}/results"
sizes=${ACCURACY_SIZES:-"32 64 128 256"}
tolerance=${ACCURACY_TOL:-1e-6}
max_iters=${ACCURACY_MAX_ITERS:-1000000}

mkdir -p "${result_dir}"
output="${result_dir}/accuracy.csv"
"${repo_dir}/bin/jacobi_serial" --size 8 --fixed-iters 1 --format csv | head -n 1 > "${output}"

for size in ${sizes}; do
    echo "[accuracy] N=${size}, tol=${tolerance}" >&2
    "${repo_dir}/bin/jacobi_serial" --size "${size}" --tol "${tolerance}" \
        --max-iters "${max_iters}" --format csv | tail -n 1 >> "${output}"
done

python3 - "${output}" <<'PY'
import csv
import math
import sys

with open(sys.argv[1], newline="", encoding="utf-8") as handle:
    rows = list(csv.DictReader(handle))
print("N,relative_l2_error,observed_order")
previous = None
for row in rows:
    error = float(row["relative_l2_error"])
    order = "-" if previous is None else f"{math.log2(previous / error):.6f}"
    print(f'{row["size"]},{error:.10e},{order}')
    previous = error
PY
