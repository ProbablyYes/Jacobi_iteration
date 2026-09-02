#!/usr/bin/env bash
set -euo pipefail

repo_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
local_dir="${repo_dir}/.local"
source_dir="${local_dir}/src"
build_dir="${local_dir}/build/openmpi-4.1.6"
openmpi_prefix="${local_dir}/openmpi"
archive="${source_dir}/openmpi-4.1.6.tar.gz"
source_tree="${source_dir}/openmpi-4.1.6"
url=https://download.open-mpi.org/release/open-mpi/v4.1/openmpi-4.1.6.tar.gz

mkdir -p "${source_dir}" "${local_dir}/build"

if [[ ! -x "${openmpi_prefix}/bin/mpicc" ]]; then
    if [[ ! -f "${archive}" ]]; then
        echo "[env] 下载 OpenMPI 4.1.6" >&2
        curl --fail --location "${url}" --output "${archive}"
    fi
    if [[ ! -d "${source_tree}" ]]; then
        echo "[env] 解压 OpenMPI" >&2
        tar -xzf "${archive}" -C "${source_dir}"
    fi
    mkdir -p "${build_dir}"
    echo "[env] 配置用户级 OpenMPI: ${openmpi_prefix}" >&2
    pushd "${build_dir}" >/dev/null
    "${source_tree}/configure" \
        --prefix="${openmpi_prefix}" \
        --disable-mpi-fortran \
        --disable-oshmem \
        --without-verbs \
        --without-ucx \
        --without-libfabric \
        --enable-mpirun-prefix-by-default
    echo "[env] 编译 OpenMPI" >&2
    make -j"$(getconf _NPROCESSORS_ONLN)"
    make install
    popd >/dev/null
fi

if [[ ! -x "${repo_dir}/.venv/bin/python" ]]; then
    echo "[env] 尝试创建项目 Python 虚拟环境" >&2
    python3 -m venv "${repo_dir}/.venv" || true
fi
if [[ -x "${repo_dir}/.venv/bin/python" ]] && \
   "${repo_dir}/.venv/bin/python" -m pip --version >/dev/null 2>&1; then
    "${repo_dir}/.venv/bin/python" -m pip install --disable-pip-version-check \
        -r "${repo_dir}/requirements.txt"
else
    echo "[env] python3-venv 不可用，改用项目级 --target 安装" >&2
    mkdir -p "${local_dir}/python"
    python3 -m pip install --disable-pip-version-check \
        --target "${local_dir}/python" -r "${repo_dir}/requirements.txt"
fi

echo "[env] 环境就绪。使用: source scripts/activate_env.sh" >&2
"${openmpi_prefix}/bin/mpirun" --version | head -n 1
