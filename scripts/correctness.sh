#!/usr/bin/env bash
set -euo pipefail

repo_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
result_dir=${RESULT_DIR:-"${repo_dir}/results"}
size=${CORRECTNESS_SIZE:-65}
iterations=${CORRECTNESS_ITERS:-500}
processes=${CORRECTNESS_PROCESSES:-4}

if [[ -x "${repo_dir}/.local/openmpi/bin/mpirun" ]]; then
    mpiexec_cmd=${MPIEXEC:-"${repo_dir}/.local/openmpi/bin/mpirun"}
else
    mpiexec_cmd=${MPIEXEC:-mpirun}
fi

mkdir -p "${result_dir}"
output="${result_dir}/correctness.csv"
"${repo_dir}/bin/jacobi_serial" --size "${size}" --fixed-iters "${iterations}" \
    --format csv > "${output}"
"${mpiexec_cmd}" --map-by core --bind-to core -np "${processes}" \
    "${repo_dir}/bin/jacobi_mpi_blocking" --size "${size}" \
    --fixed-iters "${iterations}" --format csv | tail -n 1 >> "${output}"
"${mpiexec_cmd}" --map-by core --bind-to core -np "${processes}" \
    "${repo_dir}/bin/jacobi_mpi_overlap" --size "${size}" \
    --fixed-iters "${iterations}" --format csv | tail -n 1 >> "${output}"

python3 - "${output}" <<'PY'
import csv
import math
import sys

with open(sys.argv[1], newline="", encoding="utf-8") as handle:
    rows = list(csv.DictReader(handle))
baseline = rows[0]
print("version,residual_difference,l2_error_difference,checksum_difference")
for row in rows:
    differences = [abs(float(row[field]) - float(baseline[field])) for field in
                   ("residual", "relative_l2_error", "checksum")]
    print(f'{row["version"]},' + ",".join(f"{value:.17g}" for value in differences))
    if any(not math.isfinite(value) for value in differences):
        raise SystemExit("正确性结果包含非有限值")
PY
