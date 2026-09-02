#!/usr/bin/env bash
set -euo pipefail

repo_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
result_dir="${repo_dir}/results"
if [[ -x "${repo_dir}/.local/openmpi/bin/mpirun" ]]; then
    default_mpiexec="${repo_dir}/.local/openmpi/bin/mpirun"
else
    default_mpiexec=mpirun
fi
mpiexec_cmd=${MPIEXEC:-${default_mpiexec}}
grid_size=${GRID_SIZE:-2048}
fixed_iters=${FIXED_ITERS:-200}
repetitions=${REPETITIONS:-5}
warmups=${WARMUPS:-1}
process_list=${PROCESSES:-"1 2 4 8"}
size_list=${SIZE_LIST:-"512 1024 2048"}
tolerance_list=${TOLERANCE_LIST:-"1e-3 1e-5 1e-7"}
sensitivity_processes=${SENSITIVITY_PROCESSES:-4}
tolerance_size=${TOLERANCE_SIZE:-64}
max_iters=${MAX_ITERS:-1000000}
physical_cores=$(lscpu -p=CORE,SOCKET 2>/dev/null | sed '/^#/d' | sort -u | wc -l)
if [[ ${physical_cores} -lt 1 ]]; then
    physical_cores=$(getconf _NPROCESSORS_ONLN)
fi

if [[ "${SMOKE:-0}" == "1" ]]; then
    grid_size=64
    fixed_iters=5
    repetitions=1
    warmups=0
    process_list="1 2"
    size_list="32 64"
    tolerance_list="1e-2"
    sensitivity_processes=2
    tolerance_size=16
    max_iters=20000
fi

mkdir -p "${result_dir}"
raw_file="${result_dir}/raw.csv"
meta_file="${result_dir}/system_info.txt"

"${repo_dir}/bin/jacobi_serial" --size 8 --fixed-iters 1 --format csv | head -n 1 > "${raw_file}"
{
    date --iso-8601=seconds
    uname -a
    lscpu
    free -h
    "${CC:-gcc}" --version
    "${mpiexec_cmd}" --version
} > "${meta_file}"

append_serial() {
    local size=$1
    local iterations=$2
    "${repo_dir}/bin/jacobi_serial" --size "${size}" --fixed-iters "${iterations}" \
        --format csv | tail -n 1 >> "${raw_file}"
}

mpi_launch() {
    local processes=$1
    shift
    local placement=()
    if [[ -n "${MPIEXEC_ARGS:-}" ]]; then
        read -r -a placement <<< "${MPIEXEC_ARGS}"
    elif (( processes > physical_cores )); then
        placement=(--use-hwthread-cpus --map-by hwthread --bind-to hwthread)
    else
        placement=(--map-by core --bind-to core)
    fi
    "${mpiexec_cmd}" "${placement[@]}" -np "${processes}" "$@"
}

append_mpi() {
    local executable=$1
    local processes=$2
    local size=$3
    shift 3
    mpi_launch "${processes}" "${repo_dir}/bin/${executable}" \
        --size "${size}" "$@" --format csv | tail -n 1 >> "${raw_file}"
}

echo "[benchmark] 强扩展基准 N=${grid_size}, iterations=${fixed_iters}" >&2
for ((run = 0; run < warmups; ++run)); do
    "${repo_dir}/bin/jacobi_serial" --size "${grid_size}" --fixed-iters "${fixed_iters}" >/dev/null
done
for ((run = 0; run < repetitions; ++run)); do
    append_serial "${grid_size}" "${fixed_iters}"
done

for processes in ${process_list}; do
    for executable in jacobi_mpi_blocking jacobi_mpi_overlap; do
        for ((run = 0; run < warmups; ++run)); do
            mpi_launch "${processes}" "${repo_dir}/bin/${executable}" \
                --size "${grid_size}" --fixed-iters "${fixed_iters}" >/dev/null
        done
        for ((run = 0; run < repetitions; ++run)); do
            append_mpi "${executable}" "${processes}" "${grid_size}" \
                --fixed-iters "${fixed_iters}"
        done
    done
done

echo "[benchmark] 规模敏感性，P=${sensitivity_processes}" >&2
for size in ${size_list}; do
    for executable in jacobi_mpi_blocking jacobi_mpi_overlap; do
        for ((run = 0; run < repetitions; ++run)); do
            append_mpi "${executable}" "${sensitivity_processes}" "${size}" \
                --fixed-iters "${fixed_iters}"
        done
    done
done

echo "[benchmark] 容差敏感性，N=${tolerance_size}, P=${sensitivity_processes}" >&2
for tolerance in ${tolerance_list}; do
    for executable in jacobi_mpi_blocking jacobi_mpi_overlap; do
        for ((run = 0; run < repetitions; ++run)); do
            append_mpi "${executable}" "${sensitivity_processes}" "${tolerance_size}" \
                --tol "${tolerance}" --max-iters "${max_iters}"
        done
    done
done

mpi_launch "${sensitivity_processes}" "${repo_dir}/bin/jacobi_mpi_overlap" \
    --size "${tolerance_size}" --tol 1e-7 --max-iters "${max_iters}" \
    --trace "${result_dir}/convergence.csv" >/dev/null

echo "原始数据: ${raw_file}" >&2
echo "环境信息: ${meta_file}" >&2
