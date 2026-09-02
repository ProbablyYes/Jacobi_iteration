#!/usr/bin/env bash
set -euo pipefail

repo_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
if [[ -x "${repo_dir}/.local/openmpi/bin/mpirun" ]]; then
    default_mpiexec="${repo_dir}/.local/openmpi/bin/mpirun"
else
    default_mpiexec=mpirun
fi
mpiexec_cmd=${MPIEXEC:-${default_mpiexec}}
temporary_dir=$(mktemp -d)
trap 'rm -rf "${temporary_dir}"' EXIT

expect_exit() {
    local expected=$1
    shift
    set +e
    "$@" >/dev/null 2>&1
    local actual=$?
    set -e
    if [[ ${actual} -ne ${expected} ]]; then
        echo "期望退出码 ${expected}，实际为 ${actual}: $*" >&2
        exit 1
    fi
}

echo "[test] 参数和退出状态"
expect_exit 64 "${repo_dir}/bin/jacobi_serial" --size 0
expect_exit 2 "${repo_dir}/bin/jacobi_serial" --size 16 --tol 1e-15 --max-iters 1
expect_exit 64 "${mpiexec_cmd}" -np 3 "${repo_dir}/bin/jacobi_mpi_blocking" --size 2

echo "[test] 三版本固定迭代数值一致性（含不均匀行划分）"
"${repo_dir}/bin/jacobi_serial" --size 17 --fixed-iters 40 --format csv > "${temporary_dir}/serial.csv"
"${mpiexec_cmd}" -np 3 "${repo_dir}/bin/jacobi_mpi_blocking" \
    --size 17 --fixed-iters 40 --format csv > "${temporary_dir}/blocking.csv"
"${mpiexec_cmd}" -np 3 "${repo_dir}/bin/jacobi_mpi_overlap" \
    --size 17 --fixed-iters 40 --format csv > "${temporary_dir}/overlap.csv"

python3 - "${temporary_dir}" <<'PY'
import csv
import math
import pathlib
import sys

root = pathlib.Path(sys.argv[1])
rows = {}
for name in ("serial", "blocking", "overlap"):
    with (root / f"{name}.csv").open(newline="", encoding="utf-8") as handle:
        rows[name] = next(csv.DictReader(handle))
for field in ("residual", "relative_l2_error", "checksum"):
    expected = float(rows["serial"][field])
    for name in ("blocking", "overlap"):
        actual = float(rows[name][field])
        if not math.isclose(actual, expected, rel_tol=2e-12, abs_tol=2e-12):
            raise SystemExit(f"{name} {field}: {actual} != {expected}")
PY

echo "[test] 收敛与轨迹输出"
"${mpiexec_cmd}" -np 2 "${repo_dir}/bin/jacobi_mpi_overlap" \
    --size 16 --tol 1e-4 --max-iters 20000 --trace "${temporary_dir}/trace.csv" >/dev/null
python3 - "${temporary_dir}/trace.csv" <<'PY'
import csv
import sys

with open(sys.argv[1], newline="", encoding="utf-8") as handle:
    rows = list(csv.DictReader(handle))
if len(rows) < 2:
    raise SystemExit("收敛轨迹过短")
if float(rows[-1]["residual"]) > 1e-4:
    raise SystemExit("最终残差未达到阈值")
if float(rows[-1]["residual"]) >= float(rows[0]["residual"]):
    raise SystemExit("残差未呈整体下降趋势")
PY

echo "[test] 小规模网格加密呈二阶精度"
ACCURACY_SIZES="8 16 32" ACCURACY_TOL=1e-7 ACCURACY_MAX_ITERS=50000 \
    RESULT_DIR="${temporary_dir}/accuracy-results" \
    bash "${repo_dir}/scripts/accuracy.sh" > "${temporary_dir}/orders.csv"
python3 - "${temporary_dir}/orders.csv" <<'PY'
import csv
import sys

with open(sys.argv[1], newline="", encoding="utf-8") as handle:
    rows = list(csv.DictReader(handle))
orders = [float(row["observed_order"]) for row in rows[1:]]
if any(order < 1.8 or order > 2.2 for order in orders):
    raise SystemExit(f"观测收敛阶不在合理区间: {orders}")
PY

echo "[test] benchmark 冒烟"
SMOKE=1 RESULT_DIR="${temporary_dir}/benchmark-results" \
    bash "${repo_dir}/scripts/benchmark.sh" >/dev/null

echo "全部测试通过"
