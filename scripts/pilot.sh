#!/usr/bin/env bash
set -euo pipefail

repo_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
result_dir=${RESULT_DIR:-"${repo_dir}/results"}
iterations=${PILOT_ITERS:-200}
sizes=${PILOT_SIZES:-"512 1024 2048 4096"}

mkdir -p "${result_dir}"
output="${result_dir}/pilot.csv"
"${repo_dir}/bin/jacobi_serial" --size 8 --fixed-iters 1 --format csv | head -n 1 > "${output}"

for size in ${sizes}; do
    echo "[pilot] N=${size}, iterations=${iterations}" >&2
    "${repo_dir}/bin/jacobi_serial" --size "${size}" --fixed-iters "${iterations}" \
        --format csv | tail -n 1 >> "${output}"
done

echo "Pilot 数据已写入 ${output}" >&2
echo "请选择单进程耗时约 2--30 秒的 N 作为正式 GRID_SIZE。" >&2
